#include <WinVer.h>

#define STR(x) #x
#define STR_EXPAND(x) STR(x)

#define WCE_COMMA_VERSION		WCE_MAJOR,WCE_MINOR,WCE_PATCH,WCE_BUILD
#define WCE_DOT_VERSION			STR_EXPAND(WCE_MAJOR) "." STR_EXPAND(WCE_MINOR) "." STR_EXPAND(WCE_PATCH) "." STR_EXPAND(WCE_BUILD)

// These values are defined in "WinVer.h".
#ifdef WCE_9X
	#define WCE_FILE_OS 		VOS__WINDOWS32
#else
	#define WCE_FILE_OS 		VOS_NT_WINDOWS32
#endif

#define WCE_FILE_TYPE 			VFT_APP

#define WCE_TITLE 				"Web Cache Exporter"

#define WCE_COMMENTS 			"This application exports the cache from various web browsers and plugins."
#define WCE_COMPANY_NAME 		"Jo\xE3o Henggeler"
#define WCE_FILE_DESCRIPTION 	WCE_TITLE
#define WCE_FILE_VERSION 		WCE_DOT_VERSION
#define WCE_INTERNAL_NAME 		WCE_TITLE
#define WCE_LEGAL_COPYRIGHT 	"Copyright \xA9 2020-2023 " WCE_COMPANY_NAME
#define WCE_ORIGINAL_FILENAME 	WCE_FILENAME
#define WCE_PRODUCT_NAME 		WCE_TITLE
#define WCE_PRODUCT_VERSION 	WCE_DOT_VERSION

#ifdef WCE_9X
	#define WCE_ICON_PATH 		"icon_green.ico"
#else
	#ifdef WCE_32_BIT
		#define WCE_ICON_PATH 	"icon_red.ico"
	#else
		#define WCE_ICON_PATH 	"icon_yellow.ico"
	#endif
#endif
