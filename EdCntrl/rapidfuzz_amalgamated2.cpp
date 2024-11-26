#include <utility>
#include <iterator>
#include <stdexcept>
#include <algorithm>
#include <vector>
#include <ostream>
#include <unordered_set>
#include <array>
#include <bitset>
#include <cassert>

#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#endif


#include "ltalloc/ltalloc_va.h"

namespace lt
{
template <class T>
struct allocator
{
	typedef size_t size_type;
	typedef ptrdiff_t difference_type;
	typedef T* pointer;
	typedef const T* const_pointer;
	typedef T& reference;
	typedef const T& const_reference;
	typedef T value_type;

	template <class U>
	struct rebind
	{
		typedef allocator<U> other;
	};
	allocator() throw()
	{
	}
	allocator(const allocator&) throw()
	{
	}

	template <class U>
	allocator(const allocator<U>&) throw()
	{
	}

	~allocator() throw()
	{
	}

	static pointer address(reference x) /*const*/
	{
		return &x;
	}
	static const_pointer address(const_reference x) /*const*/
	{
		return &x;
	}

	static pointer allocate(size_type s, void const* = nullptr)
	{
		if (0 == s)
			return nullptr;
		pointer temp = (pointer)get_ltalloc()->ltmalloc(s * sizeof(T));
		if (temp == nullptr)
			throw std::bad_alloc();
		return temp;
	}

	static void deallocate(pointer p, size_type = {})
	{
		if(p)
			get_ltalloc()->ltfree(p);
	}

	static constexpr size_type max_size() /*const*/ throw()
	{
		return std::numeric_limits<size_t>::max() / sizeof(T);
	}

	static void construct(pointer p, const T& val)
	{
		new ((void*)p) T(val);
	}

	static void destroy(pointer p)
	{
		p->~T();
	}
};

template <class T1, class T2>
constexpr bool operator==(const allocator<T1>&, const allocator<T2>&) noexcept { return true; }
template <class T1, class T2>
constexpr bool operator!=(const allocator<T1>&, const allocator<T2>&) noexcept { return false; }
} // namespace lt
template<typename T>
using ltvector = std::vector<T, lt::allocator<T>>;


#ifndef RAPIDFUZZ_EXCLUDE_SIMD
#if __AVX2__
#define RAPIDFUZZ_SIMD
#define RAPIDFUZZ_AVX2
#define RAPIDFUZZ_LTO_HACK 0

#include <array>
#include <immintrin.h>
#include <ostream>
#include <stdint.h>

namespace rapidfuzz
{
namespace detail
{
namespace simd_avx2
{

template <typename T>
class native_simd;

template <>
class native_simd<uint64_t>
{
  public:
	using value_type = uint64_t;

	static const int _size = 4;
	__m256i xmm;

	native_simd() noexcept
	{
	}

	native_simd(__m256i val) noexcept
	    : xmm(val)
	{
	}

	native_simd(uint64_t a) noexcept
	{
		xmm = _mm256_set1_epi64x(static_cast<int64_t>(a));
	}

	native_simd(const uint64_t* p) noexcept
	{
		load(p);
	}

	operator __m256i() const noexcept
	{
		return xmm;
	}

	constexpr static int size() noexcept
	{
		return _size;
	}

	native_simd load(const uint64_t* p) noexcept
	{
		xmm = _mm256_set_epi64x(static_cast<int64_t>(p[3]), static_cast<int64_t>(p[2]),
		                        static_cast<int64_t>(p[1]), static_cast<int64_t>(p[0]));
		return *this;
	}

	void store(uint64_t* p) const noexcept
	{
		_mm256_store_si256(reinterpret_cast<__m256i*>(p), xmm);
	}

	native_simd operator+(const native_simd b) const noexcept
	{
		return _mm256_add_epi64(xmm, b);
	}

	native_simd& operator+=(const native_simd b) noexcept
	{
		xmm = _mm256_add_epi64(xmm, b);
		return *this;
	}

	native_simd operator-(const native_simd b) const noexcept
	{
		return _mm256_sub_epi64(xmm, b);
	}

	native_simd& operator-=(const native_simd b) noexcept
	{
		xmm = _mm256_sub_epi64(xmm, b);
		return *this;
	}
};

template <>
class native_simd<uint32_t>
{
  public:
	using value_type = uint32_t;

	static const int _size = 8;
	__m256i xmm;

	native_simd() noexcept
	{
	}

	native_simd(__m256i val) noexcept
	    : xmm(val)
	{
	}

	native_simd(uint32_t a) noexcept
	{
		xmm = _mm256_set1_epi32(static_cast<int>(a));
	}

	native_simd(const uint64_t* p) noexcept
	{
		load(p);
	}

	operator __m256i() const
	{
		return xmm;
	}

	constexpr static int size() noexcept
	{
		return _size;
	}

	native_simd load(const uint64_t* p) noexcept
	{
		xmm = _mm256_set_epi64x(static_cast<int64_t>(p[3]), static_cast<int64_t>(p[2]),
		                        static_cast<int64_t>(p[1]), static_cast<int64_t>(p[0]));
		return *this;
	}

	void store(uint32_t* p) const noexcept
	{
		_mm256_store_si256(reinterpret_cast<__m256i*>(p), xmm);
	}

	native_simd operator+(const native_simd b) const noexcept
	{
		return _mm256_add_epi32(xmm, b);
	}

	native_simd& operator+=(const native_simd b) noexcept
	{
		xmm = _mm256_add_epi32(xmm, b);
		return *this;
	}

	native_simd operator-(const native_simd b) const noexcept
	{
		return _mm256_sub_epi32(xmm, b);
	}

	native_simd& operator-=(const native_simd b) noexcept
	{
		xmm = _mm256_sub_epi32(xmm, b);
		return *this;
	}
};

template <>
class native_simd<uint16_t>
{
  public:
	using value_type = uint16_t;

	static const int _size = 16;
	__m256i xmm;

	native_simd() noexcept
	{
	}

	native_simd(__m256i val)
	    : xmm(val)
	{
	}

	native_simd(uint16_t a) noexcept
	{
		xmm = _mm256_set1_epi16(static_cast<short>(a));
	}

	native_simd(const uint64_t* p) noexcept
	{
		load(p);
	}

	operator __m256i() const noexcept
	{
		return xmm;
	}

	constexpr static int size() noexcept
	{
		return _size;
	}

	native_simd load(const uint64_t* p) noexcept
	{
		xmm = _mm256_set_epi64x(static_cast<int64_t>(p[3]), static_cast<int64_t>(p[2]),
		                        static_cast<int64_t>(p[1]), static_cast<int64_t>(p[0]));
		return *this;
	}

	void store(uint16_t* p) const noexcept
	{
		_mm256_store_si256(reinterpret_cast<__m256i*>(p), xmm);
	}

	native_simd operator+(const native_simd b) const noexcept
	{
		return _mm256_add_epi16(xmm, b);
	}

	native_simd& operator+=(const native_simd b) noexcept
	{
		xmm = _mm256_add_epi16(xmm, b);
		return *this;
	}

	native_simd operator-(const native_simd b) const noexcept
	{
		return _mm256_sub_epi16(xmm, b);
	}

	native_simd& operator-=(const native_simd b) noexcept
	{
		xmm = _mm256_sub_epi16(xmm, b);
		return *this;
	}
};

template <>
class native_simd<uint8_t>
{
  public:
	using value_type = uint8_t;

	static const int _size = 32;
	__m256i xmm;

	native_simd() noexcept
	{
	}

	native_simd(__m256i val) noexcept
	    : xmm(val)
	{
	}

	native_simd(uint8_t a) noexcept
	{
		xmm = _mm256_set1_epi8(static_cast<char>(a));
	}

	native_simd(const uint64_t* p) noexcept
	{
		load(p);
	}

	operator __m256i() const noexcept
	{
		return xmm;
	}

	constexpr static int size() noexcept
	{
		return _size;
	}

	native_simd load(const uint64_t* p) noexcept
	{
		xmm = _mm256_set_epi64x(static_cast<int64_t>(p[3]), static_cast<int64_t>(p[2]),
		                        static_cast<int64_t>(p[1]), static_cast<int64_t>(p[0]));
		return *this;
	}

	void store(uint8_t* p) const noexcept
	{
		_mm256_store_si256(reinterpret_cast<__m256i*>(p), xmm);
	}

	native_simd operator+(const native_simd b) const noexcept
	{
		return _mm256_add_epi8(xmm, b);
	}

	native_simd& operator+=(const native_simd b) noexcept
	{
		xmm = _mm256_add_epi8(xmm, b);
		return *this;
	}

	native_simd operator-(const native_simd b) const noexcept
	{
		return _mm256_sub_epi8(xmm, b);
	}

	native_simd& operator-=(const native_simd b) noexcept
	{
		xmm = _mm256_sub_epi8(xmm, b);
		return *this;
	}
};

template <typename T>
std::ostream& operator<<(std::ostream& os, const native_simd<T>& a)
{
	alignas(32) std::array<T, native_simd<T>::size()> res;
	a.store(&res[0]);

	for (size_t i = res.size() - 1; i != 0; i--)
		os << std::bitset<std::numeric_limits<T>::digits>(res[i]) << "|";

	os << std::bitset<std::numeric_limits<T>::digits>(res[0]);
	return os;
}

template <typename T>
__m256i hadd_impl(__m256i x) noexcept;

template <>
inline __m256i hadd_impl<uint8_t>(__m256i x) noexcept
{
	return x;
}

template <>
inline __m256i hadd_impl<uint16_t>(__m256i x) noexcept
{
	const __m256i mask = _mm256_set1_epi16(0x001f);
	__m256i y = _mm256_srli_si256(x, 1);
	x = _mm256_add_epi16(x, y);
	return _mm256_and_si256(x, mask);
}

template <>
inline __m256i hadd_impl<uint32_t>(__m256i x) noexcept
{
	const __m256i mask = _mm256_set1_epi32(0x0000003F);
	x = hadd_impl<uint16_t>(x);
	__m256i y = _mm256_srli_si256(x, 2);
	x = _mm256_add_epi32(x, y);
	return _mm256_and_si256(x, mask);
}

template <>
inline __m256i hadd_impl<uint64_t>(__m256i x) noexcept
{
	return _mm256_sad_epu8(x, _mm256_setzero_si256());
}

/* based on the paper `Faster Population Counts Using AVX2 Instructions` */
template <typename T>
native_simd<T> popcount_impl(const native_simd<T>& v) noexcept
{
	__m256i lookup = _mm256_setr_epi8(0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4, 0, 1, 1, 2, 1, 2, 2, 3,
	                                  1, 2, 2, 3, 2, 3, 3, 4);
	const __m256i low_mask = _mm256_set1_epi8(0x0F);
	__m256i lo = _mm256_and_si256(v, low_mask);
	__m256i hi = _mm256_and_si256(_mm256_srli_epi32(v, 4), low_mask);
	__m256i popcnt1 = _mm256_shuffle_epi8(lookup, lo);
	__m256i popcnt2 = _mm256_shuffle_epi8(lookup, hi);
	__m256i total = _mm256_add_epi8(popcnt1, popcnt2);
	return hadd_impl<T>(total);
}

template <typename T>
std::array<T, native_simd<T>::size()> popcount(const native_simd<T>& a) noexcept
{
	alignas(32) std::array<T, native_simd<T>::size()> res;
	popcount_impl(a).store(&res[0]);
	return res;
}

// function andnot: a & ~ b
template <typename T>
native_simd<T> andnot(const native_simd<T>& a, const native_simd<T>& b)
{
	return _mm256_andnot_si256(b, a);
}

static inline native_simd<uint8_t> operator==(const native_simd<uint8_t>& a,
                                              const native_simd<uint8_t>& b) noexcept
{
	return _mm256_cmpeq_epi8(a, b);
}

static inline native_simd<uint16_t> operator==(const native_simd<uint16_t>& a,
                                               const native_simd<uint16_t>& b) noexcept
{
	return _mm256_cmpeq_epi16(a, b);
}

static inline native_simd<uint32_t> operator==(const native_simd<uint32_t>& a,
                                               const native_simd<uint32_t>& b) noexcept
{
	return _mm256_cmpeq_epi32(a, b);
}

static inline native_simd<uint64_t> operator==(const native_simd<uint64_t>& a,
                                               const native_simd<uint64_t>& b) noexcept
{
	return _mm256_cmpeq_epi64(a, b);
}

static inline native_simd<uint8_t> operator<<(const native_simd<uint8_t>& a, int b) noexcept
{
	return _mm256_and_si256(_mm256_slli_epi16(a, b),
	                        _mm256_set1_epi8(static_cast<char>(0xFF << (b & 0b1111))));
}

static inline native_simd<uint16_t> operator<<(const native_simd<uint16_t>& a, int b) noexcept
{
	return _mm256_slli_epi16(a, b);
}

static inline native_simd<uint32_t> operator<<(const native_simd<uint32_t>& a, int b) noexcept
{
	return _mm256_slli_epi32(a, b);
}

static inline native_simd<uint64_t> operator<<(const native_simd<uint64_t>& a, int b) noexcept
{
	return _mm256_slli_epi64(a, b);
}

template <typename T>
native_simd<T> operator&(const native_simd<T>& a, const native_simd<T>& b) noexcept
{
	return _mm256_and_si256(a, b);
}

template <typename T>
native_simd<T> operator&=(native_simd<T>& a, const native_simd<T>& b) noexcept
{
	a = a & b;
	return a;
}

template <typename T>
native_simd<T> operator|(const native_simd<T>& a, const native_simd<T>& b) noexcept
{
	return _mm256_or_si256(a, b);
}

template <typename T>
native_simd<T> operator|=(native_simd<T>& a, const native_simd<T>& b) noexcept
{
	a = a | b;
	return a;
}

template <typename T>
native_simd<T> operator^(const native_simd<T>& a, const native_simd<T>& b) noexcept
{
	return _mm256_xor_si256(a, b);
}

template <typename T>
native_simd<T> operator^=(native_simd<T>& a, const native_simd<T>& b) noexcept
{
	a = a ^ b;
	return a;
}

template <typename T>
native_simd<T> operator~(const native_simd<T>& a) noexcept
{
	return _mm256_xor_si256(a, _mm256_set1_epi32(-1));
}

} // namespace simd_avx2
} // namespace detail
} // namespace rapidfuzz

#elif (defined(_M_AMD64) || defined(_M_X64)) || defined(__SSE2__)
#define RAPIDFUZZ_SIMD
#define RAPIDFUZZ_SSE2
#define RAPIDFUZZ_LTO_HACK 1

#include <array>
#include <emmintrin.h>
#include <ostream>
#include <stdint.h>

namespace rapidfuzz
{
namespace detail
{
namespace simd_sse2
{

template <typename T>
class native_simd;

template <>
class native_simd<uint64_t>
{
  public:
	static const int _size = 2;
	__m128i xmm;

	native_simd() noexcept
	{
	}

	native_simd(__m128i val) noexcept
	    : xmm(val)
	{
	}

	native_simd(uint64_t a) noexcept
	{
		xmm = _mm_set1_epi64x(static_cast<int64_t>(a));
	}

	native_simd(const uint64_t* p) noexcept
	{
		load(p);
	}

	operator __m128i() const noexcept
	{
		return xmm;
	}

	constexpr static int size() noexcept
	{
		return _size;
	}

	native_simd load(const uint64_t* p) noexcept
	{
		xmm = _mm_set_epi64x(static_cast<int64_t>(p[1]), static_cast<int64_t>(p[0]));
		return *this;
	}

	void store(uint64_t* p) const noexcept
	{
		_mm_store_si128(reinterpret_cast<__m128i*>(p), xmm);
	}

	native_simd operator+(const native_simd b) const noexcept
	{
		return _mm_add_epi64(xmm, b);
	}

	native_simd& operator+=(const native_simd b) noexcept
	{
		xmm = _mm_add_epi64(xmm, b);
		return *this;
	}

	native_simd operator-(const native_simd b) const noexcept
	{
		return _mm_sub_epi64(xmm, b);
	}

	native_simd& operator-=(const native_simd b) noexcept
	{
		xmm = _mm_sub_epi64(xmm, b);
		return *this;
	}
};

template <>
class native_simd<uint32_t>
{
  public:
	static const int _size = 4;
	__m128i xmm;

	native_simd() noexcept
	{
	}

	native_simd(__m128i val) noexcept
	    : xmm(val)
	{
	}

	native_simd(uint32_t a) noexcept
	{
		xmm = _mm_set1_epi32(static_cast<int>(a));
	}

	native_simd(const uint64_t* p) noexcept
	{
		load(p);
	}

	operator __m128i() const noexcept
	{
		return xmm;
	}

