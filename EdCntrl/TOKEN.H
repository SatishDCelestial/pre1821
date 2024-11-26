#ifndef INCTOKENH
#define INCTOKENH

#include "OWLDefs.h"
#include "bc/cstring.h"
#include "bc/regexp.h"
#include "bc/classlib/stdtempl.h"
#include "WTstring.h"
#include "timer.h"
#include <unordered_set>

extern const WTString SPACESTR;

extern const OWL::string OWL_TOKEN_IFS;
extern const OWL::string OWL_SPACESTR;
extern const OWL::string OWL_NULLSTR;

#define OWL_CLRSTR(s)                                                                                                  \
	if (s.length())                                                                                                    \
	s = OWL_NULLSTR

// don't use this class in any new places
// it is used only by MultiParse::AddMacro
class TResWords
{
	std::unordered_set<std::string> mResWords;

  public:
	TResWords();
	~TResWords() = default;

	int IsReserved(LPCSTR sym)
	{
		return mResWords.end() != mResWords.find(std::string(sym));
	}
};

// don't use in any new places
// it is used only by MultiParse::AddMacro
extern TResWords* g_ReservedWords;

class token
{
  public:
	OWL::string ifs, str, beg, match, end;

	token() : ifs(OWL_TOKEN_IFS)
	{
	}
	token(const char* s) : str(s)
	{
	}
	token(const OWL::string& s) : str(s)
	{
	}
	token(const WTString& s) : str(s.c_str())
	{
	}

	[[nodiscard]] const char* c_str() const
	{
		return str.c_str();
	}
	[[nodiscard]] OWL::string SubStr(OWL::TRegexp& exp, size_t* pStartIdx = NULL) const;
	OWL::string Strip(const OWL::TRegexp& exp);
	void StripAll(const char* from, const char* to);
	[[nodiscard]] OWL::string& OwlStr()
	{
		return str;
	}
	[[nodiscard]] WTString Str()
	{
		return WTString(str.c_str());
	}
	int ReplaceAll(const OWL::TRegexp& exp, const OWL::string& s);
	bool ReplaceAllRegex(const CStringW& regex, const CStringW& s, bool optimize = true, bool collate = false,
	                     bool ignoreCase = true);
	int ReplaceAll(LPCSTR from, LPCSTR to, bool wholewd = false);
	int ReplaceAll(const WTString& from, const WTString& to, bool wholewd = false)
	{
		return ReplaceAll(from.c_str(), to.c_str(), wholewd);
	}
	int ReplaceAll(const OWL::TRegexp& exp, void* user_data,
	               bool (*fnc)(void* user_data, const OWL::string& match, OWL::string& replacement));
	void operator=(const OWL::string& s)
	{
		str = s;
	}
	void operator=(const char* s)
	{
		str = s;
	}
	void operator=(const WTString& s)
	{
		str = s.c_str();
	}
	[[nodiscard]] int more()
	{
		return (int)str.length();
	}
	[[nodiscard]] int length() const
	{
		return (int)str.length();
	}

	OWL::string& peek(const char* s = NULL)
	{
		if (s)
			ifs = s;
		size_t p1 = str.find_first_not_of(ifs);
		if (p1 != NPOS)
		{
			size_t p2 = str.find_first_of(ifs, p1 + 1);
			beg = str.substr(0, p1);
			if (p2 == NPOS)
			{
				match = str.substr(p1);
				OWL_CLRSTR(end);
			}
			else
			{
				match = str.substr(p1, p2 - p1);
				end = str.substr(p2);
			}
		}
		else
		{
			OWL_CLRSTR(match);
			OWL_CLRSTR(end);
			OWL_CLRSTR(beg);
		}
		return match;
	}
	[[nodiscard]] OWL::string& last()
	{
		return match;
	}
	[[nodiscard]] WTString read(const char* s = NULL)
	{
		DEFTIMER(tRead);
		peek(s);
		str = end;
		return WTString(match.c_str());
	}
	[[nodiscard]] WTString readArg();
};
#endif
