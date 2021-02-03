#ifndef INTERNET_EXPLORER_H
#define INTERNET_EXPLORER_H

bool find_internet_explorer_version(TCHAR* ie_version, u32 ie_version_size);
void export_default_or_specific_internet_explorer_cache(Exporter* exporter);

// These functions are only meant to be used in the Windows 2000 through 10 builds. In the Windows 98 and ME builds, attempting
// to call these functions will result in a compile time error.
// If we want to use them, we have to explicitly wrap the code with #ifndef BUILD_9X [...] #endif.
#ifndef BUILD_9X
	void load_esent_functions(void);
	void free_esent_functions(void);
#else
	#define load_esent_functions(...) _STATIC_ASSERT(false)
	#define free_esent_functions(...) _STATIC_ASSERT(false)
#endif

#endif
