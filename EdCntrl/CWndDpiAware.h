#ifndef CWndDpiAware_h__
#define CWndDpiAware_h__

#pragma once
#include "DpiCookbook\VsUIDpiAwareness.h"
#include "FontSettings.h"
#include <optional>
#include "DpiCookbook\VsUIDpiHelper.h"
#include "WindowUtils.h"
#include "ImageListManager.h"
#include "VAThemeUtils.h"

#ifndef DEFAULT_DPI
#define DEFAULT_DPI 96
#endif

class WindowScaler
{
	double m_scale;
	UINT m_swpFlags;
	std::vector<HWND> m_windows;

  public:
	static const UINT kDefaultSWPFlags = SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_FRAMECHANGED;

	WindowScaler(double scale, UINT swpFlags = kDefaultSWPFlags) : m_scale(scale), m_swpFlags(swpFlags)
	{
	}

	template <class TValue> static TValue Round(double value)
	{
		if (value >= 0)
			return (TValue)(value + 0.5);

		return (TValue)(value - 0.5);
	}

	static void RedrawWindow(HWND hWnd)
	{
		::RedrawWindow(hWnd, nullptr, nullptr,
		               RDW_INVALIDATE | RDW_VALIDATE | RDW_UPDATENOW | RDW_ERASENOW | RDW_ERASE | RDW_FRAME);
	}

	// transforms value from source space to destination space
	static LONG Transform(LONG value, LONG source, LONG destination)
	{
		return Round<LONG>(destination * ((double)value / source));
	}

	static void TransformRect(RECT& rect, const SIZE& source, const SIZE& destination)
	{
		rect.left = Transform(rect.left, source.cx, destination.cx);
		rect.top = Transform(rect.top, source.cy, destination.cy);
		rect.right = Transform(rect.right, source.cx, destination.cx);
		rect.bottom = Transform(rect.bottom, source.cy, destination.cy);
	}

	static void TransformPoint(POINT& pt, const SIZE& source, const SIZE& destination)
	{
		pt.x = Transform(pt.x, source.cx, destination.cx);
		pt.y = Transform(pt.y, source.cy, destination.cy);
	}

	template <class TValue> static TValue Scale(TValue value, double scaleFactor)
	{
		return Round<TValue>((double)value * scaleFactor);
	}

	static void ScaleSize(SIZE& value, double scaleFactor)
	{
		value.cx = Scale(value.cx, scaleFactor);
		value.cy = Scale(value.cy, scaleFactor);
	}

	static void ScaleRect(RECT& value, double scaleFactor)
	{
		value.left = Scale(value.left, scaleFactor);
		value.top = Scale(value.top, scaleFactor);
		value.right = Scale(value.right, scaleFactor);
		value.bottom = Scale(value.bottom, scaleFactor);
	}

	static void ScreenToClientSafe(HWND hWnd, LPRECT rect)
	{
		if (hWnd && ::IsWindow(hWnd))
		{
			::ScreenToClient(hWnd, (LPPOINT)&rect);
			::ScreenToClient(hWnd, ((LPPOINT)&rect) + 1);
		}
	}

	static void ClientToScreenSafe(HWND hWnd, LPRECT rect)
	{
		if (hWnd && ::IsWindow(hWnd))
		{
			::ClientToScreen(hWnd, (LPPOINT)&rect);
			::ClientToScreen(hWnd, ((LPPOINT)&rect) + 1);
		}
	}

	enum class ScaleWindowSwitch
	{
		None, // don't scale
		All,  // do scale all
		Only  // scale only
	};

	static bool ScaleWindow(HWND hWnd, UINT oldDPI, UINT newDPI, ScaleWindowSwitch children,
	                        UINT swpFlags = kDefaultSWPFlags)
	{
		if (oldDPI == newDPI)
			return true;

		if (oldDPI == 0)
			return false;

		return ScaleWindow(hWnd, (double)newDPI / oldDPI, children, swpFlags);
	}

