#include "stdafxed.h"
#if defined(RAD_STUDIO) || defined(VA_CPPUNIT)

#include "RadStudioFrame.h"
#include "CppBuilder.h"
#include "WindowUtils.h"
#include "VaService.h"
#include "RadStudioPlugin.h"
#include "LiveOutlineFrame.h"
#include "TraceWindowFrame.h"

static std::vector<VaRSFrameBase*> sRadStudioFrames;

VaRSFrameBase* VaRSFrameBase::RS_GetFrame(int id)
{
	if (id >= 0 && id < (int)sRadStudioFrames.size())
		return sRadStudioFrames[(size_t)id];

	return nullptr;
}

size_t VaRSFrameBase::RS_GetFramesCount()
{
	return sRadStudioFrames.size();
}

static ATOM mFrameClassAtom = 0;
LPCTSTR VaRSFrameBase::RS_GetFrameClassName()
{
	LPCTSTR className = _T("VaRSToolWindowFrameCls");

	if (!mFrameClassAtom)
	{
		WNDCLASS wndcls;
		::ZeroMemory(&wndcls, sizeof(wndcls));
		wndcls.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
		wndcls.lpfnWndProc = ::DefWindowProc;
		wndcls.hInstance = AfxGetInstanceHandle();
		wndcls.lpszClassName = className;
		wndcls.hbrBackground = (HBRUSH)::GetStockObject(NULL_BRUSH);
		mFrameClassAtom = ::RegisterClass(&wndcls);
	}

	return className;
}

VaRSFrameBase::VaRSFrameBase()
{
	auto it = std::find(sRadStudioFrames.cbegin(), sRadStudioFrames.cend(), nullptr);
	it = sRadStudioFrames.insert(it, this);
	m_wnd_id = (int)std::distance(sRadStudioFrames.cbegin(), it);
}

VaRSFrameBase::~VaRSFrameBase()
{
	sRadStudioFrames[(size_t)m_wnd_id] = nullptr;
}

HWND VaRSFrameBase::RS_ShowWindow(LPCWSTR caption)
{
	if (caption && gRadStudioHost)
	{
		return gRadStudioHost->ShowWindow(m_wnd_id, caption);
	}

	return nullptr;
}

VaRSFrameBase::RS_WndType VaRSFrameBase::RS_GetWndType(HWND hWnd)
{
	if (hWnd && ::IsWindow(hWnd))
	{
		auto cls = ::GetWindowClassString(hWnd);
		if (cls.length() > 3 && cls[0] == 'T' && cls[1] == 'V' && cls[2] == 'A')
		{
			if (cls == "TVAEmbeddedFrame")
				return RS_WndType::EmbeddedFrame;

			if (cls == "TVADockableForm")
				return RS_WndType::DockableForm;
		}
	}

	return RS_WndType::None;
}

bool VaRSFrameBase::RS_ResizeForm(HWND hWndForm, const CRect& clientRect, bool repaint)
{
	if (RS_GetWndType(hWndForm) == RS_WndType::DockableForm)
	{
		CRect wr;
		CRect cr;

		if (::GetWindowRect(hWndForm, &wr) && ::GetClientRect(hWndForm, &cr))
		{
			auto diff = clientRect.Size() - cr.Size();

			wr.right += diff.cx;
			wr.bottom += diff.cy;

			::MoveWindow(hWndForm, wr.left, wr.top, wr.Width(), wr.Height(), repaint ? TRUE : FALSE);
			return true;
		}
	}
	return false;
}

void VaRSFrameBase::RS_ResizeParentsToChild(HWND child, bool repaint, LPSIZE sizeOverride /*= nullptr*/)
{
	CRect childRect;
	if (::GetWindowRect(child, &childRect))
	{
		if (sizeOverride)
		{
			childRect.right = childRect.left + sizeOverride->cx;
			childRect.bottom = childRect.top + sizeOverride->cy;
		}

		// this way with vector will prevent us from endless loop
		std::vector<HWND> wndList;
		HWND w = ::GetParent(child);
		while (w && ::IsWindow(w))
		{
			wndList.push_back(w);
			w = ::GetParent(w);
		}

		for (HWND wnd : wndList)
		{
			::MoveWindow(wnd, 0, 0, childRect.Width(), childRect.Height(), repaint ? TRUE : FALSE);

			if (RS_GetWndType(wnd) == RS_WndType::EmbeddedFrame)
			{
				RS_ResizeForm(::GetParent(wnd), childRect, repaint);
				break;
			}
		}
	}
}