	constexpr static int size() noexcept
	{
		return _size;
	}

	native_simd load(const uint64_t* p) noexcept
	{
		xmm = _mm_set_epi64x(static_cast<int64_t>(p[1]), static_cast<int64_t>(p[0]));
		return *this;
	}

	void store(uint32_t* p) const noexcept
	{
		_mm_store_si128(reinterpret_cast<__m128i*>(p), xmm);
	}

	native_simd operator+(const native_simd b) const noexcept
	{
		return _mm_add_epi32(xmm, b);
	}

	native_simd& operator+=(const native_simd b) noexcept
	{
		xmm = _mm_add_epi32(xmm, b);
		return *this;
	}

	native_simd operator-(const native_simd b) const noexcept
	{
		return _mm_sub_epi32(xmm, b);
	}

	native_simd& operator-=(const native_simd b) noexcept
	{
		xmm = _mm_sub_epi32(xmm, b);
		return *this;
	}
};

template <>
class native_simd<uint16_t>
{
  public:
	static const int _size = 8;
	__m128i xmm;

	native_simd() noexcept
	{
	}

	native_simd(__m128i val) noexcept
	    : xmm(val)
	{
	}

	native_simd(uint16_t a) noexcept
	{
		xmm = _mm_set1_epi16(static_cast<short>(a));
	}

	native_simd(const uint64_t* p) noexcept
	{
		load(p);
	}

	operator __m128i() const noexcept
	{
		return xmm;
	}

	constexpr static int size() noexcept
	{
		return _size;
	}

	native_simd load(const uint64_t* p) noexcept
	{
		xmm = _mm_set_epi64x(static_cast<int64_t>(p[1]), static_cast<int64_t>(p[0]));
		return *this;
	}

	void store(uint16_t* p) const noexcept
	{
		_mm_store_si128(reinterpret_cast<__m128i*>(p), xmm);
	}

	native_simd operator+(const native_simd b) const noexcept
	{
		return _mm_add_epi16(xmm, b);
	}

	native_simd& operator+=(const native_simd b) noexcept
	{
		xmm = _mm_add_epi16(xmm, b);
		return *this;
	}

	native_simd operator-(const native_simd b) const noexcept
	{
		return _mm_sub_epi16(xmm, b);
	}

	native_simd& operator-=(const native_simd b) noexcept
	{
		xmm = _mm_sub_epi16(xmm, b);
		return *this;
	}
};

template <>
class native_simd<uint8_t>
{
  public:
	static const int _size = 16;
	__m128i xmm;

	native_simd() noexcept
	{
	}

	native_simd(__m128i val) noexcept
	    : xmm(val)
	{
	}

	native_simd(uint8_t a) noexcept
	{
		xmm = _mm_set1_epi8(static_cast<char>(a));
	}

	native_simd(const uint64_t* p) noexcept
	{
		load(p);
	}

	operator __m128i() const noexcept
	{
		return xmm;
	}

	constexpr static int size() noexcept
	{
		return _size;
	}

	native_simd load(const uint64_t* p) noexcept
	{
		xmm = _mm_set_epi64x(static_cast<int64_t>(p[1]), static_cast<int64_t>(p[0]));
		return *this;
	}

	void store(uint8_t* p) const noexcept
	{
		_mm_store_si128(reinterpret_cast<__m128i*>(p), xmm);
	}

	native_simd operator+(const native_simd b) const noexcept
	{
		return _mm_add_epi8(xmm, b);
	}

	native_simd& operator+=(const native_simd b) noexcept
	{
		xmm = _mm_add_epi8(xmm, b);
		return *this;
	}

	native_simd operator-(const native_simd b) const noexcept
	{
		return _mm_sub_epi8(xmm, b);
	}

	native_simd& operator-=(const native_simd b) noexcept
	{
		xmm = _mm_sub_epi8(xmm, b);
		return *this;
	}
};

template <typename T>
std::ostream& operator<<(std::ostream& os, const native_simd<T>& a)
{
	alignas(32) std::array<T, native_simd<T>::size()> res;
	a.store(&res[0]);

	for (size_t i = res.size() - 1; i != 0; i--)
		os << std::bitset<std::numeric_limits<T>::digits>(res[i]) << "|";

	os << std::bitset<std::numeric_limits<T>::digits>(res[0]);
	return os;
}

template <typename T>
__m128i hadd_impl(__m128i x) noexcept;

template <>
inline __m128i hadd_impl<uint8_t>(__m128i x) noexcept
{
	return x;
}

template <>
inline __m128i hadd_impl<uint16_t>(__m128i x) noexcept
{
	const __m128i mask = _mm_set1_epi16(0x001f);
	__m128i y = _mm_srli_si128(x, 1);
	x = _mm_add_epi16(x, y);
	return _mm_and_si128(x, mask);
}

template <>
inline __m128i hadd_impl<uint32_t>(__m128i x) noexcept
{
	const __m128i mask = _mm_set1_epi32(0x0000003f);
	x = hadd_impl<uint16_t>(x);
	__m128i y = _mm_srli_si128(x, 2);
	x = _mm_add_epi32(x, y);
	return _mm_and_si128(x, mask);
}

template <>
inline __m128i hadd_impl<uint64_t>(__m128i x) noexcept
{
	return _mm_sad_epu8(x, _mm_setzero_si128());
}

template <typename T>
native_simd<T> popcount_impl(const native_simd<T>& v) noexcept
{
	const __m128i m1 = _mm_set1_epi8(0x55);
	const __m128i m2 = _mm_set1_epi8(0x33);
	const __m128i m3 = _mm_set1_epi8(0x0F);

	/* Note: if we returned x here it would be like _mm_popcnt_epi1(x) */
	__m128i y;
	__m128i x = v;
	/* add even and odd bits*/
	y = _mm_srli_epi64(x, 1); // put even bits in odd place
	y = _mm_and_si128(y, m1); // mask out the even bits (0x55)
	x = _mm_subs_epu8(x, y);  // shortcut to mask even bits and add
	/* if we just returned x here it would be like popcnt_epi2(x) */
	/* now add the half nibbles */
	y = _mm_srli_epi64(x, 2); // move half nibbles in place to add
	y = _mm_and_si128(y, m2); // mask off the extra half nibbles (0x0f)
	x = _mm_and_si128(x, m2); // ditto
	x = _mm_adds_epu8(x, y);  // totals are a maximum of 5 bits (0x1f)
	/* if we just returned x here it would be like popcnt_epi4(x) */
	/* now add the nibbles */
	y = _mm_srli_epi64(x, 4); // move nibbles in place to add
	x = _mm_adds_epu8(x, y);  // totals are a maximum of 6 bits (0x3f)
	x = _mm_and_si128(x, m3); // mask off the extra bits

	/* todo use when sse3 available
	__m128i lookup = _mm_setr_epi8(0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4);
	const __m128i low_mask = _mm_set1_epi8(0x0F);
	__m128i lo = _mm_and_si128(v, low_mask);
	__m128i hi = _mm_and_si256(_mm_srli_epi32(v, 4), low_mask);
	__m128i popcnt1 = _mm_shuffle_epi8(lookup, lo);
	__m128i popcnt2 = _mm_shuffle_epi8(lookup, hi);
	__m128i total = _mm_add_epi8(popcnt1, popcnt2);*/

	return hadd_impl<T>(x);
}

template <typename T>
std::array<T, native_simd<T>::size()> popcount(const native_simd<T>& a) noexcept
{
	alignas(16) std::array<T, native_simd<T>::size()> res;
	popcount_impl(a).store(&res[0]);
	return res;
}

// function andnot: a & ~ b
template <typename T>
native_simd<T> andnot(const native_simd<T>& a, const native_simd<T>& b)
{
	return _mm_andnot_si128(b, a);
}

static inline native_simd<uint8_t> operator==(const native_simd<uint8_t>& a,
                                              const native_simd<uint8_t>& b) noexcept
{
	return _mm_cmpeq_epi8(a, b);
}

static inline native_simd<uint16_t> operator==(const native_simd<uint16_t>& a,
                                               const native_simd<uint16_t>& b) noexcept
{
	return _mm_cmpeq_epi16(a, b);
}

static inline native_simd<uint32_t> operator==(const native_simd<uint32_t>& a,
                                               const native_simd<uint32_t>& b) noexcept
{
	return _mm_cmpeq_epi32(a, b);
}

static inline native_simd<uint64_t> operator==(const native_simd<uint64_t>& a,
                                               const native_simd<uint64_t>& b) noexcept
{
	// no 64 compare instruction. Do two 32 bit compares
	__m128i com32 = _mm_cmpeq_epi32(a, b);           // 32 bit compares
	__m128i com32s = _mm_shuffle_epi32(com32, 0xB1); // swap low and high dwords
	__m128i test = _mm_and_si128(com32, com32s);     // low & high
	__m128i teste = _mm_srai_epi32(test, 31);        // extend sign bit to 32 bits
	__m128i testee = _mm_shuffle_epi32(teste, 0xF5); // extend sign bit to 64 bits
	return testee;
}

static inline native_simd<uint8_t> operator<<(const native_simd<uint8_t>& a, int b) noexcept
{
	return _mm_and_si128(_mm_slli_epi16(a, b), _mm_set1_epi8(static_cast<char>(0xFF << (b & 0b1111))));
}

static inline native_simd<uint16_t> operator<<(const native_simd<uint16_t>& a, int b) noexcept
{
	return _mm_slli_epi16(a, b);
}

static inline native_simd<uint32_t> operator<<(const native_simd<uint32_t>& a, int b) noexcept
{
	return _mm_slli_epi32(a, b);
}

static inline native_simd<uint64_t> operator<<(const native_simd<uint64_t>& a, int b) noexcept
{
	return _mm_slli_epi64(a, b);
}

template <typename T>
native_simd<T> operator&(const native_simd<T>& a, const native_simd<T>& b) noexcept
{
	return _mm_and_si128(a, b);
}

template <typename T>
native_simd<T> operator&=(native_simd<T>& a, const native_simd<T>& b) noexcept
{
	a = a & b;
	return a;
}

template <typename T>
native_simd<T> operator|(const native_simd<T>& a, const native_simd<T>& b) noexcept
{
	return _mm_or_si128(a, b);
}

template <typename T>
native_simd<T> operator|=(native_simd<T>& a, const native_simd<T>& b) noexcept
{
	a = a | b;
	return a;
}

template <typename T>
native_simd<T> operator^(const native_simd<T>& a, const native_simd<T>& b) noexcept
{
	return _mm_xor_si128(a, b);
}

template <typename T>
native_simd<T> operator^=(native_simd<T>& a, const native_simd<T>& b) noexcept
{
	a = a ^ b;
	return a;
}

template <typename T>
native_simd<T> operator~(const native_simd<T>& a) noexcept
{
	return _mm_xor_si128(a, _mm_set1_epi32(-1));
}

} // namespace simd_sse2
} // namespace detail
} // namespace rapidfuzz

#endif
#endif


namespace rapidfuzz::detail
{
template <typename CharT>
CharT* to_begin(CharT* s)
{
	return s;
}

template <typename T>
auto to_begin(T& x)
{
	using std::begin;
	return begin(x);
}

template <typename CharT>
CharT* to_end(CharT* s)
{
	assume(s != nullptr);
	while (*s != 0)
		++s;

	return s;
}

template <typename T>
auto to_end(T& x)
{
	using std::end;
	return end(x);
}

template <typename Iter>
class Range
{
	Iter _first;
	Iter _last;

  public:
	using value_type = typename std::iterator_traits<Iter>::value_type;
	using iterator = Iter;
	using reverse_iterator = std::reverse_iterator<iterator>;

	constexpr Range(Iter first, Iter last)
	    : _first(first), _last(last)
	{
	}

	template <typename T>
	constexpr Range(T& x)
	    : _first(to_begin(x)), _last(to_end(x))
	{
	}

	constexpr iterator begin() const noexcept
	{
		return _first;
	}
	constexpr iterator end() const noexcept
	{
		return _last;
	}

	constexpr reverse_iterator rbegin() const noexcept
	{
		return reverse_iterator(end());
	}
	constexpr reverse_iterator rend() const noexcept
	{
		return reverse_iterator(begin());
	}

	constexpr ptrdiff_t size() const
	{
		return std::distance(_first, _last);
	}
	constexpr bool empty() const
	{
		return size() == 0;
	}
	explicit constexpr operator bool() const
	{
		return !empty();
	}
	constexpr decltype(auto) operator[](ptrdiff_t n) const
	{
		return _first[n];
	}

	constexpr void remove_prefix(ptrdiff_t n)
	{
		_first += n;
	}
	constexpr void remove_suffix(ptrdiff_t n)
	{
		_last -= n;
	}

	constexpr Range subseq(ptrdiff_t pos = 0, ptrdiff_t count = std::numeric_limits<ptrdiff_t>::max())
	{
		if (pos > size())
			throw std::out_of_range("Index out of range in Range::substr");

		auto start = _first + pos;
		if (std::distance(start, _last) < count)
			return {start, _last};
		return {start, start + count};
	}

	constexpr decltype(auto) front() const
	{
		return *(_first);
	}

	constexpr decltype(auto) back() const
	{
		return *(_last - 1);
	}

	constexpr Range<reverse_iterator> reversed() const
	{
		return {rbegin(), rend()};
	}

	friend std::ostream& operator<<(std::ostream& os, const Range& seq)
	{
		os << "[";
		for (auto x : seq)
			os << static_cast<uint64_t>(x) << ", ";
		os << "]";
		return os;
	}
};

template <typename T>
Range(T& x) -> Range<decltype(to_begin(x))>;

template <typename InputIt1, typename InputIt2>
inline bool operator==(const Range<InputIt1>& a, const Range<InputIt2>& b)
{
	return std::equal(a.begin(), a.end(), b.begin(), b.end());
}

template <typename InputIt1, typename InputIt2>
inline bool operator!=(const Range<InputIt1>& a, const Range<InputIt2>& b)
{
	return !(a == b);
}

template <typename InputIt1, typename InputIt2>
inline bool operator<(const Range<InputIt1>& a, const Range<InputIt2>& b)
{
	return (std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end()));
}

template <typename InputIt1, typename InputIt2>
inline bool operator>(const Range<InputIt1>& a, const Range<InputIt2>& b)
{
	return b < a;
}

template <typename InputIt1, typename InputIt2>
inline bool operator<=(const Range<InputIt1>& a, const Range<InputIt2>& b)
{
	return !(b < a);
}

template <typename InputIt1, typename InputIt2>
inline bool operator>=(const Range<InputIt1>& a, const Range<InputIt2>& b)
{
	return !(a < b);
}

template <typename InputIt>
using RangeVec = ltvector<Range<InputIt>>;
}

namespace rapidfuzz::detail
{

/*
 * taken from https://stackoverflow.com/a/17251989/11335032
 */
template <typename T, typename U>
bool CanTypeFitValue(const U value)
{
	const intmax_t botT = intmax_t(std::numeric_limits<T>::min());
	const intmax_t botU = intmax_t(std::numeric_limits<U>::min());
	const uintmax_t topT = uintmax_t(std::numeric_limits<T>::max());
	const uintmax_t topU = uintmax_t(std::numeric_limits<U>::max());
	return !((botT > botU && value < static_cast<U>(botT)) || (topT < topU && value > static_cast<U>(topT)));
}

template <typename CharT1, size_t size = sizeof(CharT1)>
struct CharSet;

template <typename CharT1>
struct CharSet<CharT1, 1>
{
	using UCharT1 = typename std::make_unsigned<CharT1>::type;

	std::array<bool, std::numeric_limits<UCharT1>::max() + 1> m_val;

	CharSet()
	    : m_val{}
	{
	}

	void insert(CharT1 ch)
	{
		m_val[UCharT1(ch)] = true;
	}

	template <typename CharT2>
	bool find(CharT2 ch) const
	{
		if (!CanTypeFitValue<CharT1>(ch))
			return false;

		return m_val[UCharT1(ch)];
	}
};

template <typename CharT1>
struct CharSet<CharT1, 2>
{
	using UCharT1 = typename std::make_unsigned<CharT1>::type;

	std::array<uint64_t, (std::numeric_limits<UCharT1>::max() + 1) / 64> m_val;

	CharSet()
	    : m_val{}
	{
	}

	void insert(CharT1 ch)
	{
		m_val[UCharT1(ch) / 64u] |= 1ull << (UCharT1(ch) % 64u);
	}

	template <typename CharT2>
	bool find(CharT2 ch) const
	{
		if (!CanTypeFitValue<CharT1>(ch))
			return false;

		return !!(m_val[UCharT1(ch) / 64u] & (1ull << (UCharT1(ch) % 64u)));
	}
};

template <typename CharT1, size_t size>
struct CharSet
{
	std::unordered_set<CharT1> m_val;

