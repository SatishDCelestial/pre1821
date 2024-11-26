#pragma once

class WTString;
#include <atlstr.h>

extern const char *itoa(int i);
extern WTString itos(int i);
extern CStringW itosw(int i);
extern WTString uptrtos(uintptr_t i);
extern WTString utos(UINT i);
extern CStringW utosw(UINT i);
extern WTString hextos(int i);
extern int atox(const char *hexstr);
extern UINT atou(const char *str);
extern int wtox(const wchar_t *hexstr);
