#pragma once

#include "stdafxed.h"
#include <memory>

class ExpansionData;

class VsSnippetManager
{
  public:
	VsSnippetManager();
	~VsSnippetManager();

	void Refresh();
	int GetCount() const;
	WTString GetTitle(int idx) const;
	int GetItemIndex(const char* title) const;

	// pass in pos set to -1 the first time
	WTString FindNextShortcutMatch(const WTString& potentialShortcut, int& pos);
	void AddToExpansionData(const WTString& potentialShortcut, ExpansionData* lst, BOOL addAll = FALSE);

  private:
	struct VsSnippetState;
	std::unique_ptr<VsSnippetState> mState;
};

extern VsSnippetManager* gVsSnippetMgr;
