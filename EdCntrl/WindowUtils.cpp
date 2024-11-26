#include "stdafxed.h"
#include "WindowUtils.h"
#include "mainThread.h"
#include "SubClassWnd.h"
#include "DebugStream.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif // _DEBUG

#define CWndSubclassComCtl_ALLOW_OPTIONAL_ASSERT
#if defined(_DEBUG) && defined(CWndSubclassComCtl_ALLOW_OPTIONAL_ASSERT)
#define CWndSubclassComCtl_OPTIONAL_ASSERT(stmt) _ASSERT(stmt)
#else
#define CWndSubclassComCtl_OPTIONAL_ASSERT(stmt)
#endif

//#define CWndSubclassComCtl_DBGPRINT

LRESULT CWndSubclassComCtl::SubclassProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	try
	{
		auto pSubclass = reinterpret_cast<CWndSubclassComCtl*>(dwRefData);
		if (pSubclass)
		{
			return pSubclass->WindowProc(message, wParam, lParam);
		}
	}
	catch (...)
	{
	}

	try
	{
		RemoveSubclass(hWnd, uIdSubclass);
		return DefProc(hWnd, message, wParam, lParam);
	}
	catch (...)
	{
	}

	return 0;
}

LRESULT CWndSubclassComCtl::DefProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

BOOL CWndSubclassComCtl::SetSubclass(_In_ HWND hWnd, _In_ UINT_PTR uIdSubclass, _In_ CWndSubclassComCtl* dwRefData)
{
	return SetWindowSubclass(hWnd, SubclassProc, uIdSubclass, (DWORD_PTR)dwRefData);
}

BOOL CWndSubclassComCtl::GetSubclass(_In_ HWND hWnd, _In_ UINT_PTR uIdSubclass, _Out_opt_ CWndSubclassComCtl** pdwRefData)
{
	if (pdwRefData)
		*pdwRefData = nullptr;

	return GetWindowSubclass(hWnd, SubclassProc, uIdSubclass, (DWORD_PTR*)pdwRefData);
}

BOOL CWndSubclassComCtl::RemoveSubclass(_In_ HWND hWnd, _In_ UINT_PTR uIdSubclass)
{
	return RemoveWindowSubclass(hWnd, SubclassProc, uIdSubclass);
}

LRESULT CWndSubclassComCtl::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	HWND hWnd = m_hWnd; 

	if (message == WM_NCDESTROY)
	{
		UnsubclassImpl();
	}

	return DefProc(hWnd, message, wParam, lParam);
}

bool CWndSubclassComCtl::SubclassImpl(HWND hWnd)
{
	try
	{
		_ASSERTE(g_mainThread == GetCurrentThreadId());
		_ASSERTE(::IsWindow(hWnd));

		if (m_hWnd == hWnd)
		{
#ifdef CWndSubclassComCtl_DBGPRINT
			VADEBUGPRINT("#WSC Already subclassed - ComCtl");
#endif
			return true;
		}

		if (m_hWnd && !UnsubclassImpl())
		{
			return false;
		}

#ifdef CWndSubclassComCtl_DBGPRINT
		VADEBUGPRINT("#WSC Subclassing - ComCtl");
#endif

		if (::IsWindow(hWnd) && SetSubclass(hWnd, GetIdSubclass(), this))
		{
			m_hWnd = hWnd;
			return true;
		}

		CWndSubclassComCtl_OPTIONAL_ASSERT(!"CWndSubclassComCtl::SubclassImpl failed!");

		return false;
	}
	catch (...)
	{
	}

	CWndSubclassComCtl_OPTIONAL_ASSERT(!"CWndSubclassComCtl::SubclassImpl failed!");

	return false;
}