	CharSet()
	    : m_val{}
	{
	}

	void insert(CharT1 ch)
	{
		m_val.insert(ch);
	}

	template <typename CharT2>
	bool find(CharT2 ch) const
	{
		if (!CanTypeFitValue<CharT1>(ch))
			return false;

		return m_val.find(CharT1(ch)) != m_val.end();
	}
};
} // namespace rapidfuzz::detail

namespace rapidfuzz
{
template <typename T>
struct ScoreAlignment
{
	T score;           /**< resulting score of the algorithm */
	size_t src_start;  /**< index into the source string */
	size_t src_end;    /**< index into the source string */
	size_t dest_start; /**< index into the destination string */
	size_t dest_end;   /**< index into the destination string */

	ScoreAlignment()
	    : score(T()), src_start(0), src_end(0), dest_start(0), dest_end(0)
	{
	}

	ScoreAlignment(T score_, size_t src_start_, size_t src_end_, size_t dest_start_, size_t dest_end_)
	    : score(score_),
	      src_start(src_start_),
	      src_end(src_end_),
	      dest_start(dest_start_),
	      dest_end(dest_end_)
	{
	}
};

template <typename T>
inline bool operator==(const ScoreAlignment<T>& a, const ScoreAlignment<T>& b)
{
	return (a.score == b.score) && (a.src_start == b.src_start) && (a.src_end == b.src_end) &&
	       (a.dest_start == b.dest_start) && (a.dest_end == b.dest_end);
}
}

namespace rapidfuzz::detail
{

template <typename T, bool IsConst>
struct BitMatrixView
{

	using value_type = T;
	using size_type = size_t;
	using pointer = std::conditional_t<IsConst, const value_type*, value_type*>;
	using reference = std::conditional_t<IsConst, const value_type&, value_type&>;

	BitMatrixView(pointer vector, size_type cols) noexcept
	    : m_vector(vector), m_cols(cols)
	{
	}

	reference operator[](size_type col) noexcept
	{
		assert(col < m_cols);
		return m_vector[col];
	}

	size_type size() const noexcept
	{
		return m_cols;
	}

  private:
	pointer m_vector;
	size_type m_cols;
};

template <typename T>
struct BitMatrix
{

	using value_type = T;

	BitMatrix()
	    : m_rows(0), m_cols(0), m_matrix(nullptr)
	{
	}

	BitMatrix(size_t rows, size_t cols, T val)
	    : m_rows(rows), m_cols(cols), m_matrix(nullptr)
	{
		if (m_rows && m_cols)
			m_matrix = lt::allocator<T>::allocate(m_rows * m_cols);
		std::fill_n(m_matrix, m_rows * m_cols, val);
	}

	BitMatrix(const BitMatrix& other)
	    : m_rows(other.m_rows), m_cols(other.m_cols), m_matrix(nullptr)
	{
		if (m_rows && m_cols)
			m_matrix = lt::allocator<T>::allocate(m_rows * m_cols);
		std::copy(other.m_matrix, other.m_matrix + m_rows * m_cols, m_matrix);
	}

	BitMatrix(BitMatrix&& other) noexcept
	    : m_rows(0), m_cols(0), m_matrix(nullptr)
	{
		other.swap(*this);
	}

	BitMatrix& operator=(BitMatrix&& other) noexcept
	{
		other.swap(*this);
		return *this;
	}

	BitMatrix& operator=(const BitMatrix& other)
	{
		BitMatrix temp = other;
		temp.swap(*this);
		return *this;
	}

	void swap(BitMatrix& rhs) noexcept
	{
		using std::swap;
		swap(m_rows, rhs.m_rows);
		swap(m_cols, rhs.m_cols);
		swap(m_matrix, rhs.m_matrix);
	}

	~BitMatrix()
	{
		lt::allocator<T>::deallocate(m_matrix);
	}

	BitMatrixView<value_type, false> operator[](size_t row) noexcept
	{
		assert(row < m_rows);
		return {&m_matrix[row * m_cols], m_cols};
	}

	BitMatrixView<value_type, true> operator[](size_t row) const noexcept
	{
		assert(row < m_rows);
		return {&m_matrix[row * m_cols], m_cols};
	}

	size_t rows() const noexcept
	{
		return m_rows;
	}

	size_t cols() const noexcept
	{
		return m_cols;
	}

  private:
	size_t m_rows;
	size_t m_cols;
	T* m_matrix;
};

template <typename T>
struct ShiftedBitMatrix
{
	using value_type = T;

	ShiftedBitMatrix()
	{
	}

	ShiftedBitMatrix(size_t rows, size_t cols, T val)
	    : m_matrix(rows, cols, val), m_offsets(rows)
	{
	}

	ShiftedBitMatrix(const ShiftedBitMatrix& other)
	    : m_matrix(other.m_matrix), m_offsets(other.m_offsets)
	{
	}

	ShiftedBitMatrix(ShiftedBitMatrix&& other) noexcept
	{
		other.swap(*this);
	}

	ShiftedBitMatrix& operator=(ShiftedBitMatrix&& other) noexcept
	{
		other.swap(*this);
		return *this;
	}

	ShiftedBitMatrix& operator=(const ShiftedBitMatrix& other)
	{
		ShiftedBitMatrix temp = other;
		temp.swap(*this);
		return *this;
	}

	void swap(ShiftedBitMatrix& rhs) noexcept
	{
		using std::swap;
		swap(m_matrix, rhs.m_matrix);
		swap(m_offsets, rhs.m_offsets);
	}

	bool test_bit(size_t row, size_t col, bool default_ = false) const noexcept
	{
		ptrdiff_t offset = static_cast<ptrdiff_t>(m_offsets[row]);

		if (offset < 0)
		{
			col += static_cast<size_t>(-offset);
		}
		else if (col >= static_cast<size_t>(offset))
		{
			col -= static_cast<size_t>(offset);
		}
		/* bit on the left of the band */
		else
		{
			return default_;
		}

		size_t word_size = sizeof(value_type) * 8;
		size_t col_word = col / word_size;
		uint64_t col_mask = value_type(1) << (col % word_size);

		return bool(m_matrix[row][col_word] & col_mask);
	}

	auto operator[](size_t row) noexcept
	{
		return m_matrix[row];
	}

	auto operator[](size_t row) const noexcept
	{
		return m_matrix[row];
	}

	void set_offset(size_t row, ptrdiff_t offset)
	{
		m_offsets[row] = offset;
	}

  private:
	BitMatrix<value_type> m_matrix;
	ltvector<ptrdiff_t> m_offsets;
};

} // namespace rapidfuzz::detail

namespace rapidfuzz::detail
{

template <typename T>
T bit_mask_lsb(int n)
{
	T mask = static_cast<T>(-1);
	if (n < static_cast<int>(sizeof(T) * 8))
	{
		mask += static_cast<T>(1) << n;
	}
	return mask;
}

template <typename T>
bool bittest(T a, int bit)
{
	return (a >> bit) & 1;
}

/*
 * shift right without undefined behavior for shifts > bit width
 */
template <typename U>
constexpr uint64_t shr64(uint64_t a, U shift)
{
	return (shift < 64) ? a >> shift : 0;
}

/*
 * shift left without undefined behavior for shifts > bit width
 */
template <typename U>
constexpr uint64_t shl64(uint64_t a, U shift)
{
	return (shift < 64) ? a << shift : 0;
}

constexpr uint64_t addc64(uint64_t a, uint64_t b, uint64_t carryin, uint64_t* carryout)
{
	/* todo should use _addcarry_u64 when available */
	a += carryin;
	*carryout = a < carryin;
	a += b;
	*carryout |= a < b;
	return a;
}

template <typename T, typename U>
constexpr T ceil_div(T a, U divisor)
{
	T _div = static_cast<T>(divisor);
	return a / _div + static_cast<T>(a % _div != 0);
}

static inline int popcount(uint64_t x)
{
	return static_cast<int>(std::bitset<64>(x).count());
}

static inline int popcount(uint32_t x)
{
	return static_cast<int>(std::bitset<32>(x).count());
}

static inline int popcount(uint16_t x)
{
	return static_cast<int>(std::bitset<16>(x).count());
}

static inline int popcount(uint8_t x)
{
	static constexpr int bit_count[256] = {
	    0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
	    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
	    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
	    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
	    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
	    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
	    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
	    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8};
	return bit_count[x];
}

template <typename T>
constexpr T rotl(T x, unsigned int n)
{
	unsigned int num_bits = std::numeric_limits<T>::digits;
	assert(n < num_bits);
	unsigned int count_mask = num_bits - 1;

#if _MSC_VER && !defined(__clang__)
#pragma warning(push)
/* unary minus operator applied to unsigned type, result still unsigned */
#pragma warning(disable : 4146)
#endif
	return (x << n) | (x >> (-n & count_mask));
#if _MSC_VER && !defined(__clang__)
#pragma warning(pop)
#endif
}

/**
 * Extract the lowest set bit from a. If no bits are set in a returns 0.
 */
template <typename T>
constexpr T blsi(T a)
{
#if _MSC_VER && !defined(__clang__)
#pragma warning(push)
/* unary minus operator applied to unsigned type, result still unsigned */
#pragma warning(disable : 4146)
#endif
	return a & -a;
#if _MSC_VER && !defined(__clang__)
#pragma warning(pop)
#endif
}

/**
 * Clear the lowest set bit in a.
 */
template <typename T>
constexpr T blsr(T x)
{
	return x & (x - 1);
}

/**
 * Sets all the lower bits of the result to 1 up to and including lowest set bit (=1) in a.
 * If a is zero, blsmsk sets all bits to 1.
 */
template <typename T>
constexpr T blsmsk(T a)
{
	return a ^ (a - 1);
}

#if defined(_MSC_VER) && !defined(__clang__)
static inline int countr_zero(uint32_t x)
{
	unsigned long trailing_zero = 0;
	_BitScanForward(&trailing_zero, x);
	return (int)trailing_zero;
}

#if defined(_M_ARM) || defined(_M_X64)
static inline int countr_zero(uint64_t x)
{
	unsigned long trailing_zero = 0;
	_BitScanForward64(&trailing_zero, x);
	return (int)trailing_zero;
}
#else
static inline int countr_zero(uint64_t x)
{
	uint32_t msh = (uint32_t)(x >> 32);
	uint32_t lsh = (uint32_t)(x & 0xFFFFFFFF);
	if (lsh != 0)
		return countr_zero(lsh);
	return 32 + countr_zero(msh);
}
#endif

#else /*  gcc / clang */
static inline int countr_zero(uint32_t x)
{
	return __builtin_ctz(x);
}

static inline int countr_zero(uint64_t x)
{
	return __builtin_ctzll(x);
}
#endif

template <class T, T... inds, class F>
constexpr void unroll_impl(std::integer_sequence<T, inds...>, F&& f)
{
	(f(std::integral_constant<T, inds>{}), ...);
}

template <class T, T count, class F>
constexpr void unroll(F&& f)
{
	unroll_impl(std::make_integer_sequence<T, count>{}, std::forward<F>(f));
}

} // namespace rapidfuzz::detail

namespace rapidfuzz::detail
{

struct BitvectorHashmap
{
	BitvectorHashmap()
	    : m_map()
	{
	}

	template <typename CharT>
	uint64_t get(CharT key) const noexcept
	{
		return m_map[lookup(static_cast<uint64_t>(key))].value;
	}

	template <typename CharT>
	uint64_t& operator[](CharT key) noexcept
	{
		uint32_t i = lookup(static_cast<uint64_t>(key));
		m_map[i].key = static_cast<uint64_t>(key);
		return m_map[i].value;
	}

  private:
	/**
	 * lookup key inside the hashmap using a similar collision resolution
	 * strategy to CPython and Ruby
	 */
	uint32_t lookup(uint64_t key) const noexcept
	{
		uint32_t i = key % 128;

		if (!m_map[i].value || m_map[i].key == key)
			return i;

		uint64_t perturb = key;
		while (true)
		{
			i = (static_cast<uint64_t>(i) * 5 + perturb + 1) % 128;
			if (!m_map[i].value || m_map[i].key == key)
				return i;

			perturb >>= 5;
		}
	}

	struct MapElem
	{
		uint64_t key = 0;
		uint64_t value = 0;
	};
	std::array<MapElem, 128> m_map;
};

struct PatternMatchVector
{
	PatternMatchVector()
	    : m_extendedAscii()
	{
	}

	template <typename InputIt>
	PatternMatchVector(Range<InputIt> s)
	    : m_extendedAscii()
	{
		insert(s);
	}

	size_t size() const noexcept
	{
		return 1;
	}

	template <typename InputIt>
	void insert(Range<InputIt> s) noexcept
	{
		uint64_t mask = 1;
		for (const auto& ch : s)
		{
			insert_mask(ch, mask);
			mask <<= 1;
		}
	}

	template <typename CharT>
	void insert(CharT key, int64_t pos) noexcept
	{
		insert_mask(key, UINT64_C(1) << pos);
	}

	uint64_t get(char key) const noexcept
	{
		/** treat char as value between 0 and 127 for performance reasons */
		return m_extendedAscii[static_cast<uint8_t>(key)];
	}

	template <typename CharT>
	uint64_t get(CharT key) const noexcept
	{
		if (key >= 0 && key <= 255)
			return m_extendedAscii[static_cast<uint8_t>(key)];
		else
			return m_map.get(key);
	}

	template <typename CharT>
	uint64_t get(size_t block, CharT key) const noexcept
	{
		assert(block == 0);
		(void)block;
		return get(key);
	}

	void insert_mask(char key, uint64_t mask) noexcept
	{
		/** treat char as value between 0 and 127 for performance reasons */
		m_extendedAscii[static_cast<uint8_t>(key)] |= mask;
	}

	template <typename CharT>
	void insert_mask(CharT key, uint64_t mask) noexcept
	{
		if (key >= 0 && key <= 255)
			m_extendedAscii[static_cast<uint8_t>(key)] |= mask;
		else
			m_map[key] |= mask;
	}

  private:
	BitvectorHashmap m_map;
	std::array<uint64_t, 256> m_extendedAscii;
};

struct BlockPatternMatchVector
{
	BlockPatternMatchVector() = delete;

	BlockPatternMatchVector(size_t str_len)
	    : m_block_count(ceil_div(str_len, 64)), m_extendedAscii(256, m_block_count, 0)
	{
	}

	template <typename InputIt>
	BlockPatternMatchVector(Range<InputIt> s)
	    : BlockPatternMatchVector(static_cast<size_t>(s.size()))
	{
		insert(s);
	}

	~BlockPatternMatchVector()
	{
	}

	size_t size() const noexcept
	{
		return m_block_count;
	}

	template <typename CharT>
	void insert(size_t block, CharT ch, int pos) noexcept
	{
		uint64_t mask = UINT64_C(1) << pos;
		insert_mask(block, ch, mask);
	}

	/**
	 * @warning undefined behavior if iterator \p first is greater than \p last
	 * @tparam InputIt
	 * @param first
	 * @param last
	 */
	template <typename InputIt>
	void insert(Range<InputIt> s) noexcept
	{
		auto len = s.size();
		uint64_t mask = 1;
		for (ptrdiff_t i = 0; i < len; ++i)
		{
			size_t block = static_cast<size_t>(i) / 64;
			insert_mask(block, s[i], mask);
			mask = rotl(mask, 1);
		}
	}

	template <typename CharT>
	void insert_mask(size_t block, CharT key, uint64_t mask) noexcept
	{
		assert(block < size());
		if (key >= 0 && key <= 255)
			m_extendedAscii[static_cast<uint8_t>(key)][block] |= mask;
		else
		{
			if (m_map.empty())
				m_map.resize(m_block_count);
			m_map[block][key] |= mask;
		}
	}

	void insert_mask(size_t block, char key, uint64_t mask) noexcept
	{
		insert_mask(block, static_cast<uint8_t>(key), mask);
	}

	template <typename CharT>
	uint64_t get(size_t block, CharT key) const noexcept
	{
		if (key >= 0 && key <= 255)
			return m_extendedAscii[static_cast<uint8_t>(key)][block];
		else if (!m_map.empty())
			return m_map[block].get(key);
		else
			return 0;
	}

	uint64_t get(size_t block, char ch) const noexcept
	{
		return get(block, static_cast<uint8_t>(ch));
	}

  private:
	size_t m_block_count;
	ltvector<BitvectorHashmap> m_map;
	BitMatrix<uint64_t> m_extendedAscii;
};

} // namespace rapidfuzz::detail