	static bool ScaleWindow(HWND hWnd, double scaleFactor, ScaleWindowSwitch children, UINT swpFlags = kDefaultSWPFlags)
	{
		if (scaleFactor == 1)
			return true;

		if (children == ScaleWindowSwitch::None)
		{
			CRect rect;
			if (::GetWindowRect(hWnd, &rect))
			{
				ScreenToClientSafe(::GetParent(hWnd), &rect);
				ScaleRect(rect, scaleFactor);

				if (::SetWindowPos(hWnd, nullptr, rect.left, rect.top, rect.Width(), rect.Height(), swpFlags))
				{
					return true;
				}

				return false;
			}
		}
		else
		{
			struct EnumWnds
			{
				static BOOL CALLBACK Proc(HWND hwnd, LPARAM lParam)
				{
					((WindowScaler*)lParam)->AddWindow(hwnd);
					return TRUE;
				}
			};

			WindowScaler scaler(scaleFactor, swpFlags);

			if (children == ScaleWindowSwitch::All)
				scaler.AddWindow(hWnd);

			EnumChildWindows(hWnd, (WNDENUMPROC)EnumWnds::Proc, (LPARAM)&scaler);
			return scaler.Apply(true);
		}

		return false;
	}

	void AddWindow(HWND window)
	{
		if (m_scale != 1)
			m_windows.push_back(window);
	}

	void Clear()
	{
		m_windows.clear();
	}

	bool Apply(bool clear)
	{
		auto result = ScaleDeferred() || ScalePerWindow();
		if (clear)
			Clear();
		return result;
	}

  protected:
	bool ScalePerWindow()
	{
		if (m_scale == 1)
			return true;

		size_t scaled = 0;
		for (HWND hWnd : m_windows)
			if (ScaleWindow(hWnd, m_scale, ScaleWindowSwitch::None, m_swpFlags))
				scaled++;
		return scaled == m_windows.size();
	}

	bool ScaleDeferred()
	{
		if (m_scale == 1)
			return true;

		HDWP hdwp = ::BeginDeferWindowPos((int)m_windows.size());

		if (!hdwp)
			return false;

		for (HWND hWnd : m_windows)
		{
			CRect rect;
			if (::GetWindowRect(hWnd, &rect))
			{
				HWND parent = ::GetParent(hWnd);
				if (parent)
				{
					::ScreenToClient(parent, (LPPOINT)&rect);
					::ScreenToClient(parent, ((LPPOINT)&rect) + 1);
				}

				ScaleRect(rect, m_scale);

				hdwp = ::DeferWindowPos(hdwp, hWnd, HWND_TOP, rect.left, rect.top, rect.Width(), rect.Height(),
				                        m_swpFlags);

				if (!hdwp)
					return false;
			}
		}

		if (::EndDeferWindowPos(hdwp))
		{
			// 			for (HWND hWnd : m_windows)
			// 				ScaleWindowFont(hWnd, m_scale);

			return true;
		}

		return false;
	}
};

class IDpiHandler
{
	friend class CDpiHandler;

  protected:
	enum class DpiHandlerState
	{
		None,
		Attached,
		Detached
	};

	VsUI::CDpiHelper* m_helper = nullptr;
	VaFontType m_font_type = VaFontType::EnvironmentFont;
	VaFont m_font;
	DpiHandlerState m_handlerState = DpiHandlerState::None;

	virtual void AttachDpiHandler()
	{
		if (m_handlerState != DpiHandlerState::Attached)
		{
			VsUI::DpiHelper::AddHandler(this);
			m_handlerState = DpiHandlerState::Attached; // must be set BEFORE UpdateFonts call!!!

			UpdateFonts(VAFTF_All); // must be called last, because causes WMs and WindowProc calls AttachDpiHandler
		}
	}

	virtual void DetachDpiHandler()
	{
		if (m_handlerState == DpiHandlerState::Attached)
		{
			VsUI::DpiHelper::RemoveHandler(this);
			m_handlerState = DpiHandlerState::Detached;
		}
	}

  public:
	IDpiHandler() = default;

	virtual ~IDpiHandler()
	{
		DetachDpiHandler();
	}

	virtual HWND GetDpiHWND() = 0;

	virtual CWnd* GetDpiWindow()
	{
		return CWnd::FromHandle(GetDpiHWND());
	}

	const VaFont& GetDpiAwareFont() const
	{
		return m_font;
	}

	virtual VsUI::CDpiHelper* GetDpiHelper()
	{
		auto hWnd = GetDpiHWND();

		_ASSERTE(hWnd && ::IsWindow(hWnd));

		auto thrdState = AfxGetThreadState();

		if (thrdState &&
			thrdState->m_msgCur.hwnd == hWnd &&
			thrdState->m_msgCur.message == WM_DPICHANGED &&
			thrdState->m_msgCur.wParam)
		{
			if (!m_helper ||
				(int)HIWORD(thrdState->m_msgCur.wParam) != m_helper->GetDeviceDpiY())
			{
				m_helper = VsUI::DpiHelper::GetForDPI(HIWORD(thrdState->m_msgCur.wParam));					
			}

			if (m_helper)
			{
				return m_helper;				
			}
		}

		auto dpi = (int)VsUI::CDpiAwareness::GetDpiForWindow(hWnd, false);
		if (!m_helper || dpi != m_helper->GetDeviceDpiY())
		{
			m_helper = VsUI::DpiHelper::GetForDPI(dpi);
		}

		return m_helper;
	}

