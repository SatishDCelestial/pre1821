#include "StdAfxEd.h"
#include <stdlib.h>
#include <exception>
#include "VASeException.h"
#include "SeException.h"
#include "StackWalker.h"
#include "../WTString.h"
#include "../StatusWnd.h"
#include "../Registry.h"
#include "../RegKeys.h"
#include "../DBLock.h"
#include "../DevShellService.h"
#include <eh.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

#undef THIS_FILE
static char THIS_FILE[] = __FILE__;

class VA_StackWalker : public StackWalker
{
  public:
	VA_StackWalker()
	{
		DWORD StackWalkerFlag = GetRegDword(HKEY_CURRENT_USER, ID_RK_APP, "StackWalkerFlag");
		if (StackWalkerFlag)
			m_options = (int)StackWalkerFlag;
		else
			m_options = RetrieveVerbose; // SymAll too slow
	}

	virtual void OnOutput(LPCSTR szText)
	{
		// Write output to Startup.log
		if (strncmp("ERROR:", szText, 6) != 0) // Ignore errors to clean stack.
		{
			__super::OnOutput(szText);
			VALOGERROR(szText);
		}
	}
};

class VASeException : public CSeException
{
	VA_StackWalker m_sw;

  public:
	VASeException() : CSeException(NULL, NULL)
	{
	}
	VASeException(UINT nSeCode, _EXCEPTION_POINTERS* pExcPointers) : CSeException(nSeCode, pExcPointers)
	{
	}
	VASeException(CSeException& CseExc) : CSeException(CseExc)
	{
	}
	VASeException* SetException(UINT nSeCode, _EXCEPTION_POINTERS* pExcPointers)
	{
		m_nSeCode = nSeCode;
		m_pExcPointers = pExcPointers;
		// Log the exception synchronous to log all exceptions/addresses.
		CString err;
		GetErrorMessage(err);
#ifdef _DEBUG
		MessageBeep(0xffffffff); // Notify exception
#endif                           // _DEBUG
		VALOGERROR("**************************** EXCEPTION **************************************\r\n");
		VALOGERROR(err);
		return this;
	}
	void LogStack()
	{
		// Log the stack
		if (m_pExcPointers)
			m_sw.ShowCallstack(GetCurrentThread(), m_pExcPointers->ContextRecord);
	}
};

class ThreadExceptionStorage
{
	typedef std::map<DWORD, VASeException*> ExceptionMap;
	ExceptionMap m_ExceptionMap;
	mutable RWLock mMapLock;

	VASeException* GetThreadException()
	{
		RWLockReader lck2(mMapLock);
		DWORD threadID = GetCurrentThreadId();
		_ASSERTE(m_ExceptionMap.size() <
		         5); // if we are doing this on dynamically created threads, we need different logic.
		if (!m_ExceptionMap[threadID])
			m_ExceptionMap[threadID] = new VASeException();
		return m_ExceptionMap[threadID];
	}
	void Clear()
	{
		RWLockWriter lck2(mMapLock);
		for (ExceptionMap::iterator iter = m_ExceptionMap.begin(); iter != m_ExceptionMap.end(); ++iter)
			(*iter).second->Delete();
		m_ExceptionMap.clear();
	}

  public:
	VASeException* SetException(UINT nSeCode, _EXCEPTION_POINTERS* pExcPointers)
	{
		RWLockReader lck2(mMapLock);
		return GetThreadException()->SetException(nSeCode, pExcPointers);
	}

	void LogStack()
	{
		RWLockReader lck2(mMapLock);
		GetThreadException()->LogStack();
	}

	~ThreadExceptionStorage()
	{
		Clear();
	}
};

static ThreadExceptionStorage g_thredExecptions; // Should this be in Alloc/FreeTheGlobals?

void VASeTranslator(UINT nSeCode, _EXCEPTION_POINTERS* pExcPointers)
{
	// Called synchronously on all SE exceptions
	// TODO: catch only exceptions we really want to catch and just throw the rest.
	//       see IsBadXxxPtr http://blogs.msdn.com/oldnewthing/archive/2006/09/27/773741.aspx
	throw g_thredExecptions.SetException(nSeCode, pExcPointers);
}