namespace rapidfuzz
{

struct StringAffix
{
	size_t prefix_len;
	size_t suffix_len;
};

struct LevenshteinWeightTable
{
	int64_t insert_cost;
	int64_t delete_cost;
	int64_t replace_cost;
};

/**
 * @brief Edit operation types used by the Levenshtein distance
 */
enum class EditType
{
	None = 0,    /**< No Operation required */
	Replace = 1, /**< Replace a character if a string by another character */
	Insert = 2,  /**< Insert a character into a string */
	Delete = 3   /**< Delete a character from a string */
};

/**
 * @brief Edit operations used by the Levenshtein distance
 *
 * This represents an edit operation of type type which is applied to
 * the source string
 *
 * Replace: replace character at src_pos with character at dest_pos
 * Insert:  insert character from dest_pos at src_pos
 * Delete:  delete character at src_pos
 */
struct EditOp
{
	EditType type;   /**< type of the edit operation */
	size_t src_pos;  /**< index into the source string */
	size_t dest_pos; /**< index into the destination string */

	EditOp()
	    : type(EditType::None), src_pos(0), dest_pos(0)
	{
	}

	EditOp(EditType type_, size_t src_pos_, size_t dest_pos_)
	    : type(type_), src_pos(src_pos_), dest_pos(dest_pos_)
	{
	}
};

inline bool operator==(EditOp a, EditOp b)
{
	return (a.type == b.type) && (a.src_pos == b.src_pos) && (a.dest_pos == b.dest_pos);
}

inline bool operator!=(EditOp a, EditOp b)
{
	return !(a == b);
}

/**
 * @brief Edit operations used by the Levenshtein distance
 *
 * This represents an edit operation of type type which is applied to
 * the source string
 *
 * None:    s1[src_begin:src_end] == s1[dest_begin:dest_end]
 * Replace: s1[i1:i2] should be replaced by s2[dest_begin:dest_end]
 * Insert:  s2[dest_begin:dest_end] should be inserted at s1[src_begin:src_begin].
 *          Note that src_begin==src_end in this case.
 * Delete:  s1[src_begin:src_end] should be deleted.
 *          Note that dest_begin==dest_end in this case.
 */
struct Opcode
{
	EditType type;     /**< type of the edit operation */
	size_t src_begin;  /**< index into the source string */
	size_t src_end;    /**< index into the source string */
	size_t dest_begin; /**< index into the destination string */
	size_t dest_end;   /**< index into the destination string */

	Opcode()
	    : type(EditType::None), src_begin(0), src_end(0), dest_begin(0), dest_end(0)
	{
	}

	Opcode(EditType type_, size_t src_begin_, size_t src_end_, size_t dest_begin_, size_t dest_end_)
	    : type(type_), src_begin(src_begin_), src_end(src_end_), dest_begin(dest_begin_), dest_end(dest_end_)
	{
	}
};

inline bool operator==(Opcode a, Opcode b)
{
	return (a.type == b.type) && (a.src_begin == b.src_begin) && (a.src_end == b.src_end) &&
	       (a.dest_begin == b.dest_begin) && (a.dest_end == b.dest_end);
}

inline bool operator!=(Opcode a, Opcode b)
{
	return !(a == b);
}

namespace detail
{
template <typename Vec>
auto vector_slice(const Vec& vec, int start, int stop, int step) -> Vec
{
	Vec new_vec;

	if (step == 0)
		throw std::invalid_argument("slice step cannot be zero");
	if (step < 0)
		throw std::invalid_argument("step sizes below 0 lead to an invalid order of editops");

	if (start < 0)
		start = std::max<int>(start + static_cast<int>(vec.size()), 0);
	else if (start > static_cast<int>(vec.size()))
		start = static_cast<int>(vec.size());

	if (stop < 0)
		stop = std::max<int>(stop + static_cast<int>(vec.size()), 0);
	else if (stop > static_cast<int>(vec.size()))
		stop = static_cast<int>(vec.size());

	if (start >= stop)
		return new_vec;

	int count = (stop - 1 - start) / step + 1;
	new_vec.reserve(static_cast<size_t>(count));

	for (int i = start; i < stop; i += step)
		new_vec.push_back(vec[static_cast<size_t>(i)]);

	return new_vec;
}

template <typename Vec>
void vector_remove_slice(Vec& vec, int start, int stop, int step)
{
	if (step == 0)
		throw std::invalid_argument("slice step cannot be zero");
	if (step < 0)
		throw std::invalid_argument("step sizes below 0 lead to an invalid order of editops");

	if (start < 0)
		start = std::max<int>(start + static_cast<int>(vec.size()), 0);
	else if (start > static_cast<int>(vec.size()))
		start = static_cast<int>(vec.size());

	if (stop < 0)
		stop = std::max<int>(stop + static_cast<int>(vec.size()), 0);
	else if (stop > static_cast<int>(vec.size()))
		stop = static_cast<int>(vec.size());

	if (start >= stop)
		return;

	auto iter = vec.begin() + start;
	for (int i = start; i < static_cast<int>(vec.size()); i++)
		if (i >= stop || ((i - start) % step != 0))
			*(iter++) = vec[static_cast<size_t>(i)];

	vec.resize(static_cast<size_t>(std::distance(vec.begin(), iter)));
	vec.shrink_to_fit();
}

} // namespace detail

class Opcodes;

class Editops : private ltvector<EditOp>
{
  public:
	using ltvector<EditOp>::size_type;

	Editops() noexcept
	    : src_len(0), dest_len(0)
	{
	}

	Editops(size_type count, const EditOp& value)
	    : ltvector<EditOp>(count, value), src_len(0), dest_len(0)
	{
	}

	explicit Editops(size_type count)
	    : ltvector<EditOp>(count), src_len(0), dest_len(0)
	{
	}

	Editops(const Editops& other)
	    : ltvector<EditOp>(other), src_len(other.src_len), dest_len(other.dest_len)
	{
	}

	Editops(const Opcodes& other);

	Editops(Editops&& other) noexcept
	{
		swap(other);
	}

	Editops& operator=(Editops other) noexcept
	{
		swap(other);
		return *this;
	}

	/* Element access */
	using ltvector<EditOp>::at;
	using ltvector<EditOp>::operator[];
	using ltvector<EditOp>::front;
	using ltvector<EditOp>::back;
	using ltvector<EditOp>::data;

	/* Iterators */
	using ltvector<EditOp>::begin;
	using ltvector<EditOp>::cbegin;
	using ltvector<EditOp>::end;
	using ltvector<EditOp>::cend;
	using ltvector<EditOp>::rbegin;
	using ltvector<EditOp>::crbegin;
	using ltvector<EditOp>::rend;
	using ltvector<EditOp>::crend;

	/* Capacity */
	using ltvector<EditOp>::empty;
	using ltvector<EditOp>::size;
	using ltvector<EditOp>::max_size;
	using ltvector<EditOp>::reserve;
	using ltvector<EditOp>::capacity;
	using ltvector<EditOp>::shrink_to_fit;

	/* Modifiers */
	using ltvector<EditOp>::clear;
	using ltvector<EditOp>::insert;
	using ltvector<EditOp>::emplace;
	using ltvector<EditOp>::erase;
	using ltvector<EditOp>::push_back;
	using ltvector<EditOp>::emplace_back;
	using ltvector<EditOp>::pop_back;
	using ltvector<EditOp>::resize;

	void swap(Editops& rhs) noexcept
	{
		std::swap(src_len, rhs.src_len);
		std::swap(dest_len, rhs.dest_len);
		ltvector<EditOp>::swap(rhs);
	}

	Editops slice(int start, int stop, int step = 1) const
	{
		Editops ed_slice = detail::vector_slice(*this, start, stop, step);
		ed_slice.src_len = src_len;
		ed_slice.dest_len = dest_len;
		return ed_slice;
	}

	void remove_slice(int start, int stop, int step = 1)
	{
		detail::vector_remove_slice(*this, start, stop, step);
	}

	Editops reverse() const
	{
		Editops reversed = *this;
		std::reverse(reversed.begin(), reversed.end());
		return reversed;
	}

	size_t get_src_len() const noexcept
	{
		return src_len;
	}
	void set_src_len(size_t len) noexcept
	{
		src_len = len;
	}
	size_t get_dest_len() const noexcept
	{
		return dest_len;
	}
	void set_dest_len(size_t len) noexcept
	{
		dest_len = len;
	}

	Editops inverse() const
	{
		Editops inv_ops = *this;
		std::swap(inv_ops.src_len, inv_ops.dest_len);
		for (auto& op : inv_ops)
		{
			std::swap(op.src_pos, op.dest_pos);
			if (op.type == EditType::Delete)
				op.type = EditType::Insert;
			else if (op.type == EditType::Insert)
				op.type = EditType::Delete;
		}
		return inv_ops;
	}

	Editops remove_subsequence(const Editops& subsequence) const
	{
		Editops result;
		result.set_src_len(src_len);
		result.set_dest_len(dest_len);

		if (subsequence.size() > size())
			throw std::invalid_argument("subsequence is not a subsequence");

		result.resize(size() - subsequence.size());

		/* offset to correct removed edit operations */
		int offset = 0;
		auto op_iter = begin();
		auto op_end = end();
		size_t result_pos = 0;
		for (const auto& sop : subsequence)
		{
			for (; op_iter != op_end && sop != *op_iter; op_iter++)
			{
				result[result_pos] = *op_iter;
				result[result_pos].src_pos =
				    static_cast<size_t>(static_cast<ptrdiff_t>(result[result_pos].src_pos) + offset);
				result_pos++;
			}
			/* element of subsequence not part of the sequence */
			if (op_iter == op_end)
				throw std::invalid_argument("subsequence is not a subsequence");

			if (sop.type == EditType::Insert)
				offset++;
			else if (sop.type == EditType::Delete)
				offset--;
			op_iter++;
		}

		/* add remaining elements */
		for (; op_iter != op_end; op_iter++)
		{
			result[result_pos] = *op_iter;
			result[result_pos].src_pos =
			    static_cast<size_t>(static_cast<ptrdiff_t>(result[result_pos].src_pos) + offset);
			result_pos++;
		}

		return result;
	}

  private:
	size_t src_len;
	size_t dest_len;
};

inline bool operator==(const Editops& lhs, const Editops& rhs)
{
	if (lhs.get_src_len() != rhs.get_src_len() || lhs.get_dest_len() != rhs.get_dest_len())
	{
		return false;
	}

	if (lhs.size() != rhs.size())
	{
		return false;
	}
	return std::equal(lhs.begin(), lhs.end(), rhs.begin());
}

inline bool operator!=(const Editops& lhs, const Editops& rhs)
{
	return !(lhs == rhs);
}

inline void swap(Editops& lhs, Editops& rhs) noexcept(noexcept(lhs.swap(rhs)))
{
	lhs.swap(rhs);
}

class Opcodes : private ltvector<Opcode>
{
  public:
	using ltvector<Opcode>::size_type;

	Opcodes() noexcept
	    : src_len(0), dest_len(0)
	{
	}

	Opcodes(size_type count, const Opcode& value)
	    : ltvector<Opcode>(count, value), src_len(0), dest_len(0)
	{
	}

	explicit Opcodes(size_type count)
	    : ltvector<Opcode>(count), src_len(0), dest_len(0)
	{
	}

	Opcodes(const Opcodes& other)
	    : ltvector<Opcode>(other), src_len(other.src_len), dest_len(other.dest_len)
	{
	}

	Opcodes(const Editops& other);

	Opcodes(Opcodes&& other) noexcept
	{
		swap(other);
	}

	Opcodes& operator=(Opcodes other) noexcept
	{
		swap(other);
		return *this;
	}

	/* Element access */
	using ltvector<Opcode>::at;
	using ltvector<Opcode>::operator[];
	using ltvector<Opcode>::front;
	using ltvector<Opcode>::back;
	using ltvector<Opcode>::data;

	/* Iterators */
	using ltvector<Opcode>::begin;
	using ltvector<Opcode>::cbegin;
	using ltvector<Opcode>::end;
	using ltvector<Opcode>::cend;
	using ltvector<Opcode>::rbegin;
	using ltvector<Opcode>::crbegin;
	using ltvector<Opcode>::rend;
	using ltvector<Opcode>::crend;

	/* Capacity */
	using ltvector<Opcode>::empty;
	using ltvector<Opcode>::size;
	using ltvector<Opcode>::max_size;
	using ltvector<Opcode>::reserve;
	using ltvector<Opcode>::capacity;
	using ltvector<Opcode>::shrink_to_fit;

	/* Modifiers */
	using ltvector<Opcode>::clear;
	using ltvector<Opcode>::insert;
	using ltvector<Opcode>::emplace;
	using ltvector<Opcode>::erase;
	using ltvector<Opcode>::push_back;
	using ltvector<Opcode>::emplace_back;
	using ltvector<Opcode>::pop_back;
	using ltvector<Opcode>::resize;

	void swap(Opcodes& rhs) noexcept
	{
		std::swap(src_len, rhs.src_len);
		std::swap(dest_len, rhs.dest_len);
		ltvector<Opcode>::swap(rhs);
	}

	Opcodes slice(int start, int stop, int step = 1) const
	{
		Opcodes ed_slice = detail::vector_slice(*this, start, stop, step);
		ed_slice.src_len = src_len;
		ed_slice.dest_len = dest_len;
		return ed_slice;
	}

	Opcodes reverse() const
	{
		Opcodes reversed = *this;
		std::reverse(reversed.begin(), reversed.end());
		return reversed;
	}

	size_t get_src_len() const noexcept
	{
		return src_len;
	}
	void set_src_len(size_t len) noexcept
	{
		src_len = len;
	}
	size_t get_dest_len() const noexcept
	{
		return dest_len;
	}
	void set_dest_len(size_t len) noexcept
	{
		dest_len = len;
	}

	Opcodes inverse() const
	{
		Opcodes inv_ops = *this;
		std::swap(inv_ops.src_len, inv_ops.dest_len);
		for (auto& op : inv_ops)
		{
			std::swap(op.src_begin, op.dest_begin);
			std::swap(op.src_end, op.dest_end);
			if (op.type == EditType::Delete)
				op.type = EditType::Insert;
			else if (op.type == EditType::Insert)
				op.type = EditType::Delete;
		}
		return inv_ops;
	}

  private:
	size_t src_len;
	size_t dest_len;
};

inline bool operator==(const Opcodes& lhs, const Opcodes& rhs)
{
	if (lhs.get_src_len() != rhs.get_src_len() || lhs.get_dest_len() != rhs.get_dest_len())
		return false;

	if (lhs.size() != rhs.size())
		return false;

	return std::equal(lhs.begin(), lhs.end(), rhs.begin());
}

inline bool operator!=(const Opcodes& lhs, const Opcodes& rhs)
{
	return !(lhs == rhs);
}

inline void swap(Opcodes& lhs, Opcodes& rhs) noexcept(noexcept(lhs.swap(rhs)))
{
	lhs.swap(rhs);
}

inline Editops::Editops(const Opcodes& other)
{
	src_len = other.get_src_len();
	dest_len = other.get_dest_len();
	for (const auto& op : other)
	{
		switch (op.type)
		{
		case EditType::None:
			break;

		case EditType::Replace:
			for (size_t j = 0; j < op.src_end - op.src_begin; j++)
				push_back({EditType::Replace, op.src_begin + j, op.dest_begin + j});
			break;

		case EditType::Insert:
			for (size_t j = 0; j < op.dest_end - op.dest_begin; j++)
				push_back({EditType::Insert, op.src_begin, op.dest_begin + j});
			break;

		case EditType::Delete:
			for (size_t j = 0; j < op.src_end - op.src_begin; j++)
				push_back({EditType::Delete, op.src_begin + j, op.dest_begin});
			break;
		}
	}
}

inline Opcodes::Opcodes(const Editops& other)
{
	src_len = other.get_src_len();
	dest_len = other.get_dest_len();
	size_t src_pos = 0;
	size_t dest_pos = 0;
	for (size_t i = 0; i < other.size();)
	{
		if (src_pos < other[i].src_pos || dest_pos < other[i].dest_pos)
		{
			push_back({EditType::None, src_pos, other[i].src_pos, dest_pos, other[i].dest_pos});
			src_pos = other[i].src_pos;
			dest_pos = other[i].dest_pos;
		}

		size_t src_begin = src_pos;
		size_t dest_begin = dest_pos;
		EditType type = other[i].type;
		do
		{
			switch (type)
			{
			case EditType::None:
				break;

			case EditType::Replace:
				src_pos++;
				dest_pos++;
				break;

			case EditType::Insert:
				dest_pos++;
				break;

			case EditType::Delete:
				src_pos++;
				break;
			}
			i++;
		} while (i < other.size() && other[i].type == type && src_pos == other[i].src_pos &&
		         dest_pos == other[i].dest_pos);

		push_back({type, src_begin, src_pos, dest_begin, dest_pos});
	}

	if (src_pos < other.get_src_len() || dest_pos < other.get_dest_len())
	{
		push_back({EditType::None, src_pos, other.get_src_len(), dest_pos, other.get_dest_len()});
	}
}
} // namespace rapidfuzz