	std::unique_ptr<VsUI::CDpiHelperScope> SetDefaultDpiHelper() const
	{
		return VsUI::DpiHelper::SetDefaultDirect(((IDpiHandler*)this)->GetDpiHelper());
	}

	HRESULT UpdateFontObject(VaFont& fontObj, VaFontType fontType, VaFontStyle style = FS_None)
	{
		HWND hWnd = GetDpiHWND();

		if (hWnd && ::IsWindow(hWnd))
			return fontObj.UpdateForWindow(hWnd, fontType, style);

		return E_FAIL;
	}

	VaFontType GetFontType() const
	{
		return m_font_type;
	}

	void SetFontType(VaFontType fontType, bool updateFonts = true)
	{
		m_font_type = fontType;

		if (updateFonts)
			UpdateFonts(VAFTF_All);
	}

	virtual bool UpdateFonts(VaFontTypeFlags changed, UINT dpi = 0)
	{
		if (IsFontAffected(changed, m_font_type))
		{
			HWND hWnd = GetDpiHWND();

			if (!hWnd || !::IsWindow(hWnd))
				return false;

			if (!dpi)
			{
				auto * helper = GetDpiHelper();

				if (helper)
					dpi = (UINT)helper->GetDeviceDpiY();
			}

			HRESULT hr =
			    dpi ? 
				m_font.Update(dpi, m_font_type) :
				m_font.UpdateForWindow(hWnd, m_font_type);

			if (SUCCEEDED(hr))
				ApplyFont(m_font);

			return hr != E_FAIL;
		}

		return false;
	}

	void ApplyFont(HFONT font, BOOL bRedraw = TRUE)
	{
		HWND hWnd = GetDpiHWND();

		_ASSERTE(hWnd && ::IsWindow(hWnd));

		if (hWnd && ::IsWindow(hWnd))
		{
			// apply the font
			::SendMessage(hWnd, WM_SETFONT, (WPARAM)font, (LPARAM)FALSE);

			if (::IsWindowVisible(hWnd))
			{
				// force updates in window
				CRect rc;
				::GetWindowRect(hWnd, &rc);

				WINDOWPOS wp;
				ZeroMemory(&wp, sizeof(wp));
				wp.hwnd = hWnd;
				wp.cx = rc.Width();
				wp.cy = rc.Height();
				wp.flags = SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOOWNERZORDER | SWP_NOZORDER;
				::SendMessage(hWnd, WM_WINDOWPOSCHANGED, 0, (LPARAM)&wp);

				if (bRedraw)
				{
					::RedrawWindow(hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE);
				}
			}
		}
	}
};

class CDpiHandler : public IDpiHandler
{
	HWND m_dpiHWND;

  public:
	HWND GetDpiHWND() override
	{
		return m_dpiHWND;
	}

	CDpiHandler(HWND dpiHWND) : m_dpiHWND(dpiHWND)
	{
	}

	virtual ~CDpiHandler()
	{
	}

	static IDpiHandler* FromWindow(HWND hWnd)
	{
		return VsUI::DpiHelper::FindHandler(hWnd, false);
	}
};

class CDpiHandlerCached : public IDpiHandler
{
  protected:
	struct DpiCache
	{
		CPoint previousDpi = {0};
		CPoint currentDpi = {0};
//		CPoint logPixels = {0};
		HWND hWnd = nullptr;
	};

	std::unique_ptr<DpiCache> m_cache;

	VsUI::CDpiHelper* GetDpiHelper() override 
	{
		if (!m_helper)
			return __super::GetDpiHelper();

		return m_helper;
	}

