#ifndef INTERNET_EXPLORER_H
#define INTERNET_EXPLORER_H

void export_specific_or_default_internet_explorer_cache(Exporter* exporter);
void export_internet_explorer_4_to_9_cache(Exporter* exporter);
#ifndef BUILD_9X
	void windows_nt_export_internet_explorer_10_to_11_cache(Exporter* exporter);
#else
	#define windows_nt_export_internet_explorer_10_to_11_cache(...) _STATIC_ASSERT(false)
#endif

#endif
