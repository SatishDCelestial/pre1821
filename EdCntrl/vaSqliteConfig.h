
// https://www.sqlite.org/threadsafe.html
// VA will guarantee thread-safety of db connections
#define SQLITE_THREADSAFE 2

#if 1
// [case: 92495] have sqlite use independent heap to help reduce fragmentation of crt heap
#define SQLITE_WIN32_MALLOC 1
#else
#ifdef _DEBUG
#define _CRTDBG_MAP_ALLOC
#define SQLITE_SYSTEM_MALLOC 1
#include <crtdbg.h>
#endif
#endif

#if defined(SQLITE_SYSTEM_MALLOC) || !defined(_DEBUG)
// https://www.sqlite.org/compile.html#default_memstatus
// https://www.sqlite.org/c3ref/c_config_covering_index_scan.html#sqliteconfigmemstatus
// Performance improvement to reduce sqlite lock contention used by mem status
#define SQLITE_DEFAULT_MEMSTATUS 0
#else
// put "(sqlite3Stat).nowValue" in watch window to see sqlite mem usage if SQLITE_DEFAULT_MEMSTATUS > 0
#endif
