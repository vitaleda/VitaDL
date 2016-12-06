#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <psp2/appmgr.h>
#include <psp2/apputil.h>
#include <psp2/types.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/message_dialog.h>
#include <psp2/ime_dialog.h>
#include <psp2/display.h>
#include <psp2/apputil.h>

#include <psp2/types.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/net/http.h>
#include <psp2/io/stat.h>
#include <psp2/sysmodule.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>

#include <vita2d.h>

#define SCE_IME_DIALOG_MAX_TITLE_LENGTH	(128)
#define SCE_IME_DIALOG_MAX_TEXT_LENGTH	(512)

#define IME_DIALOG_RESULT_NONE 0
#define IME_DIALOG_RESULT_RUNNING 1
#define IME_DIALOG_RESULT_FINISHED 2
#define IME_DIALOG_RESULT_CANCELED 3


static uint16_t ime_title_utf16[SCE_IME_DIALOG_MAX_TITLE_LENGTH];
static uint16_t ime_initial_text_utf16[SCE_IME_DIALOG_MAX_TEXT_LENGTH];
static uint16_t ime_input_text_utf16[SCE_IME_DIALOG_MAX_TEXT_LENGTH + 1];
static uint8_t ime_input_text_utf8[SCE_IME_DIALOG_MAX_TEXT_LENGTH + 1];

void utf16_to_utf8(uint16_t *src, uint8_t *dst) {
	int i;
	for (i = 0; src[i]; i++) {
		if ((src[i] & 0xFF80) == 0) {
			*(dst++) = src[i] & 0xFF;
		} else if((src[i] & 0xF800) == 0) {
			*(dst++) = ((src[i] >> 6) & 0xFF) | 0xC0;
			*(dst++) = (src[i] & 0x3F) | 0x80;
		} else if((src[i] & 0xFC00) == 0xD800 && (src[i + 1] & 0xFC00) == 0xDC00) {
			*(dst++) = (((src[i] + 64) >> 8) & 0x3) | 0xF0;
			*(dst++) = (((src[i] >> 2) + 16) & 0x3F) | 0x80;
			*(dst++) = ((src[i] >> 4) & 0x30) | 0x80 | ((src[i + 1] << 2) & 0xF);
			*(dst++) = (src[i + 1] & 0x3F) | 0x80;
			i += 1;
		} else {
			*(dst++) = ((src[i] >> 12) & 0xF) | 0xE0;
			*(dst++) = ((src[i] >> 6) & 0x3F) | 0x80;
			*(dst++) = (src[i] & 0x3F) | 0x80;
		}
	}

	*dst = '\0';
}

void utf8_to_utf16(uint8_t *src, uint16_t *dst) {
	int i;
	for (i = 0; src[i];) {
		if ((src[i] & 0xE0) == 0xE0) {
			*(dst++) = ((src[i] & 0x0F) << 12) | ((src[i + 1] & 0x3F) << 6) | (src[i + 2] & 0x3F);
			i += 3;
		} else if ((src[i] & 0xC0) == 0xC0) {
			*(dst++) = ((src[i] & 0x1F) << 6) | (src[i + 1] & 0x3F);
			i += 2;
		} else {
			*(dst++) = src[i];
			i += 1;
		}
	}

	*dst = '\0';
}
 
void initImeDialog(char *title, char *initial_text, int max_text_length) {
    // Convert UTF8 to UTF16
	utf8_to_utf16((uint8_t *)title, ime_title_utf16);
	utf8_to_utf16((uint8_t *)initial_text, ime_initial_text_utf16);
 
    SceImeDialogParam param;
	sceImeDialogParamInit(&param);

	param.sdkVersion = 0x03150021,
	param.supportedLanguages = 0x0001FFFF;
	param.languagesForced = SCE_TRUE;
	param.type = SCE_IME_TYPE_BASIC_LATIN;
	param.title = ime_title_utf16;
	param.maxTextLength = max_text_length;
	param.initialText = ime_initial_text_utf16;
	param.inputTextBuffer = ime_input_text_utf16;

	//int res = 
	sceImeDialogInit(&param);
	return ;
}

