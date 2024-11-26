#pragma once
#include "VaMessages.h"
#include "assert_once.h"
#include "WindowUtils.h"

extern BOOL g_PaintLock;

struct PaintType
{
	enum
	{
		None,
		DontColor,
		SourceWindow,
		ObjectBrowser,
		ToolTip,
		VS_ToolTip,
		View,
		ListBox,
		WizardBar,
		FindInFiles,
		OutputWindow,
		AssemblyView
	};
	static int in_WM_PAINT;
	static int inPaintType;
	static int SetPaintType(int type);
};

#define IsPaintType(type) (PaintType::inPaintType == type)
class VAColorPaintMessages
{
	BOOL m_isPaint;

  public:
	VAColorPaintMessages(int type)
	{
		m_isPaint = TRUE;
		PaintType::in_WM_PAINT = type;
	}
	VAColorPaintMessages(UINT message, int type)
	{
		m_isPaint = (message == WM_PAINT || message == WM_DRAWITEM);
		if (m_isPaint)
			PaintType::in_WM_PAINT = type;
	}

	VAColorPaintMessages(HWND h, UINT message, int type)
	{
		m_isPaint = (message == WM_PAINT || message == WM_DRAWITEM);
		if (m_isPaint)
		{
			if (::myGetProp(h, "__VA_do_not_colour"))
				PaintType::in_WM_PAINT = PaintType::DontColor;
			else
				PaintType::in_WM_PAINT = type;
		}
	}

	~VAColorPaintMessages()
	{
		if (m_isPaint)
			PaintType::in_WM_PAINT = NULL;
	}
};

class TempPaintOverride
{
	bool mOverride;
	int mPrevInWmPaint;
	int mPrevInPaintType;

  public:
	TempPaintOverride(bool override, int type = PaintType::DontColor, int inWmPaint = 0) : mOverride(override)
	{
		if (!mOverride)
			return;

		mPrevInPaintType = PaintType::inPaintType;
		mPrevInWmPaint = PaintType::in_WM_PAINT;
		PaintType::inPaintType = type;
		PaintType::in_WM_PAINT = inWmPaint;
	}

	~TempPaintOverride()
	{
		if (!mOverride)
			return;

		PaintType::inPaintType = mPrevInPaintType;
		PaintType::in_WM_PAINT = mPrevInWmPaint;
	}
};

class ColorInstCounter
{
	static long s_WrapperCount;

  public:
	ColorInstCounter()
	{
		long n = InterlockedIncrement(&s_WrapperCount); // Ensure they are getting cleaned
		ASSERT_ONCE(n < 500);
		std::ignore = n;
	}
	~ColorInstCounter()
	{
		InterlockedDecrement(&s_WrapperCount);
	}
};

template <class wndClass, int type> class VAColorWrapper : public wndClass
{
  protected:
	int m_colorType;
#ifdef _DEBUG
	ColorInstCounter m_instTracker;
#endif // _DEBUG
  public:
	VAColorWrapper() : wndClass()
	{
		m_colorType = type;
	}
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
	{
		if (message == VAM_ISSUBCLASSED)
			return TRUE;
		VAColorPaintMessages w(message, m_colorType);
		return __super::WindowProc(message, wParam, lParam);
	}
};

template <class wndClass, int type> class VAColorWrapperSubclass : public VAColorWrapper<wndClass, type>
{
  public:
	VAColorWrapperSubclass(HWND h)
	{
		__super::SubclassWindow(h);
	}
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
	{
		if (message == WM_DESTROY)
		{
			LRESULT r = __super::WindowProc(message, wParam, lParam);
			delete this;
			return r;
		}
		return __super::WindowProc(message, wParam, lParam);
	}
	~VAColorWrapperSubclass()
	{
		if (__super::m_hWnd && ::IsWindow(__super::m_hWnd))
			__super::UnsubclassWindow();
	}
};

void PatchTextOutMethods(BOOL doPatch);
extern BOOL(WINAPI* RealExtTextOutW)(HDC dc, int x, int y, UINT style, CONST RECT* rc, LPCWSTR str, UINT len,
                                     CONST INT* w);
extern BOOL(WINAPI* RealExtTextOut)(HDC dc, int x, int y, UINT style, CONST RECT* rc, LPCSTR str, UINT len,
                                    CONST INT* w);

int WPFGetSymbolColor(IVsTextBuffer* vsTextBuffer, LPCWSTR lineText, int linePos, int bufPos, int context);
UINT SpoofACP(UINT acp);

class GlyfBuffer
{
	int m_lastPrintPos;
	HDC m_dc;
	wchar_t m_p1Char;

  public:
	LPCWSTR m_p1, m_p2;
	GlyfBuffer()
	{
		Reset();
	};
	void Reset(HDC dc = NULL);
	void Add(HDC dc, LPCWSTR str, LPCWSTR glyf, int len);
	LPCWSTR GetBuf(HDC dc, LPCWSTR glyf, int len);
	wchar_t* GetGlyfXrefBuffer();
	void Cleanup();
};

extern GlyfBuffer g_GlyfBuffer;

extern const uint wm_do_multiple_dpis_workaround_cleanup;
void DoMultipleDPIsWorkaround(HWND active_hwnd);
void DoMultipleDPIsWorkaroundCleanup(WPARAM wparam);
