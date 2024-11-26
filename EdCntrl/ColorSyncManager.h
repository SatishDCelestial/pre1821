#pragma once

#include "stdafxed.h"
#include <memory>

class ColorSyncManager
{
  public:
	ColorSyncManager();
	~ColorSyncManager();

	void CompleteInit();
	void PotentialThemeChange();
	void PotentialVaColorChange();
	bool PotentialVsColorChange();
	COLORREF GetVsEditorTextFg() const;

	// see also class IdeSettings implementation of theme tracking for case 146057
	enum ActiveVsTheme
	{
		avtUnknown,
		avtLight,
		avtBlue,
		avtDark
	};
	ActiveVsTheme GetActiveVsTheme() const;

  private:
	struct SyncState;
	std::unique_ptr<SyncState> mState;
};

extern ColorSyncManager* gColorSyncMgr;
