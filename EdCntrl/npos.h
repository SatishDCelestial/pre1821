#pragma once

// npos replacement to play well with all signed/unsigned 32/64 types

#undef NPOS
#define NPOS __npos()
struct __npos
{
	/*explicit*/ operator int32_t() const
	{
		return int32_t(-1);
	}
	/*explicit*/ operator int64_t() const
	{
		return int64_t(-1);
	}
	/*explicit*/ operator uint32_t() const
	{
		return uint32_t(-1);
	}
	/*explicit*/ operator uint64_t() const
	{
		return uint64_t(-1);
	}
	/*explicit*/ operator long() const
	{
		return long(-1);
	}
	/*explicit*/ operator ulong() const
	{
		return ulong(-1);
	}
};
#define nops_operator(op, type)                                                                                        \
	inline bool operator op(__npos, type value)                                                                        \
	{                                                                                                                  \
		return value op type(-1);                                                                                      \
	}                                                                                                                  \
	inline bool operator op(type value, __npos)                                                                        \
	{                                                                                                                  \
		return value op type(-1);                                                                                      \
	}
#define nops_operators(type) nops_operator(==, type) nops_operator(!=, type)

nops_operators(int32_t) nops_operators(int64_t) nops_operators(uint32_t) nops_operators(uint64_t) nops_operators(long)
    nops_operators(ulong)

#undef nops_operator
#undef nops_operators
