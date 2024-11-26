//#if _MSC_VER >= 1500

#include "stdafxed.h"
#pragma warning(push, 2)
#define INCL_WINSOCK_API_PROTOTYPES 1
#include <afxsock.h>
#pragma warning(pop)

#if _MSC_VER >= 1500
#else

#error gmit: unsupported?
// There's some weirdness with the vs2003 sockets headers.
// Putting afxsock.h before stdafxed.h helps.
#ifndef WINVER
#if _MSC_VER <= 1200
#define WINVER 0x0400
#else
#define WINVER 0x0500
#endif
#endif
#include <afxsock.h>
#undef INCL_WINSOCK_API_PROTOTYPES
#include "stdafxed.h"

#endif

#if !defined(VA_CPPUNIT)
#include "log.h"
#include "LicenseMonitor.h"
#include "RegKeys.h"
#include "AddIn/BuyTryDlg_Strings.h"
#include "..\common\ThreadName.h"
#endif // !VA_CPPUNIT
#include "PooledThreadBase.h"
#include "DevShellService.h"
#include "IVaLicensing.h"
#include "VaAddinClient.h"
#ifndef NOSMARTFLOW
#include "SmartFlow/phdl.h"
#endif

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

static CWinThread* sMonitorThread = NULL;
BOOL gLicenseCountOk = TRUE;

#if defined(VA_CPPUNIT)

void StopLicenseMonitor()
{
}

#else

static void MonitorViolationCallback()
{
	// should sendmsg to main thread to display this instead of
	// displaying on monitor thread
	Log("OZCB");
	CString user(gVaLicensingHost ? gVaLicensingHost->GetLicenseInfoUser() : L"");
	CString msg;
	CString__FormatA(msg, kMsgBoxText_UserCountExceeded, (const TCHAR*)user);
	WtMessageBox(msg, kMsgBoxTitle_UserCountExceeded, MB_OK);
}

// Not a thread pool thread - this is a one time thread and
// not sure if it requires alertable waits.
class CArmadilloThread : public CWinThread
{
  public:
	static void CreateMonitorThread()
	{
		ASSERT(!sMonitorThread);
		sMonitorThread = new CArmadilloThread;
	}

  private:
	CArmadilloThread() : CWinThread()
	{
		CreateThread();
	}
	~CArmadilloThread()
	{
		ASSERT(!sMonitorThread);
	}

	virtual int Run()
	{
#ifdef _DEBUG
		DEBUG_THREAD_NAME("VAX:ArmThread");
#endif
		if (gVaLicensingHost && !gVaLicensingHost->IsVaxNetworkLicenseCountOk())
		{
			gLicenseCountOk = FALSE;
#ifndef NOSMARTFLOW
			SmartFlowPhoneHome::FireLicenseCountViolation();
#endif
			new LambdaThread([] { MonitorViolationCallback(); }, "LVN", true);
		}
		return ExitInstance();
	}

	virtual BOOL InitInstance()
	{
		return TRUE;
	}

	virtual int ExitInstance()
	{
		CArmadilloThread* thrd = static_cast<CArmadilloThread*>(sMonitorThread);
		sMonitorThread = NULL;
		ASSERT(thrd == this);
		delete thrd;
		return 0;
	}
};

void CreateMonitorThread(LPVOID)
{
	if (!gVaLicensingHost)
	{
		_ASSERTE(gVaLicensingHost);
		return;
	}

	if (gVaLicensingHost->IsVaxNetworkLicenseCountSupported())
	{
		Sleep(3500);
		CArmadilloThread::CreateMonitorThread();
	}
}

void VaAddinClient::StartLicenseMonitor()
{
	if (gVaLicensingHost->IsSanctuaryLicenseInstalled(false))
		return;

	// [case: 40097] offload from UI thread
	new FunctionThread(CreateMonitorThread, "CMT", true);
}

void StopLicenseMonitor()
{
	if (sMonitorThread && sMonitorThread->m_nThreadID)
		sMonitorThread->PostThreadMessage(WM_QUIT, 0, 0);
}

#endif // !VA_CPPUNIT
