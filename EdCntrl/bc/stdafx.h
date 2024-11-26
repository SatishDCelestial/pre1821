#pragma once

#include <SDKDDKVer.h>
#include <windows.h>

#include <memory.h>
#include <string.h>
#include <tchar.h>
#include <assert.h>

#define _SILENCE_CXX17_STRSTREAM_DEPRECATION_WARNING
#include <istream>
#include <strstream>
#include <string>

#define VS_STD		std
#define ASSERT(a)	assert(a)