	bool UpdateDPICache(bool ifChanged, WPARAM wParam = 0)
	{
		HWND hWnd = GetDpiHWND();

		if (::IsWindow(hWnd))
		{
			UINT dpiX = LOWORD(wParam), dpiY = HIWORD(wParam);

			if (wParam || SUCCEEDED(VsUI::CDpiAwareness::GetDpiForWindow(hWnd, &dpiX, &dpiY)))
			{
				if (!m_cache || !ifChanged || (UINT)m_cache->currentDpi.x != dpiX ||
				    (UINT)m_cache->currentDpi.y != dpiY)
				{
					if (!m_cache)
						m_cache = std::make_unique<DpiCache>();

					if (m_cache)
					{
						m_cache->hWnd = hWnd;
						m_cache->previousDpi = m_cache->currentDpi;
						m_cache->currentDpi.x = (LONG)dpiX;
						m_cache->currentDpi.y = (LONG)dpiY;

// 						HDC hdc = ::GetDC(hWnd);
// 						if (hdc)
// 						{
// 							m_cache->logPixels.x = ::GetDeviceCaps(hdc, LOGPIXELSX);
// 							m_cache->logPixels.y = ::GetDeviceCaps(hdc, LOGPIXELSY);
// 
// 							::ReleaseDC(hWnd, hdc);
// 						}
					}

					m_helper = VsUI::DpiHelper::GetForDPI((int)dpiX);

					return true;
				}
			}
		}

		return false;
	}

	bool ValidateDPICache()
	{
		if (VsUI::CDpiAwareness::IsPerMonitorDPIAwarenessEnabled())
		{
			if (m_cache && m_cache->hWnd == GetDpiHWND())
				return true;

			UpdateDPICache(false);

			return m_cache && m_cache->hWnd == GetDpiHWND();
		}

		return false;
	}

	UINT GetDpiX()
	{
		return UINT(ValidateDPICache() ? m_cache->currentDpi.x : VsUI::DpiHelper::GetDeviceDpiX());
	}
	UINT GetDpiY()
	{
		return UINT(ValidateDPICache() ? m_cache->currentDpi.y : VsUI::DpiHelper::GetDeviceDpiX());
	}
	UINT GetPreviousDpiX()
	{
		return UINT(ValidateDPICache() ? m_cache->previousDpi.x : VsUI::DpiHelper::GetDeviceDpiX());
	}
	UINT GetPreviousDpiY()
	{
		return UINT(ValidateDPICache() ? m_cache->previousDpi.y : VsUI::DpiHelper::GetDeviceDpiX());
	}
// 	UINT GetLogPixelsX()
// 	{
// 		return UINT(ValidateDPICache() ? m_cache->logPixels.x : VsUI::DpiHelper::GetLogicalDpiX());
// 	}
// 	UINT GetLogPixelsY()
// 	{
// 		return UINT(ValidateDPICache() ? m_cache->logPixels.y : VsUI::DpiHelper::GetLogicalDpiX());
// 	}

	bool DpiChanged()
	{
		return UINT(ValidateDPICache() && m_cache->currentDpi != m_cache->previousDpi);
	}

	double GetDpiChangeScaleFactor()
	{
		if (ValidateDPICache() && m_cache->previousDpi.x != 0)
			return (double)m_cache->currentDpi.x / (double)m_cache->previousDpi.x;

		return 1;
	}

	int GetSystemMetricsForDpi(int nIndex)
	{
		return VsUI::CDpiAwareness::GetSystemMetricsForDPI(nIndex, GetDpiY());
	}

  public:
	virtual ~CDpiHandlerCached() = default;
};

// [case: 148076]
// Fixes the problem with in-place tooltips shown between two differently scaled monitors.
// Applies also for case when per monitor DPI awareness support is disabled in Visual Studio.
// In-place tooltips fail without hang but stop working for specific control in such case.
class CInPlaceTooltipDpiHandler
{
#ifdef DEBUG
//#define TTMSG
#endif
	struct CTooltipSubclass : public CWndSubclassComCtl
	{
		HWND mDpiSource = nullptr;

		LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam) override
		{
#ifdef TTMSG
// 			if (message != WM_TIMER &&
// 				message != TTM_RELAYEVENT &&
// 				message != TTM_WINDOWFROMPOINT)
			{
				auto msgString = ThemeUtils::GetTooltipMessageString(message, wParam, lParam);
				msgString.Insert(0, "#TTMSG ");
				::OutputDebugStringA(msgString);
			}
#endif
			// [case: 148076] ignore invalid DPI changes
			if (message == WM_DPICHANGED && ::IsWindow(mDpiSource))
			{
				// get DPI directly from window handle...
				// this gives us correct DPI when per monitor DPI support is disabled in VS
				UINT srcDpi = VsUI::CDpiAwareness::GetDpiForWindow(mDpiSource, false);
				UINT msgDpi = LOWORD(wParam);

				if (msgDpi != srcDpi)
				{
#ifdef TTMSG
					::OutputDebugStringA("#TTMSG #### Ignored DPI change ####");
#endif
					return 0;
				}
			}

			LRESULT result = __super::WindowProc(message, wParam, lParam);

			if (message == WM_SHOWWINDOW && wParam == SW_HIDE)
			{
				Detach();
			}

			return result;
		}

