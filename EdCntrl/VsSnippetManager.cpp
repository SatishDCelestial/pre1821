#include "stdafxed.h"
#include "VsSnippetLoader.h"
#include "VsSnippetManager.h"
#include "DevShellAttributes.h"
#include "PROJECT.H"
#include "FileTypes.h"
#include "AutotextManager.h"
#include "EDCNT.H"
#include "IdeSettings.h"
#include "Expansion.h"
#include "AutotextExpansion.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

VsSnippetManager* gVsSnippetMgr = nullptr;

int NormalizeLang(int lang)
{
	if (IsCFile(lang))
		return Src;
	else
		return kLanguageFiletypeCount;
}

struct VsSnippetManager::VsSnippetState
{
	SnippetMapPtr mSnippets = std::make_shared<SnippetMap>();

	VsSnippetState()
	{
		Refresh();
	}

	~VsSnippetState()
	{
	}

	SnippetMap::iterator GetLangSnippets(int lang)
	{
		lang = ::NormalizeLang(lang);
		if (kLanguageFiletypeCount == lang)
			return mSnippets->end();
		if (Src == lang)
		{
			if (g_IdeSettings && g_IdeSettings->GetEditorBoolOption("C/C++ Specific", "DisableMemberListExpansions"))
				return mSnippets->end();
		}
		return mSnippets->find(lang);
	}

	SnippetMap::const_iterator GetLangSnippets(int lang) const
	{
		lang = ::NormalizeLang(lang);
		if (kLanguageFiletypeCount == lang)
			return mSnippets->cend();
		if (Src == lang)
		{
			if (g_IdeSettings && g_IdeSettings->GetEditorBoolOption("C/C++ Specific", "DisableMemberListExpansions"))
				return mSnippets->cend();
		}
		return mSnippets->find(lang);
	}

	int GetCount(int lang) const
	{
		SnippetMap::const_iterator it = GetLangSnippets(lang);
		if (it == mSnippets->end())
			return 0;

		return (int)it->second.size();
	}

	WTString GetTitle(int lang, int idx) const
	{
		SnippetMap::const_iterator it = GetLangSnippets(lang);
		if (it == mSnippets->end())
			return NULLSTR;
		if (idx >= (int)it->second.size())
			return NULLSTR;
		return it->second[(uint)idx].mTitle;
	}

	int GetItemIndex(int lang, const char* title) const
	{
		SnippetMap::const_iterator it = GetLangSnippets(lang);
		if (it == mSnippets->end())
			return -1;

		const size_t kCnt = it->second.size();
		for (size_t idx = 0; idx < kCnt; ++idx)
		{
			const VsSnippetItem& it2 = it->second[idx];
			if (it2.mTitle == title)
				return (int)idx;
		}

		return -1;
	}

#define STRCMP_AC(s1, s2) (g_doDBCase ? StrCmp(s1, s2) : StrCmpI(s1, s2)) // Case insensitive shortcuts in VB.

	WTString FindNextShortcutMatch(int lang, const WTString& potentialShortcut, int& pos) const
	{
		if (gExpSession != nullptr)
		{
			// [case: 88949]
			return NULLSTR;
		}

		SnippetMap::const_iterator it = GetLangSnippets(lang);
		if (it == mSnippets->end())
			return NULLSTR;

		const WTString potentialShortcutExt = ::GetPotentialShortcutExt(potentialShortcut);
		const int kCnt = int(it->second.size());
		_ASSERTE((pos == -1 || pos < kCnt) && potentialShortcut.c_str());
		for (pos++; pos < kCnt; ++pos)
		{
			const VsSnippetItem& it2 = it->second[(uint)pos];
			WTString shortCut(it2.mShortcut);
			if (!shortCut.IsEmpty() && STRCMP_AC(shortCut.c_str(), potentialShortcut.c_str()) == 0)
			{
				// don't suggest alpha snippets in comments or strings.
				if (!ISALPHA(potentialShortcut[0]) ||
				    (g_currentEdCnt && g_currentEdCnt->m_lastScope.c_str()[0] == DB_SEP_CHR))
					return it2.mTitle;
			}
			else if (potentialShortcutExt.GetLength() && STRCMP_AC(shortCut.c_str(), potentialShortcutExt.c_str()) == 0)
				return it2.mTitle;
		}

		pos = -1;
		return NULLSTR;
	}