void VaRSFrameBase::OnShutdown()
{
	for (auto* frame : sRadStudioFrames)
	{
		if (frame)
		{
			frame->RS_OnShutdown();
		}
	}
}

static CSize sRsLastOutlineSize;
class VaRSFileOutlineFrame : public VaRSFrameCWnd
{
  public:
	VaRSFileOutlineFrame()
	{
		if (!gVaService || !gVaRadStudioPlugin)
			return;

		HWND frame_hWnd = RS_ShowWindow(L"VaOutline");
		if (RS_Create(frame_hWnd))
		{
			auto outline = gVaService->GetOutlineFrame();
			if (!outline)
			{
				gVaService->PaneCreated(m_hWnd, "VaOutline", 0);
				outline = gVaService->GetOutlineFrame();
			}
			else
			{
				outline->SetParent(this);
			}

			if (outline)
			{
				outline->ShowWindow(SW_SHOW);
				gVaRadStudioPlugin->UpdateVaOutline();

				if (sRsLastOutlineSize.cx || sRsLastOutlineSize.cy)
					RS_ResizeParentsToChild(outline->m_hWnd, true, &sRsLastOutlineSize);
				else
					RS_ResizeParentsToChild(outline->m_hWnd, true);
			}
		}
	}

	virtual ~VaRSFileOutlineFrame()
	{
		if (m_hWnd && gVaService->GetOutlineFrame())
			gVaService->PaneDestroy(m_hWnd);
	}

	void RS_OnHookWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) override
	{
		if (message == WM_SIZE)
		{
			CRect rc;
			rc.right = LOWORD(lParam);
			rc.bottom = HIWORD(lParam);
			RS_MoveWindow(rc);
		}
	}

	LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam) override
	{
		if (message == WM_DESTROY)
		{
			gVaService->PaneDestroy(m_hWnd);
		}

		return __super::WindowProc(message, wParam, lParam);
	}

	void RS_OnShutdown() override
	{
		if (m_hWnd)
			DestroyWindow();
	}

	virtual void RS_MoveWindow(const CRect& rc)
	{
		sRsLastOutlineSize.SetSize(rc.Width(), rc.Height());

		__super::RS_MoveWindow(rc);

		auto outline = gVaService->GetOutlineFrame();
		if (outline)
		{
			outline->MoveWindow(&rc);
		}
	}
};

class VaRSTraceFrame : public VaRSFrameCWnd
{
  public:
	VaRSTraceFrame()
	{
		if (!gVaService || !gVaRadStudioPlugin)
			return;

		HWND frame_hWnd = RS_ShowWindow(L"VaTrace");
		if (RS_Create(frame_hWnd))
		{
			auto frame = gVaService->GetTraceFrame();
			if (!frame)
			{
				gVaService->PaneCreated(m_hWnd, "VaTrace", 0);
				frame = gVaService->GetTraceFrame();
			}
			else
			{
				frame->SetParent(this);
			}

			if (frame)
			{
				frame->ShowWindow(SW_SHOW);
				RS_ResizeParentsToChild(frame->m_hWnd, true);
			}
		}
	}

	virtual ~VaRSTraceFrame()
	{
		if (m_hWnd && gVaService->GetTraceFrame())
			gVaService->PaneDestroy(m_hWnd);
	}

	void RS_OnHookWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) override
	{
		if (message == WM_SIZE)
		{
			CRect rc;
			rc.right = LOWORD(lParam);
			rc.bottom = HIWORD(lParam);
			RS_MoveWindow(rc);
		}
	}

	LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam) override
	{
		if (message == WM_DESTROY)
		{
			gVaService->PaneDestroy(m_hWnd);
		}

		return __super::WindowProc(message, wParam, lParam);
	}

	void RS_OnShutdown() override
	{
		if (m_hWnd && gVaService)
			gVaService->PaneDestroy(m_hWnd);

		if (m_hWnd)
			DestroyWindow();
	}

	virtual void RS_MoveWindow(const CRect& rc)
	{
		__super::RS_MoveWindow(rc);

		auto frame = gVaService->GetTraceFrame();
		if (frame)
		{
			frame->MoveWindow(&rc);
		}
	}
};

VaRSFrameCWnd* VaRSFrameFactory::CreateTraceFrame()
{
	return new VaRSTraceFrame();
}

VaRSFrameCWnd* VaRSFrameFactory::CreateFileOutlineFrame()
{
	return new VaRSFileOutlineFrame();
}

#endif