void VALogExceptionCallStack()
{
#ifdef _DEBUG
	const WTString StackWalkerReg =
	    (const char*)GetRegValue(HKEY_CURRENT_USER, ID_RK_APP, "EnableVASeExceptions");
	const BOOL StackWalkerFlag = (StackWalkerReg == "FullStack");

	if (StackWalkerFlag)
		g_thredExecptions.LogStack();
#endif
}

void VASetSeTranslator()
{
	// Enable our error SE exception handler, if EnableVASeExceptions == Yes
	const WTString StackWalkerReg =
	    (const char*)GetRegValue(HKEY_CURRENT_USER, ID_RK_APP, "EnableVASeExceptions");
	const BOOL StackWalkerFlag = (StackWalkerReg == "AddressOnly" || StackWalkerReg == "FullStack");
	// Should we save the old translator of the main thread and restore it on exit?
	//   Might run into issues similar to window sub-classing.
	// Do other 3rd party add-ins set the translator?
	if (StackWalkerFlag)
		_set_se_translator(VASeTranslator);
}

void VaCrtError(bool terminate)
{
	static bool once = true;

	if (once)
	{
		once = false;
		if (terminate)
		{
			WtMessageBox(NULL,
			             "A fatal problem has been brought to the attention of " IDS_APPNAME ".\n"
			             "If you are able to, please capture a minidump before dismissing this message and send to "
			             "Whole Tomato support at http://www.wholetomato.com/contact .\n"
			             "Visual Studio will immediately terminate after pressing OK.",
			             IDS_APPNAME, MB_ICONERROR | MB_OK);
		}
		else
		{
			WtMessageBox(NULL,
			             "A fatal problem has been brought to the attention of " IDS_APPNAME ".\n"
			             "If you are able to, please capture a minidump before dismissing this message and send to "
			             "Whole Tomato support at http://www.wholetomato.com/contact .\n"
			             "We recommend restarting Visual Studio after pressing OK (if it doesn't crash).",
			             IDS_APPNAME, MB_ICONERROR | MB_OK);
		}

		// log stack without checking flags - this would have been a hard crash, get what we can
		VA_StackWalker texasRanger;
		texasRanger.ShowCallstack();
	}

	if (terminate)
	{
		// STATUS_CTX_RESPONSE_ERROR - some ridiculous status so we know it's from us
		TerminateProcess(GetCurrentProcess(), 0xC00A000AL);
	}
	else
	{
		// don't TerminateProcess like default vs2008 release build behavior
		// give user a chance to save file and exit if it doesn't otherwise crash
	}
}

void VaInvalidParameter(const wchar_t* expression, const wchar_t* function, const wchar_t* file, unsigned int line,
                        uintptr_t pReserved)
{
#ifdef _DEBUG
	CString functionName = function != nullptr ? CString(" in function: ") + CString(function) : CString("");
	VALogError(CString("CRT Exception InvalidParameter: ") + CString(file) + functionName, (int)line, TRUE);
#else
	VALogError(CString("CRT Exception InvalidParameter "), (int)line, TRUE);
#endif
	VaCrtError(false);
	// default handler will not terminate upon return
}

void VaPureCall()
{
	VALogError(CString("CRT Exception pure call"), 0, TRUE);
	VaCrtError(true);
	// abort will be called upon return if we don't terminate
}

void VaUnexpected()
{
	VALogError(CString("CRT Exception unexpected"), 0, TRUE);
	VaCrtError(true);
	// abort will be called upon return if we don't terminate
}

void VaTerminate()
{
	VALogError(CString("CRT Exception terminate"), 0, TRUE);
	VaCrtError(true);
	// abort will be called upon return if we don't terminate
}

void VaSetCrtErrorHandlers()
{
#if !defined(VA_CPPUNIT) // no details why excluded from unit tests -- see change 16188
	_ASSERTE(!_get_invalid_parameter_handler());
	// http://msdn.microsoft.com/en-us/library/a9yf33zb%28VS.80%29.aspx
	_set_invalid_parameter_handler(VaInvalidParameter);
#endif // VA_CPPUNIT
	// http://msdn.microsoft.com/en-us/library/t296ys27%28VS.80%29.aspx
	_set_purecall_handler(VaPureCall);
	// http://msdn.microsoft.com/en-us/library/7twc8dwy%28VS.80%29.aspx
#if !_HAS_CXX23
	set_unexpected(VaUnexpected);
#endif
	// http://msdn.microsoft.com/en-us/library/t6fk7h29%28VS.80%29.aspx
	set_terminate(VaTerminate);
}
