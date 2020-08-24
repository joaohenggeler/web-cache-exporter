#ifndef INTERNET_EXPLORER_H
#define INTERNET_EXPLORER_H

bool find_internet_explorer_version(TCHAR* ie_version, u32 ie_version_size);
void export_specific_or_default_internet_explorer_cache(Exporter* exporter);

#ifndef BUILD_9X
	void windows_nt_load_esent_functions(void);
	void windows_nt_free_esent_functions(void);
#else
	#define windows_nt_load_esent_functions(...) _STATIC_ASSERT(false)
	#define windows_nt_free_esent_functions(...) _STATIC_ASSERT(false)
#endif

#endif
