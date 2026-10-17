#pragma once
/* Included first in every simulator source (CMakeLists.txt): what the
 * firmware's C library has and a PC's may not. */
#ifdef _WIN32
#define _POSIX_THREAD_SAFE_FUNCTIONS 200112L /* MinGW: gmtime_r, localtime_r */
#endif
#include <stddef.h>
#include <string.h>
#include <time.h>

size_t sim_strlcpy(char *dst, const char *src, size_t size);
#define strlcpy sim_strlcpy
