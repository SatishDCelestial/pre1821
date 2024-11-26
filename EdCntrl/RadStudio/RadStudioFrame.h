#pragma once
#if defined(RAD_STUDIO) || defined(VA_CPPUNIT)

#include "PROJECT.H"

class VaRSFrameBase
{
  public:
	enum class RS_WndType
	{
		None,
		EmbeddedFrame,
		DockableForm
	};

	static VaRSFrameBase* RS_GetFrame(int id);
	static size_t RS_GetFramesCount();

	static LPCTSTR RS_GetFrameClassName();

	VaRSFrameBase();
	virtual ~VaRSFrameBase();

	virtual HWND RS_ShowWindow(LPCWSTR caption);

	virtual void RS_OnParentChanging() = 0;
	virtual void RS_OnParentChanged(HWND newParent) = 0;
	virtual void RS_OnShutdown() = 0;

	static RS_WndType RS_GetWndType(HWND hWnd);

	static bool RS_ResizeForm(HWND hWndForm, const CRect& clientRect, bool repaint);

	static void RS_ResizeParentsToChild(HWND child, bool repaint, LPSIZE sizeOverride = nullptr);

	static void OnShutdown();

  protected:
	int m_wnd_id = -1;
};

template <class BASE>
class VaRSFrameSubclass : public BASE, public VaRSFrameBase
{
  public:
	virtual ~VaRSFrameSubclass()
	{
	}

	void RS_OnParentChanging() override
	{
		if (BASE::m_hWnd)
		{
			BASE::UnsubclassWindow();
		}
	}

	void RS_OnParentChanged(HWND newParent) override
	{
		BASE::SubclassWindow(newParent);
	}

	void RS_OnShutdown() override
	{
		if (BASE::m_hWnd)
		{
			BASE::UnsubclassWindow();
		}
	}
};

struct VaRSWndHook : public CWnd
{
	struct Listener
	{
		virtual void RS_OnHookWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) = 0;
		virtual ~Listener() = default;
	};

	Listener* m_listener;

	VaRSWndHook(Listener* listener = nullptr)
	    : m_listener(listener)
	{
	}

	void SetListener(Listener* listener)
	{
		m_listener = listener;
	}

	virtual ~VaRSWndHook()
	{
		if (m_hWnd)
			UnsubclassWindow();
	}

	LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam) override
	{
		if (m_listener)
		{
			m_listener->RS_OnHookWndProc(m_hWnd, message, wParam, lParam);
		}

		return __super::WindowProc(message, wParam, lParam);
	}
};

class VaRSFrameCWnd : public CWnd, public VaRSFrameBase, VaRSWndHook::Listener
{
	VaRSWndHook m_frame;

  public:
	VaRSFrameCWnd()
	{
		m_frame.SetListener(this);
	}

	virtual ~VaRSFrameCWnd()
	{
	}

	virtual void RS_MoveWindow(const CRect& rc)
	{
		MoveWindow(&rc);
	}

	virtual void RS_FillTheParent()
	{
		auto parent = GetParent();
		if (parent)
		{
			CRect rc;
			parent->GetClientRect(&rc);
			RS_MoveWindow(rc);
		}
	}

	bool RS_Create(HWND rs_frame)
	{
		if (m_frame.SubclassWindow(rs_frame))
		{
			CRect cr;
			m_frame.GetClientRect(&cr);
			return !!CreateEx(0, RS_GetFrameClassName(), NULL, WS_CHILD | WS_VISIBLE | WS_TABSTOP, cr, &m_frame, (UINT)m_wnd_id);
		}

		return false;
	}

	void RS_OnParentChanging() override
	{
		m_frame.UnsubclassWindow();
		ShowWindow(SW_HIDE);
		SetParent(gMainWnd);
	}

	void RS_OnParentChanged(HWND newParent) override
	{
		m_frame.SubclassWindow(newParent);
		ShowWindow(SW_SHOW);
		SetParent(CWnd::FromHandle(newParent));
		RS_FillTheParent();
	}

	void RS_OnShutdown() override
	{
		if (m_hWnd)
		{
			DestroyWindow();
		}
	}
};

struct VaRSFrameFactory
{
	static VaRSFrameCWnd* CreateTraceFrame();
	static VaRSFrameCWnd* CreateFileOutlineFrame();
};

#endif