void oslOskGetText(char *text){
	// Convert UTF16 to UTF8
	utf16_to_utf8(ime_input_text_utf16, ime_input_text_utf8);
	strcpy(text,(char*)ime_input_text_utf8);
}

int download_file(const char *src, const char *dst) {
	int ret;
	int tpl = sceHttpCreateTemplate("vitadl template", 2, 1);
	if (tpl < 0) {
		return tpl;
	}
	int conn = sceHttpCreateConnectionWithURL(tpl, src, 0);
	if (conn < 0) {
		return conn;
	}
	int req = sceHttpCreateRequestWithURL(conn, 0, src, 0);
	if (req < 0) {
		return req;
	}
	ret = sceHttpSendRequest(req, NULL, 0);
	if (ret < 0) {
		return ret;
	}
	unsigned char buf[4096] = {0};

	long long length = 0;
	ret = sceHttpGetResponseContentLength(req, &length);

	int fd = sceIoOpen(dst, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 6);
	int total_read = 0;
	if (fd < 0) {
		return fd;
	}
	while (1) {
		int read = sceHttpReadData(req, buf, sizeof(buf));
		if (read < 0) {
			return read;
		}
		if (read == 0)
			break;
		ret = sceIoWrite(fd, buf, read);
		if (ret < 0 || ret != read) {
			if (ret < 0)
				return ret;
			return -1;
		}
		total_read += read;
	}
	ret = sceIoClose(fd);

	return 0;
}

static void mkdirs(const char *dir) {
	char dir_copy[0x400] = {0};
	snprintf(dir_copy, sizeof(dir_copy) - 2, "%s", dir);
	dir_copy[strlen(dir_copy)] = '/';
	char *c;
	for (c = dir_copy; *c; ++c) {
		if (*c == '/') {
			*c = '\0';
			sceIoMkdir(dir_copy, 0777);
			*c = '/';
		}
	}
}

void netInit() {
	sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
	
	SceNetInitParam netInitParam;
	int size = 1*1024*1024;
	netInitParam.memory = malloc(size);
	netInitParam.size = size;
	netInitParam.flags = 0;
	sceNetInit(&netInitParam);

	sceNetCtlInit();
}

void httpInit() {
	sceSysmoduleLoadModule(SCE_SYSMODULE_HTTP);

	sceHttpInit(1*1024*1024);
}

int main(int argc, const char *argv[]) {
    
	sceAppUtilInit(&(SceAppUtilInitParam){}, &(SceAppUtilBootParam){});
    sceCommonDialogSetConfigParam(&(SceCommonDialogConfigParam){});
 
    vita2d_init();
	
	netInit();
	httpInit();
   
    int shown_dial = 0;
	
	char userText[512] = "http://";

    while (1) {
        if(!shown_dial){
            initImeDialog("Enter the link to the file you want to DL:", userText, 128);
			shown_dial=1;
        }

        vita2d_start_drawing();
        vita2d_clear_screen();
       
        SceCommonDialogStatus status = sceImeDialogGetStatus();
       
		if (status == IME_DIALOG_RESULT_FINISHED) {
			SceImeDialogResult result;
			memset(&result, 0, sizeof(SceImeDialogResult));
			sceImeDialogGetResult(&result);

			if (result.button == SCE_IME_DIALOG_BUTTON_CLOSE) {
				status = IME_DIALOG_RESULT_CANCELED;
				break;
			} else {
				oslOskGetText(userText);
			}

			sceImeDialogTerm();
			shown_dial = 0;
			
			mkdirs("ux0:/Downloads/");

			if (download_file(userText, "ux0:Downloads/DownloadedFile.tmp") < 0)
				goto fail;

fail:
			break;
		}
		
        vita2d_end_drawing();
        vita2d_common_dialog_update();
        vita2d_swap_buffers();
        sceDisplayWaitVblankStart();
    }
    sceKernelExitProcess(0);
    return 0;
}