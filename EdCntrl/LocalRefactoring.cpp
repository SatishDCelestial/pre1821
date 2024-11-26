#include "StdAfxEd.h"
#include "LocalRefactoring.h"
#include "EDCNT.H"
#include "WTString.h"
#include "VAParse.h"
#include "StringUtils.h"
#include "DBQuery.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

// CPP means C++, not the extension so it allows any C++ or CS files
BOOL LocalRefactoring::IsCPPCSFileAndInsideFunc()
{
	EdCntPtr ed(g_currentEdCnt);
	if (!ed)
		return FALSE;

	if (!(Is_C_CS_File(ed->m_ftype)))
	{
		if (!(Is_C_CS_File(ed->m_ScopeLangType)))
			return FALSE;
	}

	WTString scope = ed->m_lastScope;
	if (-1 == scope.Find('-'))
		return FALSE; // only allow within functions

	return TRUE;
}

// CPP means C++, not the extension so it allows any C++ files
BOOL LocalRefactoring::IsCPPFileAndInsideFunc()
{
	EdCntPtr ed(g_currentEdCnt);
	if (!ed)
		return FALSE;

	if (!(IsCFile(ed->m_ftype)))
	{
		if (!(IsCFile(ed->m_ScopeLangType)))
			return FALSE;
	}

	WTString scope = ed->m_lastScope;
	if (-1 == scope.Find('-'))
		return FALSE; // only allow within functions

	return TRUE;
}

int LocalRefactoring::FindOpeningBrace(int line, WTString& fileBuf, int& curPos)
{
	EdCntPtr ed(g_currentEdCnt);
	fileBuf = ed->GetBuf();
	curPos = ed->GetBufIndex(fileBuf, (long)ed->CurPos());
	const int startPos = ed->GetBufIndex(fileBuf, ed->LineIndex(line));
	return ::FindInCode(fileBuf, '{', ed->m_ftype, startPos);
}

// get rid of the -234 parts (minus and numbers)
WTString GetCleanScope(WTString scope)
{
	for (int i = 0; i < scope.GetLength(); i++)
	{
		if (scope[i] == '-')
		{
			int cutPos = i;
			int cutEndPos = i + 1;
			for (int j = i + 1; j < scope.GetLength(); j++)
			{
				if (scope[j] >= '0' && scope[j] <= '9')
					cutEndPos = j + 1;
				else
					break;
			}
			scope = scope.Left(cutPos) + scope.Right(scope.GetLength() - cutEndPos);
			i--;
		}
	}

	return scope;
}

WTString GetReducedScope(const WTString& scope)
{
	int colonPos = scope.ReverseFind(":");
	if (colonPos == -1)
		return scope;
	else
		return WTString(scope.Left(colonPos));
}

DTypePtr SelectFromOverloadedMethods(DType* method, int ln)
{
	EdCntPtr ed(g_currentEdCnt);
	if (ed)
	{
		MultiParsePtr mp = ed->GetParseDb();
		if (mp)
		{
			WTString symName = method->Sym();
			std::vector<DType*> v;
			WTString bcl = mp->GetBaseClassList(mp->m_baseClass);
			DBQuery query(mp);
			WTString scope = method->Scope();
			query.FindAllSymbolsInScopeAndFileList(scope.c_str(), bcl.c_str(), method->FileId());

			for (DType* dt = query.GetFirst(); dt; dt = query.GetNext())
			{
				if (dt->type() != FUNC)
					continue;

				// if (dt->FileId() != method->FileId())
				//	continue;

				if (dt->Sym() != symName)
					continue;

				v.push_back(dt);
			}

			int closestLn = INT_MAX;
			DType* closestMethod = nullptr;
			for (DType* i : v)
			{
				int iLn = i->Line();
				if (iLn <= ln &&
				    std::abs(ln - i->Line()) <
				        std::abs(closestLn -
				                 i->Line())) // if the iterated method's beginning is above the cursor's line number AND
				                             // the distance from the cursor is the lowest so far
				{
					closestLn = i->Line();
					closestMethod = i;
				}
			}

			if (closestMethod)
			{
				return std::make_shared<DType>(closestMethod);
			}
		}
	}

	return nullptr;
}

extern DTypePtr GetMethod(MultiParse* mp, WTString name, WTString scope, WTString baseClassList,
                          WTString* methodScope /*=nullptr*/, int ln /*= -1*/)
{
	_ASSERTE(mp);
	DType* sym = mp->FindSym(&name, &scope, &baseClassList);
	if (sym && sym->IsMethod())
		return std::make_shared<DType>(sym);

	DType* method = nullptr;
	do
	{
		scope = GetReducedScope(scope);
		// WTString name = GetInnerScopeName(scope);
		name = GetCleanScope(scope);
		WTString reducedScope = GetReducedScope(scope);
		if (reducedScope.IsEmpty())
			reducedScope = ":";
		if (name.IsEmpty())
			break;
		sym = mp->FindSym(&name, &reducedScope, &baseClassList);
		if (method && sym &&
		    (sym->MaskedType() == CLASS || sym->MaskedType() == STRUCT || sym->MaskedType() == NAMESPACE ||
		     sym->Attributes() & V_CONSTRUCTOR))
			break;
		if (sym && sym->IsMethod())
		{
			method = sym;
			if (methodScope)
				*methodScope = reducedScope;
		}
	} while (scope.GetLength());

	if (method && ln != -1) // we request to resolve overloads based on curPos
	{
		if (DTypePtr m = SelectFromOverloadedMethods(method, ln))
			return m;
	}

	return std::make_shared<DType>(method);
}

WTString GetQualification(const WTString& type)
{
	for (int i = type.GetLength() - 2; i >= 0; i--)
	{
		if (type[i] == ':' && type[i + 1] == ':')
			return type.Left(i);
		if (type[i] == '.')
			return type.Left(i);
	}

	return "";
}

bool IsSubStringPlusNonSymChar(const WTString& buf, const WTString& subString, int pos)
{
	int length = subString.GetLength();
	if (pos + length >= buf.GetLength()) // we need to fit buf len + 1 for the WS check
		return false;

	for (int i = 0; i < length; i++)
	{
		if (buf[pos + i] != subString[i])
			return false;
	}

	if (ISCSYM(buf[pos + length]))
		return false;

	return true;
}

WTString GetFileNameTruncated(CStringW path)
{
	int token1 = path.ReverseFind(L'\\');
	int token2 = path.ReverseFind(L'/');
	int pos = std::max(token1, token2);

	CStringW res;
	if (pos == -1)
		res = path;
	else
		res = path.Mid(pos + 1);

	if (res.GetLength() > 32)
	{
		pos = res.Find(L'.');
		if (-1 == pos || pos < 10)
		{
			res = res.Left(30);
			res += L"...";
		}
		else
		{
			// try to truncate in basename before the file extension
			CStringW right(L"...");
			right += res.Mid(pos - 5);              // 5 chars before the file extension
			res = res.Left(33 - right.GetLength()); // max len of 33 chars including ...
			res += right;
		}
	}

	return WTString(res);
}
