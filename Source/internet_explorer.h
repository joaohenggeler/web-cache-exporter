#ifndef INTERNET_EXPLORER_H
#define INTERNET_EXPLORER_H

bool find_internet_explorer_cache(TCHAR* cache_path);
void export_specific_or_default_internet_explorer_cache(Exporter* exporter);
void export_specific_internet_explorer_cache(Exporter* exporter);

#endif
