#include "common.h"

static HMODULE shell32 = NULL;

#pragma warning(push)
#pragma warning(disable : 4100)
static SH_GET_KNOWN_FOLDER_PATH(stub_sh_get_known_folder_path)
{
	return E_NOTIMPL;
}
#pragma warning(pop)

Sh_Get_Known_Folder_Path* dll_sh_get_known_folder_path = stub_sh_get_known_folder_path;

void dll_initialize(void)
{
	shell32 = LoadLibraryA("Shell32.dll");

	if(shell32 != NULL)
	{
		void* function = GetProcAddress(shell32, "SHGetKnownFolderPath");

		if(function != NULL)
		{
			dll_sh_get_known_folder_path = (Sh_Get_Known_Folder_Path*) function;
		}
		else
		{
			log_error("Failed to get the address of SHGetKnownFolderPath with the error: %s", last_error_message());
		}
	}
	else
	{
		log_error("Failed to load Shell32 with the error: %s", last_error_message());
	}
}

void dll_terminate(void)
{
	if(shell32 != NULL)
	{
		FreeLibrary(shell32);
		shell32 = NULL;
		dll_sh_get_known_folder_path = stub_sh_get_known_folder_path;
	}
}