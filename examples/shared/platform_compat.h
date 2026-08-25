#pragma once

// Small compatibility shim so the examples build with MSVC, which does not
// ship <unistd.h> and prefixes the POSIX names with an underscore.
// Shared across examples to avoid repeating the same #ifdef block.

#ifdef _WIN32
#include <io.h>

#define isatty _isatty
#define popen _popen
#define pclose _pclose

#else
#include <unistd.h>
#endif // _WIN32
