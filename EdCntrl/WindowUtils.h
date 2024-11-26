#pragma once

#include "WTString.h"

WTString GetWindowTextString(HWND h);
WTString GetWindowClassString(HWND h);
HWND GetDescendant(HWND h, int id); // Recursive GetDlgItem
CStringW GetClipboardText(HWND hWnd);
WTString GetClipboardRtf(HWND hWnd);
void SaveToClipboard(HWND h, const CStringW& txt);
void GetWindowTextW(HWND h, CStringW& text);
BOOL VsScrollbarTheme(HWND hWnd, BOOL enable = TRUE);
// returns -1 if msgBox not displayed
int OneTimeMessageBox(LPCTSTR regItemName, LPCTSTR text, UINT msgBoxType = MB_OK | MB_ICONINFORMATION,
                      HWND parent = NULL);
void DisableOneTimeMessageBox(LPCTSTR regItemName);

BOOL mySetProp(HWND hwnd, const char* prop, HANDLE data);
HANDLE myGetProp(HWND hwnd, const char* prop);
inline HANDLE myGetProp(HWND hwnd, const WTString& prop)
{
	return myGetProp(hwnd, prop.c_str());
}
HANDLE myRemoveProp(HWND hwnd, const char* prop);

UINT_PTR RegisterTimer(LPCSTR timer_name);
void SetClipText(const WTString& txt, UINT type = CF_UNICODETEXT);

LPCTSTR GetDefaultVaWndCls(LPCTSTR clsName = nullptr);

bool MoveWindowIfNeeded(HWND hWnd, LPCRECT rect, bool repaint = true);
bool MoveWindowIfNeeded(HWND hWnd, int x, int y, int width, int height, bool repaint = true);

#ifdef _MFC_VER
bool MoveWindowIfNeeded(CWnd * pWnd, LPCRECT rect, bool repaint = true);
bool MoveWindowIfNeeded(CWnd* pWnd, int x, int y, int width, int height, bool repaint = true);
#endif

// set number of ticks from now to eat beeps
extern DWORD g_IgnoreBeepsTimer;

extern "C"
{
	// Define undocumented User32 function
	int WINAPI MessageBoxTimeoutA(IN HWND hWnd, IN LPCSTR lpText, IN LPCSTR lpCaption, IN UINT uType,
	                              IN WORD wLanguageId, IN DWORD dwMilliseconds);
	int WINAPI MessageBoxTimeoutW(IN HWND hWnd, IN LPCWSTR lpText, IN LPCWSTR lpCaption, IN UINT uType,
	                              IN WORD wLanguageId, IN DWORD dwMilliseconds);
}

class CDimmer
{
	HWND m_wnd = 0;
	BYTE m_min_alpha = 0;
	BYTE m_max_alpha = 0;
	BYTE m_step = 0;
	bool m_wasnt_layered = false;

	UINT_PTR timer_id();

  public:
	typedef std::function<bool()> Func;
	Func m_fnc;

	virtual ~CDimmer()
	{
		StopDimmer();
	}

	bool IsDimmerActive() const
	{
		return m_wnd != nullptr;
	}

	void StartDimmer(HWND wnd, BYTE min_alpha = 70, BYTE max_alpha = 255, DWORD timer_interval = 20,
	                 DWORD dim_steps = 10, Func fnc = nullptr);
	void StopDimmer(bool remove_layered_flag_if_wasnt_layered = false);

	void CallOnTimer(UINT_PTR nTimer);
};

class StopWatch
{
	LARGE_INTEGER frequency;
	BOOL is_hires;
	long long start;

  public:
	StopWatch();

	long long GetTimeStamp();
	void Restart();
	bool IsHighResolution();
	long long ElapsedMilliseconds();
};

class CWndSubclassComCtl
{
  protected:
	static LRESULT CALLBACK SubclassProc(HWND hWnd, UINT message, WPARAM wParam,
	                                     LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);

	static LRESULT DefProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
	static BOOL SetSubclass(_In_ HWND hWnd, _In_ UINT_PTR uIdSubclass, _In_ CWndSubclassComCtl* dwRefData);
	static BOOL GetSubclass(_In_ HWND hWnd, _In_ UINT_PTR uIdSubclass, _Out_opt_ CWndSubclassComCtl** pdwRefData);
	static BOOL RemoveSubclass(_In_ HWND hWnd, _In_ UINT_PTR uIdSubclass);

	HWND m_hWnd = nullptr;

	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);
	virtual UINT_PTR GetIdSubclass()
	{
		return (UINT_PTR)this;
	}

	bool SubclassImpl(HWND hWnd);
	bool UnsubclassImpl();

  public:
	CWndSubclassComCtl() = default;
	CWndSubclassComCtl(const CWndSubclassComCtl&) = delete;            // non construction-copyable
	CWndSubclassComCtl& operator=(const CWndSubclassComCtl&) = delete; // non copyable

	virtual ~CWndSubclassComCtl()
	{
		Unsubclass();
	}

	HWND GetHwnd()
	{
		return m_hWnd;
	}

	bool Subclass(HWND hWnd);
	bool Unsubclass();
};

#ifdef UNICODE
#define MessageBoxTimeout MessageBoxTimeoutW
#else
#define MessageBoxTimeout MessageBoxTimeoutA
#endif
