#pragma once

#include "stdafxed.h"
#include <vector>
#include <map>
#include <memory>
#include "PooledThreadBase.h"
#include "Expansion.h"

struct VsSnippetItem
{
	VsSnippetItem()
	{
	}
	VsSnippetItem(const WTString title, const WTString shortCut, const WTString desc)
	    : mTitle(title), mShortcut(shortCut), mDescription(desc)
	{
	}

	WTString GetCompletionEntry() const
	{
		return mTitle + AUTOTEXT_SHORTCUT_SEPARATOR + mShortcut + AUTOTEXT_SHORTCUT_SEPARATOR + mDescription;
	}

	WTString mTitle;
	WTString mShortcut;
	WTString mDescription;
};

typedef std::vector<VsSnippetItem> Snippets;
typedef std::map<int, Snippets> SnippetMap;
typedef std::shared_ptr<SnippetMap> SnippetMapPtr;

class VsSnippetLoader : public PooledThreadBase
{
  public:
	VsSnippetLoader(SnippetMapPtr saveTo);

  private:
	virtual void Run() override;
	void LoadLanguageSnippets(int lang, LPCSTR regLangSubstr, LPCSTR valueName);
	CStringW RegGetSnippetPaths(LPCSTR regLangSubstr, LPCSTR valueName) const;

  private:
	CStringW mRootInstallDir;
	SnippetMapPtr mDestinationSnippets;
	SnippetMap mSnippets;
};