namespace rapidfuzz::detail
{
static inline double NormSim_to_NormDist(double score_cutoff, double imprecision = 0.00001)
{
	return std::min(1.0, 1.0 - score_cutoff + imprecision);
}

template <typename T, typename... Args>
struct NormalizedMetricBase
{
	template <typename InputIt1, typename InputIt2,
	          typename = std::enable_if_t<!std::is_same_v<InputIt2, double>>>
	static double normalized_distance(InputIt1 first1, InputIt1 last1, InputIt2 first2, InputIt2 last2,
	                                  Args... args, double score_cutoff, double score_hint)
	{
		return _normalized_distance(Range(first1, last1), Range(first2, last2), std::forward<Args>(args)...,
		                            score_cutoff, score_hint);
	}

	template <typename Sentence1, typename Sentence2>
	static double normalized_distance(const Sentence1& s1, const Sentence2& s2, Args... args,
	                                  double score_cutoff, double score_hint)
	{
		return _normalized_distance(Range(s1), Range(s2), std::forward<Args>(args)..., score_cutoff,
		                            score_hint);
	}

	template <typename InputIt1, typename InputIt2,
	          typename = std::enable_if_t<!std::is_same_v<InputIt2, double>>>
	static double normalized_similarity(InputIt1 first1, InputIt1 last1, InputIt2 first2, InputIt2 last2,
	                                    Args... args, double score_cutoff, double score_hint)
	{
		return _normalized_similarity(Range(first1, last1), Range(first2, last2), std::forward<Args>(args)...,
		                              score_cutoff, score_hint);
	}

	template <typename Sentence1, typename Sentence2>
	static double normalized_similarity(const Sentence1& s1, const Sentence2& s2, Args... args,
	                                    double score_cutoff, double score_hint)
	{
		return _normalized_similarity(Range(s1), Range(s2), std::forward<Args>(args)..., score_cutoff,
		                              score_hint);
	}

  protected:
	template <typename InputIt1, typename InputIt2>
	static double _normalized_distance(Range<InputIt1> s1, Range<InputIt2> s2, Args... args,
	                                   double score_cutoff, double score_hint)
	{
		auto maximum = T::maximum(s1, s2, args...);
		auto cutoff_distance =
		    static_cast<decltype(maximum)>(std::ceil(static_cast<double>(maximum) * score_cutoff));
		auto hint_distance =
		    static_cast<decltype(maximum)>(std::ceil(static_cast<double>(maximum) * score_hint));
		auto dist = T::_distance(s1, s2, std::forward<Args>(args)..., cutoff_distance, hint_distance);
		double norm_dist = (maximum != 0) ? static_cast<double>(dist) / static_cast<double>(maximum) : 0.0;
		return (norm_dist <= score_cutoff) ? norm_dist : 1.0;
	}

	template <typename InputIt1, typename InputIt2>
	static double _normalized_similarity(Range<InputIt1> s1, Range<InputIt2> s2, Args... args,
	                                     double score_cutoff, double score_hint)
	{
		double cutoff_score = NormSim_to_NormDist(score_cutoff);
		double hint_score = NormSim_to_NormDist(score_hint);
		double norm_dist =
		    _normalized_distance(s1, s2, std::forward<Args>(args)..., cutoff_score, hint_score);
		double norm_sim = 1.0 - norm_dist;
		return (norm_sim >= score_cutoff) ? norm_sim : 0.0;
	}

	NormalizedMetricBase()
	{
	}
	friend T;
};

template <typename T, typename ResType, int64_t WorstSimilarity, int64_t WorstDistance, typename... Args>
struct DistanceBase : public NormalizedMetricBase<T, Args...>
{
	template <typename InputIt1, typename InputIt2,
	          typename = std::enable_if_t<!std::is_same_v<InputIt2, double>>>
	static ResType distance(InputIt1 first1, InputIt1 last1, InputIt2 first2, InputIt2 last2, Args... args,
	                        ResType score_cutoff, ResType score_hint)
	{
		return T::_distance(Range(first1, last1), Range(first2, last2), std::forward<Args>(args)...,
		                    score_cutoff, score_hint);
	}

	template <typename Sentence1, typename Sentence2>
	static ResType distance(const Sentence1& s1, const Sentence2& s2, Args... args, ResType score_cutoff,
	                        ResType score_hint)
	{
		return T::_distance(Range(s1), Range(s2), std::forward<Args>(args)..., score_cutoff, score_hint);
	}

	template <typename InputIt1, typename InputIt2,
	          typename = std::enable_if_t<!std::is_same_v<InputIt2, double>>>
	static ResType similarity(InputIt1 first1, InputIt1 last1, InputIt2 first2, InputIt2 last2, Args... args,
	                          ResType score_cutoff, ResType score_hint)
	{
		return _similarity(Range(first1, last1), Range(first2, last2), std::forward<Args>(args)...,
		                   score_cutoff, score_hint);
	}

	template <typename Sentence1, typename Sentence2>
	static ResType similarity(const Sentence1& s1, const Sentence2& s2, Args... args, ResType score_cutoff,
	                          ResType score_hint)
	{
		return _similarity(Range(s1), Range(s2), std::forward<Args>(args)..., score_cutoff, score_hint);
	}

  protected:
	template <typename InputIt1, typename InputIt2>
	static ResType _similarity(Range<InputIt1> s1, Range<InputIt2> s2, Args... args, ResType score_cutoff,
	                           ResType score_hint)
	{
		auto maximum = T::maximum(s1, s2, args...);
		if (score_cutoff > maximum)
			return 0;

		score_hint = std::min(score_cutoff, score_hint);
		ResType cutoff_distance = maximum - score_cutoff;
		ResType hint_distance = maximum - score_hint;
		ResType dist = T::_distance(s1, s2, std::forward<Args>(args)..., cutoff_distance, hint_distance);
		ResType sim = maximum - dist;
		return (sim >= score_cutoff) ? sim : 0;
	}

	DistanceBase()
	{
	}
	friend T;
};

template <typename T, typename ResType, int64_t WorstSimilarity, int64_t WorstDistance, typename... Args>
struct SimilarityBase : public NormalizedMetricBase<T, Args...>
{
	template <typename InputIt1, typename InputIt2,
	          typename = std::enable_if_t<!std::is_same_v<InputIt2, double>>>
	static ResType distance(InputIt1 first1, InputIt1 last1, InputIt2 first2, InputIt2 last2, Args... args,
	                        ResType score_cutoff, ResType score_hint)
	{
		return _distance(Range(first1, last1), Range(first2, last2), std::forward<Args>(args)...,
		                 score_cutoff, score_hint);
	}

	template <typename Sentence1, typename Sentence2>
	static ResType distance(const Sentence1& s1, const Sentence2& s2, Args... args, ResType score_cutoff,
	                        ResType score_hint)
	{
		return _distance(Range(s1), Range(s2), std::forward<Args>(args)..., score_cutoff, score_hint);
	}

	template <typename InputIt1, typename InputIt2,
	          typename = std::enable_if_t<!std::is_same_v<InputIt2, double>>>
	static ResType similarity(InputIt1 first1, InputIt1 last1, InputIt2 first2, InputIt2 last2, Args... args,
	                          ResType score_cutoff, ResType score_hint)
	{
		return T::_similarity(Range(first1, last1), Range(first2, last2), std::forward<Args>(args)...,
		                      score_cutoff, score_hint);
	}

	template <typename Sentence1, typename Sentence2>
	static ResType similarity(const Sentence1& s1, const Sentence2& s2, Args... args, ResType score_cutoff,
	                          ResType score_hint)
	{
		return T::_similarity(Range(s1), Range(s2), std::forward<Args>(args)..., score_cutoff, score_hint);
	}

  protected:
	template <typename InputIt1, typename InputIt2>
	static ResType _distance(Range<InputIt1> s1, Range<InputIt2> s2, Args... args, ResType score_cutoff,
	                         ResType score_hint)
	{
		auto maximum = T::maximum(s1, s2, args...);
		ResType cutoff_similarity =
		    (maximum >= score_cutoff) ? maximum - score_cutoff : static_cast<ResType>(WorstSimilarity);
		ResType hint_similarity =
		    (maximum >= score_hint) ? maximum - score_hint : static_cast<ResType>(WorstSimilarity);
		ResType sim = T::_similarity(s1, s2, std::forward<Args>(args)..., cutoff_similarity, hint_similarity);
		ResType dist = maximum - sim;

		if constexpr (std::is_floating_point_v<ResType>)
			return (dist <= score_cutoff) ? dist : 1.0;
		else
			return (dist <= score_cutoff) ? dist : score_cutoff + 1;
	}

	SimilarityBase()
	{
	}
	friend T;
};

template <typename T>
struct CachedNormalizedMetricBase
{
	template <typename InputIt2>
	double normalized_distance(InputIt2 first2, InputIt2 last2, double score_cutoff = 1.0,
	                           double score_hint = 1.0) const
	{
		return _normalized_distance(Range(first2, last2), score_cutoff, score_hint);
	}

	template <typename Sentence2>
	double normalized_distance(const Sentence2& s2, double score_cutoff = 1.0, double score_hint = 1.0) const
	{
		return _normalized_distance(Range(s2), score_cutoff, score_hint);
	}

	template <typename InputIt2>
	double normalized_similarity(InputIt2 first2, InputIt2 last2, double score_cutoff = 0.0,
	                             double score_hint = 0.0) const
	{
		return _normalized_similarity(Range(first2, last2), score_cutoff, score_hint);
	}

	template <typename Sentence2>
	double normalized_similarity(const Sentence2& s2, double score_cutoff = 0.0,
	                             double score_hint = 0.0) const
	{
		return _normalized_similarity(Range(s2), score_cutoff, score_hint);
	}

  protected:
	template <typename InputIt2>
	double _normalized_distance(Range<InputIt2> s2, double score_cutoff, double score_hint) const
	{
		const T& derived = static_cast<const T&>(*this);
		auto maximum = derived.maximum(s2);
		auto cutoff_distance =
		    static_cast<decltype(maximum)>(std::ceil(static_cast<double>(maximum) * score_cutoff));
		auto hint_distance =
		    static_cast<decltype(maximum)>(std::ceil(static_cast<double>(maximum) * score_hint));
		double dist = static_cast<double>(derived._distance(s2, cutoff_distance, hint_distance));
		double norm_dist = (maximum != 0) ? dist / static_cast<double>(maximum) : 0.0;
		return (norm_dist <= score_cutoff) ? norm_dist : 1.0;
	}

	template <typename InputIt2>
	double _normalized_similarity(Range<InputIt2> s2, double score_cutoff, double score_hint) const
	{
		double cutoff_score = NormSim_to_NormDist(score_cutoff);
		double hint_score = NormSim_to_NormDist(score_hint);
		double norm_dist = _normalized_distance(s2, cutoff_score, hint_score);
		double norm_sim = 1.0 - norm_dist;
		return (norm_sim >= score_cutoff) ? norm_sim : 0.0;
	}

	CachedNormalizedMetricBase()
	{
	}
	friend T;
};

template <typename T, typename ResType, int64_t WorstSimilarity, int64_t WorstDistance>
struct CachedDistanceBase : public CachedNormalizedMetricBase<T>
{
	template <typename InputIt2>
	ResType distance(InputIt2 first2, InputIt2 last2,
	                 ResType score_cutoff = static_cast<ResType>(WorstDistance),
	                 ResType score_hint = static_cast<ResType>(WorstDistance)) const
	{
		const T& derived = static_cast<const T&>(*this);
		return derived._distance(Range(first2, last2), score_cutoff, score_hint);
	}

	template <typename Sentence2>
	ResType distance(const Sentence2& s2, ResType score_cutoff = static_cast<ResType>(WorstDistance),
	                 ResType score_hint = static_cast<ResType>(WorstDistance)) const
	{
		const T& derived = static_cast<const T&>(*this);
		return derived._distance(Range(s2), score_cutoff, score_hint);
	}

	template <typename InputIt2>
	ResType similarity(InputIt2 first2, InputIt2 last2,
	                   ResType score_cutoff = static_cast<ResType>(WorstSimilarity),
	                   ResType score_hint = static_cast<ResType>(WorstSimilarity)) const
	{
		return _similarity(Range(first2, last2), score_cutoff, score_hint);
	}

	template <typename Sentence2>
	ResType similarity(const Sentence2& s2, ResType score_cutoff = static_cast<ResType>(WorstSimilarity),
	                   ResType score_hint = static_cast<ResType>(WorstSimilarity)) const
	{
		return _similarity(Range(s2), score_cutoff, score_hint);
	}

  protected:
	template <typename InputIt2>
	ResType _similarity(Range<InputIt2> s2, ResType score_cutoff, ResType score_hint) const
	{
		const T& derived = static_cast<const T&>(*this);
		ResType maximum = derived.maximum(s2);
		if (score_cutoff > maximum)
			return 0;

		score_hint = std::min(score_cutoff, score_hint);
		ResType cutoff_distance = maximum - score_cutoff;
		ResType hint_distance = maximum - score_hint;
		ResType dist = derived._distance(s2, cutoff_distance, hint_distance);
		ResType sim = maximum - dist;
		return (sim >= score_cutoff) ? sim : 0;
	}

	CachedDistanceBase()
	{
	}
	friend T;
};

template <typename T, typename ResType, int64_t WorstSimilarity, int64_t WorstDistance>
struct CachedSimilarityBase : public CachedNormalizedMetricBase<T>
{
	template <typename InputIt2>
	ResType distance(InputIt2 first2, InputIt2 last2,
	                 ResType score_cutoff = static_cast<ResType>(WorstDistance),
	                 ResType score_hint = static_cast<ResType>(WorstDistance)) const
	{
		return _distance(Range(first2, last2), score_cutoff, score_hint);
	}

	template <typename Sentence2>
	ResType distance(const Sentence2& s2, ResType score_cutoff = static_cast<ResType>(WorstDistance),
	                 ResType score_hint = static_cast<ResType>(WorstDistance)) const
	{
		return _distance(Range(s2), score_cutoff, score_hint);
	}

	template <typename InputIt2>
	ResType similarity(InputIt2 first2, InputIt2 last2,
	                   ResType score_cutoff = static_cast<ResType>(WorstSimilarity),
	                   ResType score_hint = static_cast<ResType>(WorstSimilarity)) const
	{
		const T& derived = static_cast<const T&>(*this);
		return derived._similarity(Range(first2, last2), score_cutoff, score_hint);
	}

	template <typename Sentence2>
	ResType similarity(const Sentence2& s2, ResType score_cutoff = static_cast<ResType>(WorstSimilarity),
	                   ResType score_hint = static_cast<ResType>(WorstSimilarity)) const
	{
		const T& derived = static_cast<const T&>(*this);
		return derived._similarity(Range(s2), score_cutoff, score_hint);
	}

  protected:
	template <typename InputIt2>
	ResType _distance(Range<InputIt2> s2, ResType score_cutoff, ResType score_hint) const
	{
		const T& derived = static_cast<const T&>(*this);
		ResType maximum = derived.maximum(s2);
		ResType cutoff_similarity = (maximum > score_cutoff) ? maximum - score_cutoff : 0;
		ResType hint_similarity = (maximum > score_hint) ? maximum - score_hint : 0;
		ResType sim = derived._similarity(s2, cutoff_similarity, hint_similarity);
		ResType dist = maximum - sim;

		if constexpr (std::is_floating_point_v<ResType>)
			return (dist <= score_cutoff) ? dist : 1.0;
		else
			return (dist <= score_cutoff) ? dist : score_cutoff + 1;
	}

	CachedSimilarityBase()
	{
	}
	friend T;
};

template <typename T>
struct MultiNormalizedMetricBase
{
	template <typename InputIt2>
	void normalized_distance(double* scores, size_t score_count, InputIt2 first2, InputIt2 last2,
	                         double score_cutoff = 1.0) const
	{
		_normalized_distance(scores, score_count, Range(first2, last2), score_cutoff);
	}

	template <typename Sentence2>
	void normalized_distance(double* scores, size_t score_count, const Sentence2& s2,
	                         double score_cutoff = 1.0) const
	{
		_normalized_distance(scores, score_count, Range(s2), score_cutoff);
	}

	template <typename InputIt2>
	void normalized_similarity(double* scores, size_t score_count, InputIt2 first2, InputIt2 last2,
	                           double score_cutoff = 0.0) const
	{
		_normalized_similarity(scores, score_count, Range(first2, last2), score_cutoff);
	}

