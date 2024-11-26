#include "stdafxed.h"
#include "..\vaIPC\vaIPC\IpcClient.h"
#include "LOG.H"
#include "WTString.h"
#include "Settings.h"
#include "DllNames.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

class DbMtxClient : public vaIPC::Client::IpcClientConnectionCallbackBase
{
  public:
	DbMtxClient()
	{
		mResponseReceived = ::CreateEvent(nullptr, false, false, nullptr);
	}

	virtual ~DbMtxClient()
	{
	}

	virtual void OnError(const std::wstring& msg) override
	{
		_ASSERTE(!"DbMtxClient unexpected error");
		vLog("ERROR: dbMtx err(%s)", WTString(msg.c_str()).c_str());
		::SetEvent(mResponseReceived);
		CloseConnection();
	}

	virtual bool ReadData(const void* pData, size_t len, bool moreAvailable) override
	{
		std::wstring msg;
		try
		{
			std::wstring tmp((wchar_t*)pData, len / sizeof(wchar_t));
			msg = tmp;
		}
		catch (const std::exception&)
		{
			vLog("ERROR: dbMtx rd exc");
		}

		if (msg.empty())
		{
			_ASSERTE(!"DbMtxClient empty msg");
			vLog("ERROR: dbMtx rd sz");
			// treat errors as fatal (response received, but operation incomplete)
		}
		else if (msg[0] == L'l' && 0 == msg.find(L"log:"))
		{
			if (g_loggingEnabled)
			{
				WTString logMsg(msg.c_str());
				logMsg = logMsg.Mid(4);
				Log(logMsg.c_str());
			}

			// return without SetEvent or CloseConnection
			// server should continue to talk to us
			return true;
		}
		else if (msg[0] == L'E' && 0 == msg.find(L"ERROR:"))
		{
			if (g_loggingEnabled)
			{
				WTString logMsg(msg.c_str());
				Log(logMsg.c_str());
			}
			// treat errors as fatal (response received, but operation incomplete)
		}
		else if (len != mExpectedResponse.size() * sizeof(wchar_t))
		{
			_ASSERTE(!"DbMtxClient unexpected response");
			Log("ERROR: dbMtx rd ur 1");
			// treat errors as fatal (response received, but operation incomplete)
		}
		else if (!memcmp(pData, mExpectedResponse.c_str(), len))
		{
			mOperationCompleted = true;
			mExpectedResponse.clear();
		}
		else
		{
			_ASSERTE(!"DbMtxClient unexpected data");
			Log("ERROR: dbMtx rd ur 2");
			// treat errors as fatal (response received, but operation incomplete)
		}

		::SetEvent(mResponseReceived);

		// leave connection open if it's working properly
		if (!mOperationCompleted)
			CloseConnection();

		return false;
	}

	void SortLetterFiles(const CStringW& dbDir)
	{
		mExpectedResponse = L"dbmtx:S:complete";

		CStringW tmp;
		CString__FormatW(tmp, L"dbmtx:S:%d:%d:", Psettings->mUsePpl, Psettings->m_doLocaleChange ? 1 : 0);
		tmp += dbDir;
		RequestAndWait((LPCWSTR)tmp);
	}

	void PruneCacheFiles(const CStringW& dbDir)
	{
		mExpectedResponse = L"dbmtx:P:complete";
		CStringW tmp;
		CString__FormatW(tmp, L"dbmtx:P:%d:%d:", Psettings->mUsePpl, Psettings->m_doLocaleChange ? 1 : 0);
		tmp += dbDir;
		RequestAndWait((LPCWSTR)tmp);
	}

#if defined(VA_CPPUNIT)
	bool UnitTest(const CStringW& dbDir)
	{
		mExpectedResponse = L"dbmtx:T:complete";
		CStringW tmp(L"dbmtx:T:0:0:");
		tmp += dbDir;
		RequestAndWait((LPCWSTR)tmp);
		return mOperationCompleted;
	}
#endif

	void CloseConnection()
	{
		auto thrd = GetClientConnection();
		if (!thrd)
		{
			vLog("ERROR: dbMtx close -- missing connection");
			return;
		}

		vaIPC::Client::StopConnectionInstance(thrd->GetServiceInstance(), thrd->GetClientInstance());
	}

  private:
	void RequestAndWait(const std::wstring& msg)
	{
		auto thrd = GetClientConnection();
		if (!thrd)
		{
			vLog("ERROR: dbMtx req -- missing connection");
			return;
		}

		::ResetEvent(mResponseReceived);
		mOperationCompleted = false;
		thrd->RequestW(msg);
		DWORD res = ::WaitForSingleObject(mResponseReceived, 5 * 60000);
		if (res != WAIT_OBJECT_0)
		{
			vLog("ERROR: dbMtx req -- wait failure, closing");
			CloseConnection();
		}
		else if (!mOperationCompleted)
		{
			vLog("ERROR: dbMtx req -- unexpected state, closing");
			CloseConnection();
		}
	}

  private:
	bool mOperationCompleted;
	std::wstring mExpectedResponse;
	AutoHandle mResponseReceived;
};

using DbMtxClientPtr = std::shared_ptr<DbMtxClient>;

static const std::wstring exe(IDS_DBMTX_EXEW);
static const std::wstring dbMtxSvcName(L"DbMtx");
static const std::wstring dbMtxSvcInstanceName(dbMtxSvcName + L"-1");

void LaunchDbMtxServer()
{
	vaIPC::Client::LaunchServer(exe, dbMtxSvcName, dbMtxSvcInstanceName, [] {
		auto ret = std::make_shared<DbMtxClient>();
		ret->init();
		return ret;
	});
}

DbMtxClientPtr GetDbMtxConnection()
{
	LaunchDbMtxServer();

	const std::wstring clientInstanceName(L"DbMtxConnection-1");
	auto conn = vaIPC::Client::LaunchConnectionInstance<DbMtxClient>(dbMtxSvcInstanceName, clientInstanceName);
	return conn;
}

void DbMtxSort(const CStringW& dbDir)
{
	auto conn = GetDbMtxConnection();
	if (!conn)
	{
		_ASSERTE(!"DbMtxConnect failed");
		vLog("ERROR: dbMtx failed to start");
		return;
	}

	conn->SortLetterFiles(dbDir);
}

void DbMtxPrune(const CStringW& dbDir)
{
	auto conn = GetDbMtxConnection();
	if (!conn)
	{
		_ASSERTE(!"DbMtxConnect failed");
		vLog("ERROR: dbMtx failed to start");
		return;
	}

	conn->PruneCacheFiles(dbDir);
}

#if defined(VA_CPPUNIT)
bool DbMtxTest(const CStringW& dbDir)
{
	auto conn = GetDbMtxConnection();
	if (!conn)
	{
		_ASSERTE(!"DbMtxConnect failed");
		vLog("ERROR: dbMtx failed to start");
		return false;
	}

	return conn->UnitTest(dbDir);
}
#endif
