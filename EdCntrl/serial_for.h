#pragma once

#include <functional>

// this is just a utility class to help when comparing concurrency::parallel_for
// to a serial loop.
//		concurrency::parallel_for(...
// becomes
//		serial_for<T>(...

template <typename T> void serial_for(T start, T end, std::function<void(T)> f)
{
	for (DWORD x = (DWORD)start; x < (DWORD)end; ++x)
		f((T)x);
}
