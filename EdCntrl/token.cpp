#include "stdafxed.h"
#include "token.h"
#include "log.h"
#include "MParse.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

using OWL::string;
using OWL::TRegexp;

TResWords* g_ReservedWords = NULL;
const WTString NULLSTR("");
const WTString COLONSTR(":");
const WTString SPACESTR(" ");

const string OWL_TOKEN_IFS("\n\r \t");
const string OWL_SPACESTR(" ");
const string OWL_NULLSTR("");

TResWords::TResWords()
{
	LOG2("TResWords ctor");
	// don't modify this list -- it is used only by MultiParse::AddMacro
	// to prevent redefinition of the words
	mResWords.insert("char");
	mResWords.insert("struct");
	mResWords.insert("class");
	mResWords.insert("unsigned");
	mResWords.insert("typedef");
	mResWords.insert("public");
	mResWords.insert("private");
	mResWords.insert("protect");
	mResWords.insert("protected");
#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
	mResWords.insert("__published");
#endif
	mResWords.insert("internal");
	mResWords.insert("static");
	mResWords.insert("word");
	mResWords.insert("reserved");
	mResWords.insert("return");
	mResWords.insert(":");
	mResWords.insert("enum");
	mResWords.insert("switch");
	mResWords.insert("if");
	mResWords.insert("for");
	mResWords.insert("PASCAL");
	mResWords.insert("CALLBACK");
	mResWords.insert("FALL");
	mResWords.insert("STDCALL");
	mResWords.insert("__stdcall");
	mResWords.insert("__RPC_FAR");
	mResWords.insert("long");
	mResWords.insert("int");
	mResWords.insert("#define");
	mResWords.insert("void");
	mResWords.insert("HIDETHIS");
	mResWords.insert("inherits");
	mResWords.insert("operator");
	mResWords.insert("interface");
	mResWords.insert("nullptr");
}

string token::Strip(const TRegexp& exp)
{
	string s;
	if (exp.status() != TRegexp::OK)
		return s;
	size_t p, len;
	if ((p = str.find(exp, &len, 0)) != NPOS)
	{
		s = str.substr(p, len);
		str = str.replace(p, len, OWL_NULLSTR);
	}
	return s;
}

WTString token::readArg()
{
	int inbrc = 0;
	LPCSTR p = str.c_str();
	while (*p)
	{
		switch (*p)
		{
		case '"':
			while (p[1] && p[1] != '"')
			{
				if (p[1] == '\\' && p[2])
					p++;
				p++;
			}
			break;
		case '(':
		case '{':
		case '[':
			inbrc++;
			break;
		case ')':
		case '}':
		case ']':
			if (!inbrc)
				goto done;
			inbrc--;
			break;
		case ';':
		case ',':
			if (!inbrc)
				goto done;
			break;
		}
		p++;
	}
	return (NULLSTR);
done:
	size_t sz = ptr_sub__uint(p, str.c_str());
	if (!sz)
		return NULLSTR;
	WTString rstr = str.substr(0, sz).c_str();
	str = str.substr(sz + 1);
	return rstr;
}

string token::SubStr(TRegexp& exp, size_t* pStartIdx /*= NULL*/) const
{
	string s;
	if (exp.status() != TRegexp::OK)
		return s;
	size_t p, len;
	size_t startIdx = pStartIdx ? *pStartIdx : 0;
	if ((p = str.find(exp, &len, startIdx)) != NPOS)
	{
		if (pStartIdx)
			*pStartIdx = p;
		s = str.substr(p, len);
	}
	return s;
}

int token::ReplaceAll(const TRegexp& exp, const string& s)
{
	DEFTIMER(tReplaceAllTimer);
	if (exp.status() != TRegexp::OK)
		return 0;
	size_t p = 0, len, count = 0;
	while ((p = str.find(exp, &len, p)) != NPOS)
	{
		str = str.replace(p, len, s);
		count++;
		p += s.length();
	}
	return (int)count;
}

bool token::ReplaceAllRegex(const CStringW& regexStr, const CStringW& s, bool optimize /*= true*/,
                            bool collate /*= false*/, bool ignoreCase /*= true*/)
{
	DEFTIMER(tReplaceAllRegexTimer);
	std::wstring _thisStr((LPCWSTR)WTString(c_str()).Wide());

	auto flags = std::wregex::ECMAScript;

	if (collate)
		flags |= std::wregex::collate;

	if (optimize)
		flags |= std::wregex::optimize;

	if (ignoreCase)
		flags |= std::wregex::icase;

	std::wregex regex(regexStr, flags);

	std::wstring res = std::regex_replace(_thisStr, regex, std::wstring(s));
	if (res != _thisStr)
	{
		WTString wtstr(res.c_str());
		str = wtstr.c_str();
		return true;
	}

	return false;
}

int token::ReplaceAll(const OWL::TRegexp& exp, void* user_data,
                      bool (*fnc)(void* d, const OWL::string& m, OWL::string& r))
{
	DEFTIMER(tReplaceAllTimer);
	if (exp.status() != TRegexp::OK || fnc == nullptr)
		return 0;
	size_t p = 0, len, count = 0;
	string mch, rpl;
	while ((p = str.find(exp, &len, p)) != NPOS)
	{
		mch.assign(str, p, len);
		if (fnc(user_data, mch, rpl))
		{
			str = str.replace(p, len, rpl);
			count++;
			p += rpl.length();
			continue;
		}
		break;
	}
	return (int)count;
}

int token::ReplaceAll(LPCSTR from, LPCSTR to, bool wholewd)
{
	DEFTIMER(tsReplaceAllTimer);
	string exp(from), s(to);
	size_t tolen = strlen(to);
	size_t p = 0, count = 0;
	if (!from || !*from)
	{
		ASSERT(FALSE);
		return 0;
	}
	while ((p = str.find(exp, p)) != NPOS)
	{
		if (wholewd && ((p && ISALNUM(str[p - 1])) || ISALNUM(str.c_str()[p + exp.length()])))
			p++;
		else
		{
			str = str.replace(p, exp.length(), s);
			count++;
			p += tolen;
		}
		if (count > 1000000)
		{ // sanity check to prevent infinite loop
			ASSERT(FALSE);
			return (int)count;
		}
	}
	return (int)count;
}

void token::StripAll(const char* from, const char* to)
{
	size_t p = 0;
	while ((p = str.find(string(from), p)) != NPOS)
	{
		size_t p2 = str.find(string(to), p);
		str.remove(p, p2);
	}
}