	template <typename Sentence2>
	void normalized_similarity(double* scores, size_t score_count, const Sentence2& s2,
	                           double score_cutoff = 0.0) const
	{
		_normalized_similarity(scores, score_count, Range(s2), score_cutoff);
	}

  protected:
	template <typename InputIt2>
	void _normalized_distance(double* scores, size_t score_count, Range<InputIt2> s2,
	                          double score_cutoff = 1.0) const
	{
		const T& derived = static_cast<const T&>(*this);
		if (score_count < derived.result_count())
			throw std::invalid_argument("scores has to have >= result_count() elements");

		// reinterpretation only works when the types have the same size
		int64_t* scores_i64 = nullptr;
		ltvector<int64_t> scores_i64_alt;
		if constexpr (sizeof(double) == sizeof(int64_t))
			scores_i64 = reinterpret_cast<int64_t*>(scores);
		else
		{
			scores_i64_alt.resize(derived.result_count());
			scores_i64 = scores_i64_alt.data();
		}

		Range s2_(s2);
		derived.distance(scores_i64, derived.result_count(), s2_);

		for (size_t i = 0; i < derived.get_input_count(); ++i)
		{
			auto maximum = derived.maximum(i, s2);
			double norm_dist =
			    (maximum != 0) ? static_cast<double>(scores_i64[i]) / static_cast<double>(maximum) : 0.0;
			scores[i] = (norm_dist <= score_cutoff) ? norm_dist : 1.0;
		}

// 		if constexpr (sizeof(double) != sizeof(int64_t))
// 			delete[] scores_i64;
	}

	template <typename InputIt2>
	void _normalized_similarity(double* scores, size_t score_count, Range<InputIt2> s2,
	                            double score_cutoff) const
	{
		const T& derived = static_cast<const T&>(*this);
		_normalized_distance(scores, score_count, s2);

		for (size_t i = 0; i < derived.get_input_count(); ++i)
		{
			double norm_sim = 1.0 - scores[i];
			scores[i] = (norm_sim >= score_cutoff) ? norm_sim : 0.0;
		}
	}

	MultiNormalizedMetricBase()
	{
	}
	friend T;
};

template <typename T, typename ResType, int64_t WorstSimilarity, int64_t WorstDistance>
struct MultiDistanceBase : public MultiNormalizedMetricBase<T>
{
	template <typename InputIt2>
	void distance(ResType* scores, size_t score_count, InputIt2 first2, InputIt2 last2,
	              ResType score_cutoff = static_cast<ResType>(WorstDistance)) const
	{
		const T& derived = static_cast<const T&>(*this);
		derived._distance(scores, score_count, Range(first2, last2), score_cutoff);
	}

	template <typename Sentence2>
	void distance(ResType* scores, size_t score_count, const Sentence2& s2,
	              ResType score_cutoff = static_cast<ResType>(WorstDistance)) const
	{
		const T& derived = static_cast<const T&>(*this);
		derived._distance(scores, score_count, Range(s2), score_cutoff);
	}

	template <typename InputIt2>
	void similarity(ResType* scores, size_t score_count, InputIt2 first2, InputIt2 last2,
	                ResType score_cutoff = static_cast<ResType>(WorstSimilarity)) const
	{
		_similarity(scores, score_count, Range(first2, last2), score_cutoff);
	}

	template <typename Sentence2>
	void similarity(ResType* scores, size_t score_count, const Sentence2& s2,
	                ResType score_cutoff = static_cast<ResType>(WorstSimilarity)) const
	{
		_similarity(scores, score_count, Range(s2), score_cutoff);
	}

  protected:
	template <typename InputIt2>
	void _similarity(ResType* scores, size_t score_count, Range<InputIt2> s2, ResType score_cutoff) const
	{
		const T& derived = static_cast<const T&>(*this);
		derived._distance(scores, score_count, s2);

		for (size_t i = 0; i < derived.get_input_count(); ++i)
		{
			ResType maximum = derived.maximum(i, s2);
			ResType sim = maximum - scores[i];
			scores[i] = (sim >= score_cutoff) ? sim : 0;
		}
	}

	MultiDistanceBase()
	{
	}
	friend T;
};

template <typename T, typename ResType, int64_t WorstSimilarity, int64_t WorstDistance>
struct MultiSimilarityBase : public MultiNormalizedMetricBase<T>
{
	template <typename InputIt2>
	void distance(ResType* scores, size_t score_count, InputIt2 first2, InputIt2 last2,
	              ResType score_cutoff = static_cast<ResType>(WorstDistance)) const
	{
		_distance(scores, score_count, Range(first2, last2), score_cutoff);
	}

	template <typename Sentence2>
	void distance(ResType* scores, size_t score_count, const Sentence2& s2,
	              ResType score_cutoff = WorstDistance) const
	{
		_distance(scores, score_count, Range(s2), score_cutoff);
	}

	template <typename InputIt2>
	void similarity(ResType* scores, size_t score_count, InputIt2 first2, InputIt2 last2,
	                ResType score_cutoff = static_cast<ResType>(WorstSimilarity)) const
	{
		const T& derived = static_cast<const T&>(*this);
		derived._similarity(scores, score_count, Range(first2, last2), score_cutoff);
	}

	template <typename Sentence2>
	void similarity(ResType* scores, size_t score_count, const Sentence2& s2,
	                ResType score_cutoff = static_cast<ResType>(WorstSimilarity)) const
	{
		const T& derived = static_cast<const T&>(*this);
		derived._similarity(scores, score_count, Range(s2), score_cutoff);
	}

  protected:
	template <typename InputIt2>
	void _distance(ResType* scores, size_t score_count, Range<InputIt2> s2, ResType score_cutoff) const
	{
		const T& derived = static_cast<const T&>(*this);
		derived._similarity(scores, score_count, s2);

		for (size_t i = 0; i < derived.get_input_count(); ++i)
		{
			ResType maximum = derived.maximum(i, s2);
			ResType dist = maximum - scores[i];

			if constexpr (std::is_floating_point_v<ResType>)
				scores[i] = (dist <= score_cutoff) ? dist : 1.0;
			else
				scores[i] = (dist <= score_cutoff) ? dist : score_cutoff + 1;
		}
	}

	MultiSimilarityBase()
	{
	}
	friend T;
};

} // namespace rapidfuzz::detail

namespace rapidfuzz::detail
{

/**
 * Removes common prefix of two string views
 */
template <typename InputIt1, typename InputIt2>
size_t remove_common_prefix(Range<InputIt1>& s1, Range<InputIt2>& s2)
{
	auto first1 = std::begin(s1);
	auto prefix =
	    std::distance(first1, std::mismatch(first1, std::end(s1), std::begin(s2), std::end(s2)).first);
	s1.remove_prefix(prefix);
	s2.remove_prefix(prefix);
	return static_cast<size_t>(prefix);
}

/**
 * Removes common suffix of two string views
 */
template <typename InputIt1, typename InputIt2>
size_t remove_common_suffix(Range<InputIt1>& s1, Range<InputIt2>& s2)
{
	auto rfirst1 = std::rbegin(s1);
	auto suffix =
	    std::distance(rfirst1, std::mismatch(rfirst1, std::rend(s1), std::rbegin(s2), std::rend(s2)).first);
	s1.remove_suffix(suffix);
	s2.remove_suffix(suffix);
	return static_cast<size_t>(suffix);
}

/**
 * Removes common affix of two string views
 */
template <typename InputIt1, typename InputIt2>
StringAffix remove_common_affix(Range<InputIt1>& s1, Range<InputIt2>& s2)
{
	return StringAffix{remove_common_prefix(s1, s2), remove_common_suffix(s1, s2)};
}

template <typename, typename = void>
struct is_space_dispatch_tag : std::integral_constant<int, 0>
{
};

template <typename CharT>
struct is_space_dispatch_tag<CharT, typename std::enable_if<sizeof(CharT) == 1>::type>
    : std::integral_constant<int, 1>
{
};

/*
 * Implementation of is_space for char types that are at least 2 Byte in size
 */
template <typename CharT>
bool is_space_impl(const CharT ch, std::integral_constant<int, 0>)
{
	switch (ch)
	{
	case 0x0009:
	case 0x000A:
	case 0x000B:
	case 0x000C:
	case 0x000D:
	case 0x001C:
	case 0x001D:
	case 0x001E:
	case 0x001F:
	case 0x0020:
	case 0x0085:
	case 0x00A0:
	case 0x1680:
	case 0x2000:
	case 0x2001:
	case 0x2002:
	case 0x2003:
	case 0x2004:
	case 0x2005:
	case 0x2006:
	case 0x2007:
	case 0x2008:
	case 0x2009:
	case 0x200A:
	case 0x2028:
	case 0x2029:
	case 0x202F:
	case 0x205F:
	case 0x3000:
		return true;
	}
	return false;
}

/*
 * Implementation of is_space for char types that are 1 Byte in size
 */
template <typename CharT>
bool is_space_impl(const CharT ch, std::integral_constant<int, 1>)
{
	switch (ch)
	{
	case 0x0009:
	case 0x000A:
	case 0x000B:
	case 0x000C:
	case 0x000D:
	case 0x001C:
	case 0x001D:
	case 0x001E:
	case 0x001F:
	case 0x0020:
		return true;
	}
	return false;
}

/*
 * checks whether unicode characters have the bidirectional
 * type 'WS', 'B' or 'S' or the category 'Zs'
 */
template <typename CharT>
bool is_space(const CharT ch)
{
	return is_space_impl(ch, is_space_dispatch_tag<CharT>{});
}

} // namespace rapidfuzz::detail

namespace rapidfuzz::detail
{

template <bool RecordMatrix>
struct LCSseqResult;

template <>
struct LCSseqResult<true>
{
	ShiftedBitMatrix<uint64_t> S;