		void Attach(HWND dpiSource, HWND toolTip)
		{
			mDpiSource = dpiSource;

			if (m_hWnd != toolTip)
			{
				if (m_hWnd)
					Unsubclass();

				if (toolTip)
					Subclass(toolTip);

#ifdef TTMSG
				if (m_hWnd)
					::OutputDebugStringA("#TTMSG #### Attached ####");
				else
					::OutputDebugStringA("#TTMSG #### Detached ####");
#endif
			}
		}

		void Detach()
		{
			Attach(nullptr, nullptr);
		}
#undef TTMSG
	};

	std::unique_ptr<CTooltipSubclass> pSubclass;

  public:
	void OnOwnerWindowMessage(IDpiHandler& owner, UINT message, WPARAM wParam, LPARAM lParam)
	{
		// [case: 148076]
		// NOTE: Don't check IsPerMonitorDPIAwarenessEnabled because it does not matter.
		// Tooltips fail without hang and stop working when per monitor DPI support is disabled.
		// This solution fixes the problem even when per monitor awareness is disabled.

		if (message == WM_NOTIFY)
		{
			auto* pNMHDR = (NMHDR*)lParam;
			if (pNMHDR && pNMHDR->code == TTN_SHOW)
			{
				AFX_MODULE_THREAD_STATE* pAMTS = AfxGetModuleThreadState();
				if (!pAMTS || !pAMTS->m_pToolTip || pAMTS->m_pToolTip->m_hWnd != pNMHDR->hwndFrom)
				{
					if (VsUI::DpiHelper::IsMultiDpiEnvironment())
					{
						// create all only for controls which need this

						if (!pSubclass)
							pSubclass = std::make_unique<CTooltipSubclass>();

						if (pSubclass)
							pSubclass->Attach(owner.GetDpiHWND(), pNMHDR->hwndFrom);
					}
				}
			}
		}
		else if (pSubclass && message == WM_DESTROY)
		{
			pSubclass->Detach();
			pSubclass = nullptr;
		}
	}
};

template <typename BASE, DPI_AWARENESS_CONTEXT dpiContext = DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2>
class CDpiAware : public CDpiHandlerCached, FontSettings::Listener
{
  protected:
	template <typename T> struct TmpValue
	{
		T& m_valRef;
		T m_endVal;

		TmpValue(T& valRef, T endVal) : m_valRef(valRef), m_endVal(endVal)
		{
		}

		TmpValue(T& valRef, T initVal, T endVal) : m_valRef(valRef), m_endVal(endVal)
		{
			m_valRef = initVal;
		}

		~TmpValue()
		{
			m_valRef = m_endVal;
		}
	};

	class DpiScope
	{
		VsUI::CDpiScope* pAwareness;

		DpiScope(const DpiScope&) = delete;
		DpiScope& operator=(const DpiScope&) = delete;

	  public:
		DpiScope(DPI_AWARENESS_CONTEXT dpiContext, bool setScope) : pAwareness(nullptr)
		{
			if (setScope)
			{
				if (dpiContext)
					pAwareness = new VsUI::CDpiScope(dpiContext);
			}
		}

		~DpiScope()
		{
			delete pAwareness;
		}
	};

	enum class DpiChange
	{
		None,
		Current,		// WM_DPICHANGED - applies to current window BEFORE DPI is changed
		BeforeParent,	// WM_DPICHANGED_BEFOREPARENT
		AfterParent		// WM_DPICHANGED_AFTERPARENT
	};

	bool m_notifyOnlyRealDpiChanges = true;
	bool m_notifyNextAfterParent = false;
	CInPlaceTooltipDpiHandler m_tooltipDpiHandler;

	DPI_AWARENESS_CONTEXT GetDpiContext()
	{
		return dpiContext;
	}

	virtual void ApplyDpiChange(DpiChange currentChange)
	{
		if (currentChange != DpiChange::AfterParent)
		{
			auto pWnd = GetDpiWindow();
			auto dpi = GetDpiY();

			if (pWnd)
			{
				gImgListMgr->TryUpdateImageListsForDPI(*pWnd, dpi);
			}

			UpdateFonts(VAFTF_All, dpi);
		}
	}

