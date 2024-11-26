#pragma once

class WTString;

extern BOOL FPSSpell(LPCSTR str, CStringList* plst = NULL);
extern void FPSAddWord(LPCSTR text, BOOL ignore);
extern bool FPSSpellSurroundingAmpersandWord(const char* stream, int start_index, int end_index, const WTString& cwd);

#pragma pack(push, 1)
class _Item
{
	_Item* next = nullptr;
	uint8_t flags = 0;
	char str[1];

  public:
	void operator delete(void* p)
	{
		free(p);
	}

	static _Item* create(LPCSTR str, BYTE flags = 0)
	{
		assert(str);
		static_assert((sizeof(_Item) - sizeof(next)) == 2); // check there's no padding

		return new (malloc(sizeof(_Item) + strlen(str))) _Item(str, flags);
	}
	~_Item() = default;

  private:
	_Item(LPCSTR _str, BYTE _flags = 0)
	{
		flags = _flags;
		assert(str);
		strcpy(str, _str);
	}

  public:
	void SetFlag(BYTE _flags)
	{
		flags |= _flags;
	}
	LPSTR GetStr()
	{
		return str;
	}
	BYTE GetFlag()
	{
		return flags;
	}
	void SetNext(_Item* nxt)
	{
		next = nxt;
	}
	_Item* GetNext()
	{
		return next;
	}
};
#pragma pack(pop)

#define LISTSZ 1024
class WTHashList
{
	int nEntries;
	_Item* pRows[LISTSZ];

  public:
	WTHashList();
	~WTHashList();
	_Item* Find(LPCSTR str);
	_Item* Add(LPCSTR str, int flag = 0);

  private:
	void Remove(LPCSTR str);
	void Flush();
};