	int64_t sim;
};

template <>
struct LCSseqResult<false>
{
	int64_t sim;
};

/*
 * An encoded mbleven model table.
 *
 * Each 8-bit integer represents an edit sequence, with using two
 * bits for a single operation.
 *
 * Each Row of 8 integers represent all possible combinations
 * of edit sequences for a gived maximum edit distance and length
 * difference between the two strings, that is below the maximum
 * edit distance
 *
 *   0x1 = 01 = DELETE,
 *   0x2 = 10 = INSERT
 *
 * 0x5 -> DEL + DEL
 * 0x6 -> DEL + INS
 * 0x9 -> INS + DEL
 * 0xA -> INS + INS
 */
static constexpr std::array<std::array<uint8_t, 7>, 14> lcs_seq_mbleven2018_matrix = {{
    /* max edit distance 1 */
    {0},
    /* case does not occur */ /* len_diff 0 */
    {0x01},                   /* len_diff 1 */
    /* max edit distance 2 */
    {0x09, 0x06}, /* len_diff 0 */
    {0x01},       /* len_diff 1 */
    {0x05},       /* len_diff 2 */
    /* max edit distance 3 */
    {0x09, 0x06},       /* len_diff 0 */
    {0x25, 0x19, 0x16}, /* len_diff 1 */
    {0x05},             /* len_diff 2 */
    {0x15},             /* len_diff 3 */
    /* max edit distance 4 */
    {0x96, 0x66, 0x5A, 0x99, 0x69, 0xA5}, /* len_diff 0 */
    {0x25, 0x19, 0x16},                   /* len_diff 1 */
    {0x65, 0x56, 0x95, 0x59},             /* len_diff 2 */
    {0x15},                               /* len_diff 3 */
    {0x55},                               /* len_diff 4 */
}};

template <typename InputIt1, typename InputIt2>
int64_t lcs_seq_mbleven2018(Range<InputIt1> s1, Range<InputIt2> s2, int64_t score_cutoff)
{
	auto len1 = s1.size();
	auto len2 = s2.size();

	if (len1 < len2)
		return lcs_seq_mbleven2018(s2, s1, score_cutoff);

	auto len_diff = len1 - len2;
	int64_t max_misses = static_cast<ptrdiff_t>(len1) - score_cutoff;
	auto ops_index = (max_misses + max_misses * max_misses) / 2 + len_diff - 1;
	auto& possible_ops = lcs_seq_mbleven2018_matrix[static_cast<size_t>(ops_index)];
	int64_t max_len = 0;

	for (uint8_t ops : possible_ops)
	{
		ptrdiff_t s1_pos = 0;
		ptrdiff_t s2_pos = 0;
		int64_t cur_len = 0;

		while (s1_pos < len1 && s2_pos < len2)
		{
			if (s1[s1_pos] != s2[s2_pos])
			{
				if (!ops)
					break;
				if (ops & 1)
					s1_pos++;
				else if (ops & 2)
					s2_pos++;
#if defined(__GNUC__) && !defined(__clang__) && !defined(__ICC) && __GNUC__ < 10
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#endif
				ops >>= 2;
#if defined(__GNUC__) && !defined(__clang__) && !defined(__ICC) && __GNUC__ < 10
#pragma GCC diagnostic pop
#endif
			}
			else
			{
				cur_len++;
				s1_pos++;
				s2_pos++;
			}
		}

		max_len = std::max(max_len, cur_len);
	}

	return (max_len >= score_cutoff) ? max_len : 0;
}

template <bool RecordMatrix>
struct LCSseqResult;

#ifdef RAPIDFUZZ_SIMD
template <typename VecType, typename InputIt, int _lto_hack = RAPIDFUZZ_LTO_HACK>
void lcs_simd(Range<int64_t*> scores, const BlockPatternMatchVector& block, Range<InputIt> s2,
              int64_t score_cutoff) noexcept
{
#ifdef RAPIDFUZZ_AVX2
	using namespace simd_avx2;
#else
	using namespace simd_sse2;
#endif
	auto score_iter = scores.begin();
	static constexpr size_t vecs = static_cast<size_t>(native_simd<uint64_t>::size());
	assert(block.size() % vecs == 0);

	for (size_t cur_vec = 0; cur_vec < block.size(); cur_vec += vecs)
	{
		native_simd<VecType> S(static_cast<VecType>(-1));

		for (const auto& ch : s2)
		{
			alignas(32) std::array<uint64_t, vecs> stored;
			unroll<int, vecs>([&](auto i) { stored[i] = block.get(cur_vec + i, ch); });

			native_simd<VecType> Matches(stored.data());
			native_simd<VecType> u = S & Matches;
			S = (S + u) | (S - u);
		}

		S = ~S;

		auto counts = popcount(S);
		unroll<int, counts.size()>([&](auto i) {
			*score_iter =
			    (static_cast<int64_t>(counts[i]) >= score_cutoff) ? static_cast<int64_t>(counts[i]) : 0;
			score_iter++;
		});
	}
}

#endif

template <size_t N, bool RecordMatrix, typename PMV, typename InputIt1, typename InputIt2>
auto lcs_unroll(const PMV& block, Range<InputIt1>, Range<InputIt2> s2, int64_t score_cutoff = 0)
    -> LCSseqResult<RecordMatrix>
{
	uint64_t S[N];
	unroll<size_t, N>([&](size_t i) { S[i] = ~UINT64_C(0); });

	LCSseqResult<RecordMatrix> res;
	if constexpr (RecordMatrix)
		res.S = ShiftedBitMatrix<uint64_t>(s2.size(), N, ~UINT64_C(0));

	for (ptrdiff_t i = 0; i < s2.size(); ++i)
	{
		uint64_t carry = 0;
		unroll<size_t, N>([&](size_t word) {
			uint64_t Matches = block.get(word, s2[i]);
			uint64_t u = S[word] & Matches;
			uint64_t x = addc64(S[word], u, carry, &carry);
			S[word] = x | (S[word] - u);

			if constexpr (RecordMatrix)
				res.S[i][word] = S[word];
		});
	}

	res.sim = 0;
	unroll<size_t, N>([&](size_t i) { res.sim += popcount(~S[i]); });

	if (res.sim < score_cutoff)
		res.sim = 0;

	return res;
}

template <bool RecordMatrix, typename PMV, typename InputIt1, typename InputIt2>
auto lcs_blockwise(const PMV& block, Range<InputIt1>, Range<InputIt2> s2, int64_t score_cutoff = 0)
    -> LCSseqResult<RecordMatrix>
{
	auto words = block.size();
	ltvector<uint64_t> S(words, ~UINT64_C(0));

	LCSseqResult<RecordMatrix> res;
	if constexpr (RecordMatrix)
		res.S = ShiftedBitMatrix<uint64_t>(s2.size(), words, ~UINT64_C(0));

	for (ptrdiff_t i = 0; i < s2.size(); ++i)
	{
		uint64_t carry = 0;
		for (size_t word = 0; word < words; ++word)
		{
			const uint64_t Matches = block.get(word, s2[i]);
			uint64_t Stemp = S[word];

			uint64_t u = Stemp & Matches;

			uint64_t x = addc64(Stemp, u, carry, &carry);
			S[word] = x | (Stemp - u);

			if constexpr (RecordMatrix)
				res.S[i][word] = S[word];
		}
	}

	res.sim = 0;
	for (uint64_t Stemp : S)
		res.sim += popcount(~Stemp);

	if (res.sim < score_cutoff)
		res.sim = 0;

	return res;
}

template <typename PMV, typename InputIt1, typename InputIt2>
int64_t longest_common_subsequence(const PMV& block, Range<InputIt1> s1, Range<InputIt2> s2,
                                   int64_t score_cutoff)
{
	auto nr = ceil_div(s1.size(), 64);
	switch (nr)
	{
	case 0:
		return 0;
	case 1:
		return lcs_unroll<1, false>(block, s1, s2, score_cutoff).sim;
	case 2:
		return lcs_unroll<2, false>(block, s1, s2, score_cutoff).sim;
	case 3:
		return lcs_unroll<3, false>(block, s1, s2, score_cutoff).sim;
	case 4:
		return lcs_unroll<4, false>(block, s1, s2, score_cutoff).sim;
	case 5:
		return lcs_unroll<5, false>(block, s1, s2, score_cutoff).sim;
	case 6:
		return lcs_unroll<6, false>(block, s1, s2, score_cutoff).sim;
	case 7:
		return lcs_unroll<7, false>(block, s1, s2, score_cutoff).sim;
	case 8:
		return lcs_unroll<8, false>(block, s1, s2, score_cutoff).sim;
	default:
		return lcs_blockwise<false>(block, s1, s2, score_cutoff).sim;
	}
}

template <typename InputIt1, typename InputIt2>
int64_t longest_common_subsequence(Range<InputIt1> s1, Range<InputIt2> s2, int64_t score_cutoff)
{
	if (s1.empty())
		return 0;
	if (s1.size() <= 64)
		return longest_common_subsequence(PatternMatchVector(s1), s1, s2, score_cutoff);

	return longest_common_subsequence(BlockPatternMatchVector(s1), s1, s2, score_cutoff);
}

template <typename InputIt1, typename InputIt2>
int64_t lcs_seq_similarity(const BlockPatternMatchVector& block, Range<InputIt1> s1, Range<InputIt2> s2,
                           int64_t score_cutoff)
{
	auto len1 = s1.size();
	auto len2 = s2.size();
	int64_t max_misses = static_cast<int64_t>(len1) + len2 - 2 * score_cutoff;

	/* no edits are allowed */
	if (max_misses == 0 || (max_misses == 1 && len1 == len2))
		return std::equal(s1.begin(), s1.end(), s2.begin(), s2.end()) ? len1 : 0;

	if (max_misses < std::abs(len1 - len2))
		return 0;

	// do this first, since we can not remove any affix in encoded form
	if (max_misses >= 5)
		return longest_common_subsequence(block, s1, s2, score_cutoff);

	/* common affix does not effect Levenshtein distance */
	StringAffix affix = remove_common_affix(s1, s2);
	int64_t lcs_sim = static_cast<int64_t>(affix.prefix_len + affix.suffix_len);
	if (!s1.empty() && !s2.empty())
		lcs_sim += lcs_seq_mbleven2018(s1, s2, score_cutoff - lcs_sim);

	return (lcs_sim >= score_cutoff) ? lcs_sim : 0;
}

template <typename InputIt1, typename InputIt2>
int64_t lcs_seq_similarity(Range<InputIt1> s1, Range<InputIt2> s2, int64_t score_cutoff)
{
	auto len1 = s1.size();
	auto len2 = s2.size();

	// Swapping the strings so the second string is shorter
	if (len1 < len2)
		return lcs_seq_similarity(s2, s1, score_cutoff);

	int64_t max_misses = static_cast<int64_t>(len1) + len2 - 2 * score_cutoff;

	/* no edits are allowed */
	if (max_misses == 0 || (max_misses == 1 && len1 == len2))
		return std::equal(s1.begin(), s1.end(), s2.begin(), s2.end()) ? len1 : 0;

	if (max_misses < std::abs(len1 - len2))
		return 0;

	/* common affix does not effect Levenshtein distance */
	StringAffix affix = remove_common_affix(s1, s2);
	int64_t lcs_sim = static_cast<int64_t>(affix.prefix_len + affix.suffix_len);
	if (s1.size() && s2.size())
	{
		if (max_misses < 5)
			lcs_sim += lcs_seq_mbleven2018(s1, s2, score_cutoff - lcs_sim);
		else
			lcs_sim += longest_common_subsequence(s1, s2, score_cutoff - lcs_sim);
	}

	return (lcs_sim >= score_cutoff) ? lcs_sim : 0;
}

/**
 * @brief recover alignment from bitparallel Levenshtein matrix
 */
template <typename InputIt1, typename InputIt2>
Editops recover_alignment(Range<InputIt1> s1, Range<InputIt2> s2, const LCSseqResult<true>& matrix,
                          StringAffix affix)
{
	auto len1 = s1.size();
	auto len2 = s2.size();
	size_t dist = static_cast<size_t>(static_cast<int64_t>(len1) + len2 - 2 * matrix.sim);
	Editops editops(dist);
	editops.set_src_len(len1 + affix.prefix_len + affix.suffix_len);
	editops.set_dest_len(len2 + affix.prefix_len + affix.suffix_len);

	if (dist == 0)
		return editops;

	auto col = len1;
	auto row = len2;

	while (row && col)
	{
		/* Deletion */
		if (matrix.S.test_bit(row - 1, col - 1))
		{
			assert(dist > 0);
			dist--;
			col--;
			editops[dist].type = EditType::Delete;
			editops[dist].src_pos = col + affix.prefix_len;
			editops[dist].dest_pos = row + affix.prefix_len;
		}
		else
		{
			row--;

			/* Insertion */
			if (row && !(matrix.S.test_bit(row - 1, col - 1)))
			{
				assert(dist > 0);
				dist--;
				editops[dist].type = EditType::Insert;
				editops[dist].src_pos = col + affix.prefix_len;
				editops[dist].dest_pos = row + affix.prefix_len;
			}
			/* Match */
			else
			{
				col--;
				assert(s1[col] == s2[row]);
			}
		}
	}

	while (col)
	{
		dist--;
		col--;
		editops[dist].type = EditType::Delete;
		editops[dist].src_pos = col + affix.prefix_len;
		editops[dist].dest_pos = row + affix.prefix_len;
	}

	while (row)
	{
		dist--;
		row--;
		editops[dist].type = EditType::Insert;
		editops[dist].src_pos = col + affix.prefix_len;
		editops[dist].dest_pos = row + affix.prefix_len;
	}

	return editops;
}

template <typename InputIt1, typename InputIt2>
LCSseqResult<true> lcs_matrix(Range<InputIt1> s1, Range<InputIt2> s2)
{
	auto nr = ceil_div(s1.size(), 64);
	switch (nr)
	{
	case 0: {
		LCSseqResult<true> res;
		res.sim = 0;
		return res;
	}
	case 1:
		return lcs_unroll<1, true>(PatternMatchVector(s1), s1, s2);
	case 2:
		return lcs_unroll<2, true>(BlockPatternMatchVector(s1), s1, s2);
	case 3:
		return lcs_unroll<3, true>(BlockPatternMatchVector(s1), s1, s2);
	case 4:
		return lcs_unroll<4, true>(BlockPatternMatchVector(s1), s1, s2);
	case 5:
		return lcs_unroll<5, true>(BlockPatternMatchVector(s1), s1, s2);
	case 6:
		return lcs_unroll<6, true>(BlockPatternMatchVector(s1), s1, s2);
	case 7:
		return lcs_unroll<7, true>(BlockPatternMatchVector(s1), s1, s2);
	case 8:
		return lcs_unroll<8, true>(BlockPatternMatchVector(s1), s1, s2);
	default:
		return lcs_blockwise<true>(BlockPatternMatchVector(s1), s1, s2);
	}
}

template <typename InputIt1, typename InputIt2>
Editops lcs_seq_editops(Range<InputIt1> s1, Range<InputIt2> s2)
{
	/* prefix and suffix are no-ops, which do not need to be added to the editops */
	StringAffix affix = remove_common_affix(s1, s2);

	return recover_alignment(s1, s2, lcs_matrix(s1, s2), affix);
}

class LCSseq : public SimilarityBase<LCSseq, int64_t, 0, std::numeric_limits<int64_t>::max()>
{
	friend SimilarityBase<LCSseq, int64_t, 0, std::numeric_limits<int64_t>::max()>;
	friend NormalizedMetricBase<LCSseq>;

	template <typename InputIt1, typename InputIt2>
	static int64_t maximum(Range<InputIt1> s1, Range<InputIt2> s2)
	{
		return std::max(s1.size(), s2.size());
	}

	template <typename InputIt1, typename InputIt2>
	static int64_t _similarity(Range<InputIt1> s1, Range<InputIt2> s2, int64_t score_cutoff,
	                           [[maybe_unused]] int64_t score_hint)
	{
		return lcs_seq_similarity(s1, s2, score_cutoff);
	}
};

} // namespace rapidfuzz::detail

namespace rapidfuzz
{

namespace detail
{
template <typename T>
auto inner_type(T const*) -> T;

template <typename T>
auto inner_type(T const&) -> typename T::value_type;
} // namespace detail

template <typename T>
using char_type = decltype(detail::inner_type(std::declval<T const&>()));

/* backport of std::iter_value_t from C++20
 * This does not cover the complete functionality, but should be enough for
 * the use cases in this library
 */
template <typename T>
using iter_value_t = typename std::iterator_traits<T>::value_type;

// taken from
// https://stackoverflow.com/questions/16893992/check-if-type-can-be-explicitly-converted
template <typename From, typename To>
struct is_explicitly_convertible
{
	template <typename T>
	static void f(T);

	template <typename F, typename T>
	static constexpr auto test(int /*unused*/) -> decltype(f(static_cast<T>(std::declval<F>())), true)
	{
		return true;
	}

	template <typename F, typename T>
	static constexpr auto test(...) -> bool
	{
		return false;
	}

	static bool const value = test<From, To>(0);
};

} // namespace rapidfuzz

namespace rapidfuzz
{

template <typename InputIt1, typename InputIt2>
int64_t lcs_seq_distance(InputIt1 first1, InputIt1 last1, InputIt2 first2, InputIt2 last2,
                         int64_t score_cutoff = std::numeric_limits<int64_t>::max())
{
	return detail::LCSseq::distance(first1, last1, first2, last2, score_cutoff, score_cutoff);
}

template <typename Sentence1, typename Sentence2>
int64_t lcs_seq_distance(const Sentence1& s1, const Sentence2& s2,
                         int64_t score_cutoff = std::numeric_limits<int64_t>::max())
{
	return detail::LCSseq::distance(s1, s2, score_cutoff, score_cutoff);
}

template <typename InputIt1, typename InputIt2>
int64_t lcs_seq_similarity(InputIt1 first1, InputIt1 last1, InputIt2 first2, InputIt2 last2,
                           int64_t score_cutoff = 0)
{
	return detail::LCSseq::similarity(first1, last1, first2, last2, score_cutoff, score_cutoff);
}

template <typename Sentence1, typename Sentence2>
int64_t lcs_seq_similarity(const Sentence1& s1, const Sentence2& s2, int64_t score_cutoff = 0)
{
	return detail::LCSseq::similarity(s1, s2, score_cutoff, score_cutoff);
}

template <typename InputIt1, typename InputIt2>
double lcs_seq_normalized_distance(InputIt1 first1, InputIt1 last1, InputIt2 first2, InputIt2 last2,
                                   double score_cutoff = 1.0)
{
	return detail::LCSseq::normalized_distance(first1, last1, first2, last2, score_cutoff, score_cutoff);
}

template <typename Sentence1, typename Sentence2>
double lcs_seq_normalized_distance(const Sentence1& s1, const Sentence2& s2, double score_cutoff = 1.0)
{
	return detail::LCSseq::normalized_distance(s1, s2, score_cutoff, score_cutoff);
}

template <typename InputIt1, typename InputIt2>
double lcs_seq_normalized_similarity(InputIt1 first1, InputIt1 last1, InputIt2 first2, InputIt2 last2,
                                     double score_cutoff = 0.0)
{
	return detail::LCSseq::normalized_similarity(first1, last1, first2, last2, score_cutoff, score_cutoff);
}

template <typename Sentence1, typename Sentence2>
double lcs_seq_normalized_similarity(const Sentence1& s1, const Sentence2& s2, double score_cutoff = 0.0)
{
	return detail::LCSseq::normalized_similarity(s1, s2, score_cutoff, score_cutoff);
}

template <typename InputIt1, typename InputIt2>
Editops lcs_seq_editops(InputIt1 first1, InputIt1 last1, InputIt2 first2, InputIt2 last2)
{
	return detail::lcs_seq_editops(detail::Range(first1, last1), detail::Range(first2, last2));
}

template <typename Sentence1, typename Sentence2>
Editops lcs_seq_editops(const Sentence1& s1, const Sentence2& s2)
{
	return detail::lcs_seq_editops(detail::Range(s1), detail::Range(s2));
}

#ifdef RAPIDFUZZ_SIMD
namespace experimental
{
template <int MaxLen>
struct MultiLCSseq : public detail::MultiSimilarityBase<MultiLCSseq<MaxLen>, int64_t, 0,
                                                        std::numeric_limits<int64_t>::max()>
{
  private:
	friend detail::MultiSimilarityBase<MultiLCSseq<MaxLen>, int64_t, 0, std::numeric_limits<int64_t>::max()>;
	friend detail::MultiNormalizedMetricBase<MultiLCSseq<MaxLen>>;

	constexpr static size_t get_vec_size()
	{
#ifdef RAPIDFUZZ_AVX2
		using namespace detail::simd_avx2;
#else
		using namespace detail::simd_sse2;
#endif
		if constexpr (MaxLen <= 8)
			return native_simd<uint8_t>::size();
		else if constexpr (MaxLen <= 16)
			return native_simd<uint16_t>::size();
		else if constexpr (MaxLen <= 32)
			return native_simd<uint32_t>::size();
		else if constexpr (MaxLen <= 64)
			return native_simd<uint64_t>::size();

		static_assert(MaxLen <= 64);
	}

	constexpr static size_t find_block_count(size_t count)
	{
		size_t vec_size = get_vec_size();
		size_t simd_vec_count = detail::ceil_div(count, vec_size);
		return detail::ceil_div(simd_vec_count * vec_size * MaxLen, 64);
	}

  public:
	MultiLCSseq(size_t count)
	    : input_count(count), pos(0), PM(find_block_count(count) * 64)
	{
		str_lens.resize(result_count());
	}

	/**
	 * @brief get minimum size required for result vectors passed into
	 * - distance
	 * - similarity
	 * - normalized_distance
	 * - normalized_similarity
	 *
	 * @return minimum vector size
	 */
	size_t result_count() const
	{
		size_t vec_size = get_vec_size();
		size_t simd_vec_count = detail::ceil_div(input_count, vec_size);
		return simd_vec_count * vec_size;
	}

	template <typename Sentence1>
	void insert(const Sentence1& s1_)
	{
		insert(detail::to_begin(s1_), detail::to_end(s1_));
	}

	template <typename InputIt1>
	void insert(InputIt1 first1, InputIt1 last1)
	{
		auto len = std::distance(first1, last1);
		int block_pos = static_cast<int>((pos * MaxLen) % 64);
		auto block = (pos * MaxLen) / 64;
		assert(len <= MaxLen);

		if (pos >= input_count)
			throw std::invalid_argument("out of bounds insert");

		str_lens[pos] = static_cast<size_t>(len);

		for (; first1 != last1; ++first1)
		{
			PM.insert(block, *first1, block_pos);
			block_pos++;
		}
		pos++;
	}

  private:
	template <typename InputIt2>
	void _similarity(int64_t* scores, size_t score_count, detail::Range<InputIt2> s2,
	                 int64_t score_cutoff = 0) const
	{
		if (score_count < result_count())
			throw std::invalid_argument("scores has to have >= result_count() elements");

		detail::Range scores_(scores, scores + score_count);
		if constexpr (MaxLen == 8)
			detail::lcs_simd<uint8_t>(scores_, PM, s2, score_cutoff);
		else if constexpr (MaxLen == 16)
			detail::lcs_simd<uint16_t>(scores_, PM, s2, score_cutoff);
		else if constexpr (MaxLen == 32)
			detail::lcs_simd<uint32_t>(scores_, PM, s2, score_cutoff);
		else if constexpr (MaxLen == 64)
			detail::lcs_simd<uint64_t>(scores_, PM, s2, score_cutoff);
	}

	template <typename InputIt2>
	int64_t maximum(size_t s1_idx, detail::Range<InputIt2> s2) const
	{
		return std::max(static_cast<int64_t>(str_lens[s1_idx]), static_cast<int64_t>(s2.size()));
	}

	size_t get_input_count() const noexcept
	{
		return input_count;
	}

	size_t input_count;
	size_t pos;
	detail::BlockPatternMatchVector PM;
	ltvector<size_t> str_lens;
};
} /* namespace experimental */
#endif

template <typename CharT1>
struct CachedLCSseq
    : detail::CachedSimilarityBase<CachedLCSseq<CharT1>, int64_t, 0, std::numeric_limits<int64_t>::max()>
{
	template <typename Sentence1>
	explicit CachedLCSseq(const Sentence1& s1_)
	    : CachedLCSseq(detail::to_begin(s1_), detail::to_end(s1_))
	{
	}

	template <typename InputIt1>
	CachedLCSseq(InputIt1 first1, InputIt1 last1)
	    : s1(first1, last1), PM(detail::Range(first1, last1))
	{
	}

  private:
	friend detail::CachedSimilarityBase<CachedLCSseq<CharT1>, int64_t, 0,
	                                    std::numeric_limits<int64_t>::max()>;
	friend detail::CachedNormalizedMetricBase<CachedLCSseq<CharT1>>;

	template <typename InputIt2>
	int64_t maximum(detail::Range<InputIt2> s2) const
	{
		return std::max(static_cast<ptrdiff_t>(s1.size()), s2.size());
	}

	template <typename InputIt2>
	int64_t _similarity(detail::Range<InputIt2> s2, int64_t score_cutoff,
	                    [[maybe_unused]] int64_t score_hint) const
	{
		return detail::lcs_seq_similarity(PM, detail::Range(s1), s2, score_cutoff);
	}

	ltvector<CharT1> s1;
	detail::BlockPatternMatchVector PM;
};

template <typename Sentence1>
explicit CachedLCSseq(const Sentence1& s1_) -> CachedLCSseq<char_type<Sentence1>>;

template <typename InputIt1>
CachedLCSseq(InputIt1 first1, InputIt1 last1) -> CachedLCSseq<iter_value_t<InputIt1>>;

} // namespace rapidfuzz

