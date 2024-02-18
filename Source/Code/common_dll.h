#ifndef COMMON_DLL_H
#define COMMON_DLL_H

#include "common_core.h"

#define SH_GET_KNOWN_FOLDER_PATH(name) HRESULT __stdcall name(const GUID & rfid, DWORD dwFlags, HANDLE hToken, PWSTR *ppszPath)
typedef SH_GET_KNOWN_FOLDER_PATH(Sh_Get_Known_Folder_Path);

extern Sh_Get_Known_Folder_Path* dll_sh_get_known_folder_path;
#define SHGetKnownFolderPath dll_sh_get_known_folder_path

void dll_initialize(void);
void dll_terminate(void);

#endif