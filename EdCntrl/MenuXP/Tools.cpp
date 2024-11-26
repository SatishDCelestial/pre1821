///////////////////////////////////////////////////////////////////////////////
//
// Tools.cpp : implementation file
//
///////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "Tools.h"
#include "Registry.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

///////////////////////////////////////////////////////////////////////////////
// Check if the specified window is child of a docked toolbar
bool ChildOfDockedToolbar (CWnd* pWnd)
{
    CWnd* pParent = pWnd->GetParent();
    
    return pParent->IsKindOf (RUNTIME_CLASS(CControlBar)) &&
           ((CControlBar*)pParent)->m_pDockBar != NULL;
}

///////////////////////////////////////////////////////////////////////////////
// Return the current system version
//
#pragma warning(push)
#pragma warning(disable: 4996)
WinVer WINAPI GetWinVersion ()
{
	// update at some point:
	// http://stackoverflow.com/questions/22303824/warning-c4996-getversionexw-was-declared-deprecated
	static WinVer s_wvVal = wvUndefined;

    if ( s_wvVal != wvUndefined )
    {
        return s_wvVal;
    }
    OSVERSIONINFO osvi;

    ZeroMemory (&osvi, sizeof OSVERSIONINFO);
    osvi.dwOSVersionInfoSize = sizeof OSVERSIONINFO;

    if ( !GetVersionEx (&osvi) )
    {
        return s_wvVal = wvUndefined;
    }
    if ( osvi.dwPlatformId == VER_PLATFORM_WIN32s )
    {
        return s_wvVal = wvWin32s;
    }

	if ( osvi.dwPlatformId == VER_PLATFORM_WIN32_NT )
    {
        if ( osvi.dwMajorVersion == 3L )
        {
            return s_wvVal = wvWinNT3;
        }

		if ( osvi.dwMajorVersion == 4L )
        {
            return s_wvVal = wvWinNT4;
        }

		if ( osvi.dwMajorVersion == 5L )
		{
			if ( osvi.dwMinorVersion == 0L )
			{
				return s_wvVal = wvWin2000;
			}
			if ( osvi.dwMinorVersion == 2L )
			{
				return s_wvVal = wvWinServer2003;
			}
			return s_wvVal = wvWinXP;
		}

		if ( osvi.dwMajorVersion == 6L )
		{
			if ( osvi.dwMinorVersion == 0L )
				return s_wvVal = wvVista;
			if ( osvi.dwMinorVersion == 1L )
				return s_wvVal = wvWin7;
			if ( osvi.dwMinorVersion == 2L )
			{
				if (osvi.dwBuildNumber > 0x000023f0) // 9200
					return s_wvVal = wvWin8_1;
	
				return s_wvVal = wvWin8;
			}
			if ( osvi.dwMinorVersion == 3L )
			{
				// http://msdn.microsoft.com/en-us/library/windows/desktop/ms724834%28v=vs.85%29.aspx/html
				return s_wvVal = wvWin8_1;
			}
			if (osvi.dwMinorVersion == 4L)
				return s_wvVal = wvWin10;
		}

		if (osvi.dwMajorVersion == 10L)
		{
			return s_wvVal = wvWin10;
		}

		return s_wvVal = WinVer(wvWinMax - 1);
    }

	ASSERT(osvi.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS);
	if ( osvi.dwMajorVersion == 4L )
	{
		if ( osvi.dwMinorVersion == 10L )
		{
			return s_wvVal = wvWin98;
		}
		if ( osvi.dwMinorVersion == 90L )
		{
			return s_wvVal = wvWinME;
		}
	}

	return s_wvVal = wvWin95;
}
#pragma warning(pop)

BOOL
WinVersionSupportsWinRT()
{
	WinVer v = GetWinVersion();
	return v >=  wvWin8;
}

DWORD GetWinReleaseId()
{
	static DWORD releaseId = (DWORD)-1;

	if (releaseId != -1)
	{
		return releaseId;
	}

	if (GetWinVersion() < wvWin10)
	{
		releaseId = 0;
		return releaseId;
	}
	
	CString releaseIdStr = GetRegValue(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", "ReleaseId", "0");
	releaseId = (DWORD)atoi(releaseIdStr);

	return releaseId;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
CMouseMgr::CMouseMgr () : m_bOver (false)
{
}

///////////////////////////////////////////////////////////////////////////////
void CMouseMgr::Init (HWND hWnd, WORD wFlags)
{
    m_hWnd = hWnd;
    m_wFlags = wFlags;
}

///////////////////////////////////////////////////////////////////////////////
bool CMouseMgr::MouseOver () const
{
    return m_bOver;
}

extern "C" WINUSERAPI BOOL WINAPI TrackMouseEvent (LPTRACKMOUSEEVENT lpEventTrack);
///////////////////////////////////////////////////////////////////////////////
bool CMouseMgr::OnMouseMove (HWND hTrack)
{
    ASSERT(m_hWnd != NULL);
    if ( hTrack == NULL ) hTrack = m_hWnd;

    if ( !m_bOver )
    {
        m_bOver = true;

        if ( m_wFlags & MMS_PAINT )
        {
            ::InvalidateRect (m_hWnd, NULL, false);
        }
        if ( m_wFlags & MMS_NCPAINT )
        {
            ::SetWindowPos (m_hWnd, NULL, 0, 0, 0, 0, SWP_NOZORDER|SWP_NOMOVE|SWP_NOSIZE|SWP_FRAMECHANGED);
        }
        // Prepare for tracking MouseOut
        TRACKMOUSEEVENT tme = { sizeof TRACKMOUSEEVENT, TME_LEAVE, m_hTrack = hTrack, 0 };
        ::TrackMouseEvent (&tme);

        return true;
    }
    if ( hTrack != m_hTrack )
    {
        // Cancel tracking MouseOut for the previous window (main or child)
        TRACKMOUSEEVENT tme = { sizeof TRACKMOUSEEVENT, TME_CANCEL, m_hTrack, 0 };
        ::TrackMouseEvent (&tme);

        // Track MouseOut
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = m_hTrack = hTrack;
        ::TrackMouseEvent (&tme);
    }
    return false;
}

///////////////////////////////////////////////////////////////////////////////
bool CMouseMgr::OnMouseOut (HWND hTrack)
{
    ASSERT(m_hWnd != NULL);
    if ( hTrack == NULL ) hTrack = m_hWnd;

    if ( hTrack != m_hTrack )
    {
        return false;
    }
    m_bOver = false;

    if ( m_wFlags & MMS_PAINT )
    {
        ::InvalidateRect (m_hWnd, NULL, false);
    }
    if ( m_wFlags & MMS_NCPAINT )
    {
        ::SetWindowPos (m_hWnd, NULL, 0, 0, 0, 0, SWP_NOZORDER|SWP_NOMOVE|SWP_NOSIZE|SWP_FRAMECHANGED);
    }
    return true;
}