namespace rapidfuzz
{

#ifdef RAPIDFUZZ_SIMD
namespace experimental
{
template <int MaxLen>
struct MultiIndel
    : public detail::MultiDistanceBase<MultiIndel<MaxLen>, int64_t, 0, std::numeric_limits<int64_t>::max()>
{
  private:
	friend detail::MultiDistanceBase<MultiIndel<MaxLen>, int64_t, 0, std::numeric_limits<int64_t>::max()>;
	friend detail::MultiNormalizedMetricBase<MultiIndel<MaxLen>>;

  public:
	MultiIndel(size_t count)
	    : scorer(count)
	{
	}

	/**
	 * @brief get minimum size required for result vectors passed into
	 * - distance
	 * - similarity
	 * - normalized_distance
	 * - normalized_similarity
	 *
	 * @return minimum vector size
	 */
	size_t result_count() const
	{
		return scorer.result_count();
	}

	template <typename Sentence1>
	void insert(const Sentence1& s1_)
	{
		insert(detail::to_begin(s1_), detail::to_end(s1_));
	}

	template <typename InputIt1>
	void insert(InputIt1 first1, InputIt1 last1)
	{
		scorer.insert(first1, last1);
		str_lens.push_back(static_cast<size_t>(std::distance(first1, last1)));
	}

  private:
	template <typename InputIt2>
	void _distance(int64_t* scores, size_t score_count, detail::Range<InputIt2> s2,
	               int64_t score_cutoff = std::numeric_limits<int64_t>::max()) const
	{
		scorer.similarity(scores, score_count, s2);

		for (size_t i = 0; i < get_input_count(); ++i)
		{
			int64_t maximum_ = maximum(i, s2);
			int64_t dist = maximum_ - 2 * scores[i];
			scores[i] = (dist <= score_cutoff) ? dist : score_cutoff + 1;
		}
	}

	template <typename InputIt2>
	int64_t maximum(size_t s1_idx, detail::Range<InputIt2> s2) const
	{
		return static_cast<int64_t>(str_lens[s1_idx]) + s2.size();
	}

	size_t get_input_count() const noexcept
	{
		return str_lens.size();
	}

	ltvector<size_t> str_lens;
	MultiLCSseq<MaxLen> scorer;
};
} /* namespace experimental */
#endif

template <typename CharT1>
struct CachedIndel : public detail::CachedDistanceBase<CachedIndel<CharT1>, int64_t, 0,
                                                       std::numeric_limits<int64_t>::max()>
{
	template <typename Sentence1>
	explicit CachedIndel(const Sentence1& s1_)
	    : CachedIndel(detail::to_begin(s1_), detail::to_end(s1_))
	{
	}

	template <typename InputIt1>
	CachedIndel(InputIt1 first1, InputIt1 last1)
	    : s1_len(std::distance(first1, last1)), scorer(first1, last1)
	{
	}

  private:
	friend detail::CachedDistanceBase<CachedIndel<CharT1>, int64_t, 0, std::numeric_limits<int64_t>::max()>;
	friend detail::CachedNormalizedMetricBase<CachedIndel<CharT1>>;

	template <typename InputIt2>
	int64_t maximum(detail::Range<InputIt2> s2) const
	{
		return s1_len + s2.size();
	}

	template <typename InputIt2>
	int64_t _distance(detail::Range<InputIt2> s2, int64_t score_cutoff, int64_t score_hint) const
	{
		int64_t maximum_ = maximum(s2);
		int64_t lcs_cutoff = std::max<int64_t>(0, maximum_ / 2 - score_cutoff);
		int64_t lcs_cutoff_hint = std::max<int64_t>(0, maximum_ / 2 - score_hint);
		int64_t lcs_sim = scorer.similarity(s2, lcs_cutoff, lcs_cutoff_hint);
		int64_t dist = maximum_ - 2 * lcs_sim;
		return (dist <= score_cutoff) ? dist : score_cutoff + 1;
	}

	int64_t s1_len;
	CachedLCSseq<CharT1> scorer;
};

template <typename Sentence1>
explicit CachedIndel(const Sentence1& s1_) -> CachedIndel<char_type<Sentence1>>;

template <typename InputIt1>
CachedIndel(InputIt1 first1, InputIt1 last1) -> CachedIndel<iter_value_t<InputIt1>>;

} // namespace rapidfuzz

namespace rapidfuzz::fuzz
{
template <typename CharT1>
struct CachedRatio
{
	template <typename InputIt1>
	CachedRatio(InputIt1 first1, InputIt1 last1)
	    : cached_indel(first1, last1)
	{
	}

	template <typename Sentence1>
	CachedRatio(const Sentence1& s1)
	    : cached_indel(s1)
	{
	}

	template <typename InputIt2>
	double similarity(InputIt2 first2, InputIt2 last2, double score_cutoff = 0.0,
	                  double score_hint = 0.0) const;

	template <typename Sentence2>
	double similarity(const Sentence2& s2, double score_cutoff = 0.0, double score_hint = 0.0) const;

	// private:
	CachedIndel<CharT1> cached_indel;
};

template <typename Sentence1>
CachedRatio(const Sentence1& s1) -> CachedRatio<char_type<Sentence1>>;

template <typename InputIt1>
CachedRatio(InputIt1 first1, InputIt1 last1) -> CachedRatio<iter_value_t<InputIt1>>;

template <typename InputIt1, typename InputIt2>
ScoreAlignment<double> partial_ratio_alignment(InputIt1 first1, InputIt1 last1, InputIt2 first2,
                                               InputIt2 last2, double score_cutoff = 0);

template <typename Sentence1, typename Sentence2>
ScoreAlignment<double> partial_ratio_alignment(const Sentence1& s1, const Sentence2& s2,
                                               double score_cutoff = 0);

template <typename CharT1>
template <typename InputIt2>
double CachedRatio<CharT1>::similarity(InputIt2 first2, InputIt2 last2, double score_cutoff,
                                       double score_hint) const
{
	return similarity(detail::Range(first2, last2), score_cutoff, score_hint);
}

template <typename CharT1>
template <typename Sentence2>
double CachedRatio<CharT1>::similarity(const Sentence2& s2, double score_cutoff, double score_hint) const
{
	return cached_indel.normalized_similarity(s2, score_cutoff / 100, score_hint / 100) * 100;
}
} // namespace rapidfuzz::fuzz

namespace rapidfuzz::fuzz::fuzz_detail
{

template <typename InputIt1, typename InputIt2, typename CachedCharT1>
ScoreAlignment<double>
partial_ratio_impl(rapidfuzz::detail::Range<InputIt1> s1, rapidfuzz::detail::Range<InputIt2> s2,
                   const CachedRatio<CachedCharT1>& cached_ratio,
                   const detail::CharSet<iter_value_t<InputIt1>>& s1_char_set, double score_cutoff)
{
	ScoreAlignment<double> res;
	auto len1 = static_cast<size_t>(s1.size());
	auto len2 = static_cast<size_t>(s2.size());
	res.src_start = 0;
	res.src_end = len1;
	res.dest_start = 0;
	res.dest_end = len1;

	if (len2 > len1)
	{
		int64_t maximum = static_cast<int64_t>(len1) * 2;
		double norm_cutoff_sim = rapidfuzz::detail::NormSim_to_NormDist(score_cutoff / 100);
		int64_t cutoff_dist = static_cast<int64_t>(std::ceil(static_cast<double>(maximum) * norm_cutoff_sim));
		int64_t best_dist = std::numeric_limits<int64_t>::max();
		ltvector<int64_t> scores(len2 - len1, -1);
		ltvector<std::pair<size_t, size_t>> windows = {{0, len2 - len1 - 1}};
		ltvector<std::pair<size_t, size_t>> new_windows;

		while (!windows.empty())
		{
			for (const auto& window : windows)
			{
				auto subseq1_first = s2.begin() + static_cast<ptrdiff_t>(window.first);
				auto subseq2_first = s2.begin() + static_cast<ptrdiff_t>(window.second);
				rapidfuzz::detail::Range subseq1(subseq1_first, subseq1_first + static_cast<ptrdiff_t>(len1));
				rapidfuzz::detail::Range subseq2(subseq2_first, subseq2_first + static_cast<ptrdiff_t>(len1));

				if (scores[window.first] == -1)
				{
					scores[window.first] = cached_ratio.cached_indel.distance(subseq1);
					if (scores[window.first] < cutoff_dist)
					{
						cutoff_dist = best_dist = scores[window.first];
						res.dest_start = window.first;
						res.dest_end = window.first + len1;
						if (best_dist == 0)
						{
							res.score = 100;
							return res;
						}
					}
				}
				if (scores[window.second] == -1)
				{
					scores[window.second] = cached_ratio.cached_indel.distance(subseq2);
					if (scores[window.second] < cutoff_dist)
					{
						cutoff_dist = best_dist = scores[window.second];
						res.dest_start = window.second;
						res.dest_end = window.second + len1;
						if (best_dist == 0)
						{
							res.score = 100;
							return res;
						}
					}
				}

				size_t cell_diff = window.second - window.first;
				if (cell_diff == 1)
					continue;

				/* find the minimum score possible in the range first <-> last */
				int64_t known_edits = std::abs(scores[window.first] - scores[window.second]);
				/* half of the cells that are not needed for known_edits can lead to a better score */
				int64_t min_score = std::min(scores[window.first], scores[window.second]) -
				                    (static_cast<int64_t>(cell_diff) + known_edits / 2);
				if (min_score < cutoff_dist)
				{
					size_t center = cell_diff / 2;
					new_windows.emplace_back(window.first, window.first + center);
					new_windows.emplace_back(window.first + center, window.second);
				}
			}

			std::swap(windows, new_windows);
			new_windows.clear();
		}

		double score = 1.0 - (static_cast<double>(best_dist) / static_cast<double>(maximum));
		score *= 100;
		if (score >= score_cutoff)
			score_cutoff = res.score = score;
	}

	for (size_t i = 1; i < len1; ++i)
	{
		rapidfuzz::detail::Range subseq(s2.begin(), s2.begin() + static_cast<ptrdiff_t>(i));
		if (!s1_char_set.find(subseq.back()))
			continue;

		double ls_ratio = cached_ratio.similarity(subseq, score_cutoff);
		if (ls_ratio > res.score)
		{
			score_cutoff = res.score = ls_ratio;
			res.dest_start = 0;
			res.dest_end = i;
			if (res.score == 100.0)
				return res;
		}
	}

	for (size_t i = len2 - len1; i < len2; ++i)
	{
		rapidfuzz::detail::Range subseq(s2.begin() + static_cast<ptrdiff_t>(i), s2.end());
		if (!s1_char_set.find(subseq.front()))
			continue;

		double ls_ratio = cached_ratio.similarity(subseq, score_cutoff);
		if (ls_ratio > res.score)
		{
			score_cutoff = res.score = ls_ratio;
			res.dest_start = i;
			res.dest_end = len2;
			if (res.score == 100.0)
				return res;
		}
	}

	return res;
}

template <typename InputIt1, typename InputIt2, typename CharT1 = iter_value_t<InputIt1>>
ScoreAlignment<double> partial_ratio_impl(rapidfuzz::detail::Range<InputIt1> s1,
                                          rapidfuzz::detail::Range<InputIt2> s2, double score_cutoff)
{
	CachedRatio<CharT1> cached_ratio(s1);

	detail::CharSet<CharT1> s1_char_set;
	for (auto ch : s1)
		s1_char_set.insert(ch);

	return partial_ratio_impl(s1, s2, cached_ratio, s1_char_set, score_cutoff);
}
} // namespace fuzz_detail

namespace rapidfuzz::fuzz
{
template <typename InputIt1, typename InputIt2>
ScoreAlignment<double> partial_ratio_alignment(InputIt1 first1, InputIt1 last1, InputIt2 first2,
                                               InputIt2 last2, double score_cutoff)
{
	auto len1 = static_cast<size_t>(std::distance(first1, last1));
	auto len2 = static_cast<size_t>(std::distance(first2, last2));

	if (len1 > len2)
	{
		ScoreAlignment<double> result = partial_ratio_alignment(first2, last2, first1, last1, score_cutoff);
		std::swap(result.src_start, result.dest_start);
		std::swap(result.src_end, result.dest_end);
		return result;
	}

	if (score_cutoff > 100)
		return ScoreAlignment<double>(0, 0, len1, 0, len1);

	if (!len1 || !len2)
		return ScoreAlignment<double>(static_cast<double>(len1 == len2) * 100.0, 0, len1, 0, len1);

	auto s1 = detail::Range(first1, last1);
	auto s2 = detail::Range(first2, last2);

	auto alignment = fuzz_detail::partial_ratio_impl(s1, s2, score_cutoff);
	if (alignment.score != 100 && s1.size() == s2.size())
	{
		score_cutoff = std::max(score_cutoff, alignment.score);
		auto alignment2 = fuzz_detail::partial_ratio_impl(s2, s1, score_cutoff);
		if (alignment2.score > alignment.score)
		{
			std::swap(alignment2.src_start, alignment2.dest_start);
			std::swap(alignment2.src_end, alignment2.dest_end);
			return alignment2;
		}
	}

	return alignment;
}

template <typename Sentence1, typename Sentence2>
ScoreAlignment<double> partial_ratio_alignment(const Sentence1& s1, const Sentence2& s2, double score_cutoff)
{
	return partial_ratio_alignment(detail::to_begin(s1), detail::to_end(s1), detail::to_begin(s2),
	                               detail::to_end(s2), score_cutoff);
}

template <typename InputIt1, typename InputIt2>
double partial_ratio(InputIt1 first1, InputIt1 last1, InputIt2 first2, InputIt2 last2, double score_cutoff)
{
	return partial_ratio_alignment(first1, last1, first2, last2, score_cutoff).score;
}

template <typename Sentence1, typename Sentence2>
double partial_ratio(const Sentence1& s1, const Sentence2& s2, double score_cutoff)
{
	return partial_ratio_alignment(s1, s2, score_cutoff).score;
}
}




template <typename CH>
double rapidfuzz__fuzz__partial_ratio(std::basic_string_view<CH> string, std::basic_string_view<CH> pattern,
                                      double score_cutoff)
{
	return rapidfuzz::fuzz::partial_ratio(string, pattern, score_cutoff);
}
template double rapidfuzz__fuzz__partial_ratio(std::basic_string_view<char> string,
                                               std::basic_string_view<char> pattern, double score_cutoff);
template double rapidfuzz__fuzz__partial_ratio(std::basic_string_view<wchar_t> string,
                                               std::basic_string_view<wchar_t> pattern, double score_cutoff);


