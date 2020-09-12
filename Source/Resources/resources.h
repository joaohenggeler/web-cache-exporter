#include <WinVer.h>

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

#define WCE_COMMA_VERSION		MAJOR_VERSION,MINOR_VERSION,PATCH_VERSION,BUILD_NUMBER
#define WCE_DOTTED_VERSION		STR(MAJOR_VERSION) "." STR(MINOR_VERSION) "." STR(PATCH_VERSION) "." STR(BUILD_NUMBER)

// These values are defined in "WinVer.h".
#ifdef BUILD_9X
	#define WCE_FILE_OS 		VOS__WINDOWS32
#else
	#define WCE_FILE_OS 		VOS_NT_WINDOWS32
#endif

#define WCE_FILE_TYPE 			VFT_APP

#define WCE_NAME 				"Web Cache Exporter"

#define WCE_COMMENTS 			"This application exports the cache from various web browsers and plugins."
#define WCE_COMPANY_NAME 		"Jo\xE3o Henggeler"
#define WCE_FILE_DESCRIPTION 	WCE_NAME
#define WCE_FILE_VERSION 		WCE_DOTTED_VERSION
#define WCE_INTERNAL_NAME 		WCE_NAME
#define WCE_LEGAL_COPYRIGHT 	"Copyright \xA9 2020 " WCE_COMPANY_NAME
#define WCE_ORIGINAL_FILENAME 	WCE_EXE_FILENAME
#define WCE_PRODUCT_NAME 		WCE_NAME
#define WCE_PRODUCT_VERSION 	WCE_DOTTED_VERSION

#ifdef BUILD_9X
	#define WCE_ICON_PATH 		"icon_green.ico"
#else
	#ifdef BUILD_32_BIT
		#define WCE_ICON_PATH 	"icon_red.ico"
	#else
		#define WCE_ICON_PATH 	"icon_blue.ico"
	#endif
#endif