	virtual bool HandleWndMessage(UINT message, WPARAM wParam, LPARAM lParam, LRESULT& outResult)
	{
		if (message == WM_DESTROY)
			DetachDpiHandler();
		else if (m_handlerState == DpiHandlerState::None)
			AttachDpiHandler();

		DpiScope scope(GetDpiContext(), VsUI::CDpiAwareness::IsPerMonitorDPIAwarenessEnabled());

		if (!ValidateDPICache())
			return false;

		// [case: 148076] ignore invalid DPI changes (applies even when per monitor DPI support is disabled)
		m_tooltipDpiHandler.OnOwnerWindowMessage(*this, message, wParam, lParam);

		if (message == WM_DPICHANGED || message == WM_DPICHANGED_BEFOREPARENT || message == WM_DPICHANGED_AFTERPARENT)
		{
			WPARAM dpiWParam = 0;
			DpiChange dpiChange = DpiChange::None;

			switch (message)
			{
			case WM_DPICHANGED_BEFOREPARENT:
				dpiChange = DpiChange::BeforeParent;
				break;
			case WM_DPICHANGED_AFTERPARENT:
				dpiChange = DpiChange::AfterParent;
				break;
			default:
				dpiWParam = wParam;
				dpiChange = DpiChange::Current;
				break;
			}

			bool handled = false;
			bool updated = UpdateDPICache(true, dpiWParam);

			if (!m_notifyOnlyRealDpiChanges || updated ||
			    (m_notifyNextAfterParent && message == WM_DPICHANGED_AFTERPARENT))
			{
				OnDpiChanged(dpiChange, handled);
				if (!handled)
				{
					ApplyDpiChange(dpiChange);
				}
			}

			// allow notifications for corresponding WM_DPICHANGED_AFTERPARENT
			// when only real DPI changes notifications are enabled
			m_notifyNextAfterParent = message == WM_DPICHANGED_BEFOREPARENT && updated;

			outResult = 0;
			return false; // let other classes handle the message
		}

		return false;
	}

	virtual void OnFontSettingsChanged(VaFontTypeFlags changed, bool& handled)
	{
	}

	void OnFontSettingsChanged(VaFontTypeFlags changed) final
	{
		HWND hWnd = GetDpiHWND();
		if (hWnd && ::IsWindow(hWnd))
		{
			bool handled = false;
			OnFontSettingsChanged(changed, handled);
			if (!handled)
			{
				UpdateFonts(changed);
			}
		}
	}

	virtual void OnDpiChanged(DpiChange change, bool& handled)
	{
		if (change != DpiChange::BeforeParent)
		{
			// force redraw so:
			// - all controls don't contain glitches
			// - edit controls position caret correctly

			HWND hWnd = GetDpiHWND();
			if (hWnd && ::IsWindow(hWnd))
			{
				::SetWindowPos(hWnd, nullptr, 0, 0, 0, 0,
				               SWP_ASYNCWINDOWPOS | SWP_FRAMECHANGED | SWP_NOSIZE | SWP_NOZORDER | SWP_NOMOVE |
				                   SWP_NOCOPYBITS);
			}
		}
	}

  public:
	virtual ~CDpiAware()
	{
	}
};

template <class BASE> class CWndDpiAware : public BASE, public CDpiAware<BASE>
{

  protected:
	HWND GetDpiHWND() override
	{
		return __super::GetSafeHwnd();
	}

	CWnd* GetDpiWindow() override
	{
		return this;
	}

	BOOL PreCreateWindow(CREATESTRUCT& cs) override
	{
		VsUI::CDpiAwareness::AssertUnexpectedDpiContext(__super::GetDpiContext());

		if (__super::PreCreateWindow(cs))
		{
			__super::AttachDpiHandler();
			return TRUE;
		}

		return FALSE;
	}

	void PreSubclassWindow() override
	{
		__super::PreSubclassWindow();
		__super::AttachDpiHandler();
	}

	LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam) override
	{
		LRESULT rslt = 0;
		if (__super::HandleWndMessage(message, wParam, lParam, rslt))
			return rslt;

		return __super::WindowProc(message, wParam, lParam);
	}

  public:
	virtual ~CWndDpiAware()
	{
	}
};

template <typename BASE> class CWndDpiAwareMiniHelp : public CWndDpiAware<BASE>
{
  public:
	CWndDpiAwareMiniHelp()
	{
		CWndDpiAware<BASE>::m_font_type = VaFontType::MiniHelpFont;
	}
};

#endif // CWndDpiAware_h__