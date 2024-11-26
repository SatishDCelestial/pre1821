#pragma once

#include <list>
#include "FindReferences.h"

#ifdef RAD_STUDIO
#include "RadStudio/RadStudioFrame.h"
#endif

class FindUsageDlg;
class FindReferencesThread;
class FindReferences;

class FindReferencesResultsFrame 
#ifndef RAD_STUDIO
	: public CWnd
#else
    : public VaRSFrameSubclass<CWnd>
#endif // RAD_STUDIO
{
	friend class SecondaryResultsFrameVs;
	friend class SecondaryResultsFrameVc6;

  public:
	virtual ~FindReferencesResultsFrame();

	DWORD GetFoundUsageCount() const;
	virtual void FocusFoundUsage();
	virtual bool IsPrimaryResultAndIsVisible() const
	{
		return false;
	}
	virtual bool HidePrimaryResults()
	{
		return false;
	}
	bool IsWindowFocused() const;
	virtual bool IsOkToClone() const = 0;
	virtual LPCTSTR GetCaption() = 0;
	void OnSize();
	bool IsThreadRunning() const;
	void ThemeUpdated();
	bool Cancel();
	bool WaitForThread(DWORD waitLen);

	void DocumentModified(const WCHAR* filenameIn, int startLineNo, int startLineIdx, int oldEndLineNo,
	                      int oldEndLineIdx, int newEndLineNo, int newEndLineIdx, int editNo);
	void DocumentSaved(const WCHAR* filename);
	void DocumentClosed(const WCHAR* filename);

	// called from package for IDE command integration
	DWORD QueryStatus(DWORD cmdId) const;
	HRESULT Exec(DWORD cmdId);

#ifdef RAD_STUDIO
	void RS_OnParentChanged(HWND newParent) override
	{
		__super::RS_OnParentChanged(newParent);
		OnSize();
	}
#endif // RAD_STUDIO

  protected:
	FindReferencesResultsFrame();

	bool CommonCtor(HWND hResultsPane);
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);

	// access to FindUsageDlg members from derived frame classes
	void SetRefFlags(int flags);
	WTString GetRefSym() const;
	void FindSymbol(const WTString& sym, int imgIdx);
	bool RefsResultIsCloned() const;
	const FindReferences& GetReferences() const;
	FindReferencesThread* GetReferencesThread() const;

	FindUsageDlg* mResultsWnd;
	WTString mCaption;
};

class PrimaryResultsFrame : public FindReferencesResultsFrame
{
  protected:
	PrimaryResultsFrame(int flags, int typeImageIdx, LPCSTR symScope);
	PrimaryResultsFrame(const FindReferences& refs);
	virtual ~PrimaryResultsFrame();

	virtual bool IsPrimaryResultAndIsVisible() const;
	virtual bool IsOkToClone() const;
	virtual LPCTSTR GetCaption();
};

class PrimaryResultsFrameVs : public PrimaryResultsFrame
{
  public:
	PrimaryResultsFrameVs(int flags, int typeImageIdx, LPCSTR symScope)
	    : PrimaryResultsFrame(flags, typeImageIdx, symScope)
	{
	}
	PrimaryResultsFrameVs(const FindReferences& refs) : PrimaryResultsFrame(refs)
	{
	}

	~PrimaryResultsFrameVs();
};

class PrimaryResultsFrameVc6 : public PrimaryResultsFrame
{
  public:
	PrimaryResultsFrameVc6(int flags, int typeImageIdx, LPCSTR symScope)
	    : PrimaryResultsFrame(flags, typeImageIdx, symScope)
	{
	}
	PrimaryResultsFrameVc6(const FindReferences& refs) : PrimaryResultsFrame(refs)
	{
	}

	virtual void FocusFoundUsage();
	virtual bool HidePrimaryResults();

  private:
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);

	// leave search results intact, attach to new parent window
	void Reparent();
};

class SecondaryResultsFrame : public FindReferencesResultsFrame
{
  protected:
	SecondaryResultsFrame() : FindReferencesResultsFrame()
	{
	}

	virtual bool IsOkToClone() const
	{
		return false;
	}
	virtual LPCTSTR GetCaption();
};

class SecondaryResultsFrameVs : public SecondaryResultsFrame
{
  public:
	SecondaryResultsFrameVs(const FindReferencesResultsFrame* refsToCopy);
	virtual ~SecondaryResultsFrameVs();
};

class SecondaryResultsFrameVc6 : public SecondaryResultsFrame
{
  public:
	SecondaryResultsFrameVc6(CWnd* vaFrameParent, const FindReferencesResultsFrame* refsToCopy);
};

extern FindReferencesResultsFrame* gActiveFindRefsResultsFrame;