bool CWndSubclassComCtl::UnsubclassImpl()
{
	try
	{
		_ASSERTE(g_mainThread == GetCurrentThreadId());

		if (m_hWnd)
		{
#ifdef CWndSubclassComCtl_DBGPRINT
			VADEBUGPRINT("#WSC Unsubclassing - ComCtl");
#endif

			if (RemoveSubclass(m_hWnd, GetIdSubclass()) || !::IsWindow(m_hWnd))
			{
				m_hWnd = nullptr;
				return true;
			}

			CWndSubclassComCtl_OPTIONAL_ASSERT(!"CWndSubclassComCtl::UnsubclassImpl failed!");

			return false;
		}
	}
	catch (...)
	{
	}

	return false;
}

bool CWndSubclassComCtl::Subclass(HWND hWnd)
{
	if (g_mainThread != GetCurrentThreadId())
	{
		bool result = false;
		::RunFromMainThread([&]() { result = SubclassImpl(hWnd); });
		return result;
	}

	return SubclassImpl(hWnd);
}

bool CWndSubclassComCtl::Unsubclass()
{
	if (g_mainThread != GetCurrentThreadId())
	{
		bool result = false;
		::RunFromMainThread([&]() { result = UnsubclassImpl(); });
		return result;
	}

	return UnsubclassImpl();
}

StopWatch::StopWatch()
{
	is_hires = QueryPerformanceFrequency(&frequency);
	Restart();
}

long long StopWatch::GetTimeStamp()
{
	if (is_hires)
	{
		LARGE_INTEGER now;
		QueryPerformanceCounter(&now);
		return (1000LL * now.QuadPart) / frequency.QuadPart;
	}

	return GetTickCount();
}

void StopWatch::Restart()
{
	start = GetTimeStamp();
}

bool StopWatch::IsHighResolution()
{
	return !!is_hires;
}

long long StopWatch::ElapsedMilliseconds()
{
	return GetTimeStamp() - start;
}

bool MoveWindowIfNeeded(HWND hWnd, LPCRECT rect, bool repaint /*= true*/)
{
	_ASSERT(rect);
	return rect && MoveWindowIfNeeded(hWnd, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, repaint);
}

bool MoveWindowIfNeeded(HWND hWnd, int x, int y, int width, int height, bool repaint /*= true*/)
{
	_ASSERT(hWnd);

	return ::MoveWindow(hWnd, x, y, width, height, repaint ? TRUE : FALSE);

// 	if (!hWnd)
// 	{
// 		return false;
// 	}
// 
// 	CRect oldRect;
// 	if (::GetWindowRect(hWnd, &oldRect))
// 	{
// 		// we can check Height and Width without moving to client
// 		if (oldRect.Width() != width || oldRect.Height() != height)
// 		{
// 			return !!MoveWindow(hWnd, x, y, width, height, repaint ? TRUE : FALSE);
// 		}
// 
// 		HWND parent = ::GetParent(hWnd);
// 		if (parent)
// 		{
// 			// we don't have to translate both points as height has been already tested
// 			::ScreenToClient(parent, &oldRect.TopLeft());
// 			// ::ScreenToClient(parent, &oldRect.BottomRight()); // not necessary
// 		}
// 
// 		// now we need to check only location
// 		// note: oldRect's width and height may be invalid at this point
// 		if (oldRect.left != x || oldRect.top != y)
// 		{
// 			return !!MoveWindow(hWnd, x, y, width, height, repaint ? TRUE : FALSE);
// 		}
// 
// 		if (repaint)
// 		{
// 			::InvalidateRect(hWnd, nullptr, false);
// 		}
// 	}
// 
// 	return false;
}

bool MoveWindowIfNeeded(CWnd* pWnd, LPCRECT rect, bool repaint /*= true*/)
{
	return pWnd && MoveWindowIfNeeded(pWnd->m_hWnd, rect, repaint);
}

bool MoveWindowIfNeeded(CWnd* pWnd, int x, int y, int width, int height, bool repaint /*= true*/)
{
	return pWnd && MoveWindowIfNeeded(pWnd->m_hWnd, x, y, width, height, repaint);
}