	void AddToExpansionData(int lang, const WTString& potentialShortcut, ExpansionData* lst, BOOL addAll) const
	{
		if (gExpSession != nullptr)
		{
			// [case: 88949]
			return;
		}

		{
			EdCntPtr ed(g_currentEdCnt);
			if (ed && !Psettings->mAllowSnippetsInUnrealMarkup)
			{
				ScopeInfoPtr si(ed->ScopeInfoPtr());
				if (si->HasUe4SuggestionMode())
				{
					// [case: 111552]
					return;
				}
			}
		}

		SnippetMap::const_iterator it = GetLangSnippets(lang);
		if (it == mSnippets->end())
			return;

		int itemsAdded = 0;
		const WTString potentialShortcutExt = ::GetPotentialShortcutExt(potentialShortcut);
		size_t pos = 0;
		const size_t kCnt = it->second.size();
		_ASSERTE(potentialShortcut.c_str());
		BOOL filterNonCSym = (!potentialShortcut.GetLength() || ISCSYM(potentialShortcut[0]));
		for (; pos < kCnt; ++pos)
		{
			const VsSnippetItem& it2 = it->second[pos];
			const WTString& curTitle(it2.mTitle);
			const WTString& shortCut(it2.mShortcut);

			if (addAll)
			{
				// Filter non-csym snippets from IDE suggestion lists. case=21971
				// Makes sense to do this for all languages. ?
				if (!filterNonCSym || ISCSYM(curTitle[0]))
				{
					lst->AddStringAndSort(it2.GetCompletionEntry(), ET_VS_SNIPPET, 0, WTHashKey(curTitle), 0);
					++itemsAdded;
				}
			}
			else if (shortCut.GetLength() && (STRCMP_AC(shortCut.c_str(), potentialShortcut.c_str()) == 0 ||
			                                  STRCMP_AC(curTitle.c_str(), potentialShortcut.c_str()) == 0))
			{
				// don't suggest alpha snippets in comments or strings.
				if (!ISALPHA(potentialShortcut[0]) ||
				    (g_currentEdCnt && g_currentEdCnt->m_lastScope.c_str()[0] == DB_SEP_CHR))
				{
					lst->AddStringAndSort(it2.GetCompletionEntry(), ET_VS_SNIPPET, 0, WTHashKey(curTitle), 0);
					++itemsAdded;
				}
			}
			else if (potentialShortcutExt.GetLength() &&
			         (STRCMP_AC(shortCut.c_str(), potentialShortcutExt.c_str()) == 0 ||
			          STRCMP_AC(curTitle.c_str(), potentialShortcutExt.c_str()) == 0))
			{
				lst->AddStringAndSort(it2.GetCompletionEntry(), ET_VS_SNIPPET, 0, WTHashKey(curTitle), 0);
				++itemsAdded;
			}
			else if (shortCut.GetLength() && potentialShortcut.GetLength() > 1 &&
			         StartsWith(shortCut, potentialShortcut, FALSE))
			{
				// different behavior than VA Snippets, include startsWith matches like dev11
				// don't suggest alpha snippets in comments or strings.
				if (!ISALPHA(potentialShortcut[0]) ||
				    (g_currentEdCnt && g_currentEdCnt->m_lastScope.c_str()[0] == DB_SEP_CHR))
				{
					lst->AddStringAndSort(it2.GetCompletionEntry(), ET_VS_SNIPPET, 0, WTHashKey(curTitle), 0);
					++itemsAdded;
				}
			}
		}
	}

	void Refresh()
	{
		new VsSnippetLoader(mSnippets);
	}
};

VsSnippetManager::VsSnippetManager() : mState(new VsSnippetState)
{
	_ASSERTE(!gVsSnippetMgr);
	_ASSERTE(gShellAttr->IsDevenv11OrHigher());
}

VsSnippetManager::~VsSnippetManager()
{
}

void VsSnippetManager::Refresh()
{
	mState->Refresh();
}

int VsSnippetManager::GetCount() const
{
	return mState->GetCount(gTypingDevLang);
}

WTString VsSnippetManager::GetTitle(int idx) const
{
	return mState->GetTitle(gTypingDevLang, idx);
}

int VsSnippetManager::GetItemIndex(const char* title) const
{
	return mState->GetItemIndex(gTypingDevLang, title);
}

WTString VsSnippetManager::FindNextShortcutMatch(const WTString& potentialShortcut, int& pos)
{
	return mState->FindNextShortcutMatch(gTypingDevLang, potentialShortcut, pos);
}

void VsSnippetManager::AddToExpansionData(const WTString& potentialShortcut, ExpansionData* lst,
                                          BOOL addAll /*= FALSE*/)
{
	mState->AddToExpansionData(gTypingDevLang, potentialShortcut, lst, addAll);
}
