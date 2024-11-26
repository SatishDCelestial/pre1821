#include "stdafx.h"
#include "DebuggerToolsCpp.h"
#include "VaDebuggerPluginService.h"
#include "..\VaPkg\VaPkg\VaStepFilterServerId.h"
#include "utils3264.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

using namespace Microsoft::VisualStudio::Debugger;

VaDebuggerPluginService::VaDebuggerPluginService()
{
	// no point in throwing - they try to instantiate us at every step into
	// 	if (!Psettings || !Psettings->m_enableVA || !Psettings->mEnableDebuggerStepFilter)
	// 		AfxThrowNotSupportedException();
	//
}

#if !defined(VISUAL_ASSIST_X)
void VaDebuggerPluginService::MakeSlot()
{
	mVaStepFilterServiceConnectionId = GetTickCount();

	CStringW slotName;
	CString__FormatW(slotName, kSlotNameFormatStr, mVaStepFilterServiceConnectionId);

	HANDLE hSlot = CreateMailslotW(slotName,
	                               10,  // max size
	                               100, // timeout
	                               (LPSECURITY_ATTRIBUTES) nullptr);

	DWORD err = GetLastError();
	(void)err;
	if (hSlot == INVALID_HANDLE_VALUE)
		return;

	mSlot = hSlot;
}
#endif

HRESULT STDMETHODCALLTYPE
VaDebuggerPluginService::GetStepIntoFlags(Evaluation::DkmLanguageInstructionAddress* pLanguageInstructionAddress,
                                          Stepping::DkmLanguageStepIntoFlags::e* pStepIntoFlags)
{
	if (!pLanguageInstructionAddress)
		return S_OK;

	if (!pStepIntoFlags)
		return pLanguageInstructionAddress->GetStepIntoFlags(pStepIntoFlags); // ?

	CComPtr<DkmString> pName;
	if (SUCCEEDED(pLanguageInstructionAddress->GetMethodName(Evaluation::DkmVariableInfoFlags::None, &pName)))
	{
		// 		CComPtr<Evaluation::DkmLanguage> pLang(pLanguageInstructionAddress->Language());
		// 		if (pLang)
		// 		{
		// 			CComPtr<DkmString> pLangName(pLang->Name());
		// 			if (pLangName)
		// 			{
		// 			}
		// 		}

		DWORD32 ret = 0;

#if defined(VISUAL_ASSIST_X)
		// use this when the file is compiled into VA_X.dll
		ret = ::IsStepIntoSkipped(pName->Value());

#else
		// file compiled into separate debugger plugin dll

		// Consider: we could check in ctor at runtime to see if we are in devenv process.
		// If so, GetVaService()->GetVaDebuggerService() and call IsStepIntoSkipped on that
		// interface instead of the remoting machinations below

		// [case: 132670]
#ifdef _DEBUG
#define DbgErrorBrk(assertMsg)                                                                                         \
	{                                                                                                                  \
		static bool doAssertOnce = true;                                                                               \
		if (doAssertOnce)                                                                                              \
		{                                                                                                              \
			_ASSERTE(!assertMsg);                                                                                      \
			doAssertOnce = false;                                                                                      \
		}                                                                                                              \
		break;                                                                                                         \
	}
#else
#define DbgErrorBrk(assertMsg)                                                                                         \
	{                                                                                                                  \
		break;                                                                                                         \
	}
#endif
		for (;;)
		{
			if (!mSlot)
				MakeSlot();

			if (!mSlot)
				DbgErrorBrk("no mailslot");

			CComPtr<DkmVariant> dv1, dv2;
			DkmVariant::Create((BYTE*)pName->Value(), (pName->Length() + 1) * sizeof(WCHAR), &dv1);

			VARIANT v2;
			v2.vt = VT_UI4;
			v2.uintVal = mVaStepFilterServiceConnectionId;
			DkmVariant::Convert(&v2, &dv2);

			if (!dv1 || !dv2)
				DbgErrorBrk("failed to create DkmVariant");

			CComPtr<DkmCustomMessage> msg;
			CComPtr<DkmRuntimeInstance> rti = pLanguageInstructionAddress->RuntimeInstance();
			DkmCustomMessage::Create(rti->Connection(), rti->Process(), ClassId, DbgStepFilteStepIntoMsg, dv1, dv2,
			                         &msg);
			if (!msg)
				DbgErrorBrk("failed to create DkmCustomMessage");

			HRESULT hRes = msg->SendToVsService(IID_SVaStepFilterService, true);
			if (S_OK != hRes)
				DbgErrorBrk("SendToVsService failed");

			DWORD msgCnt, msgSize;
			BOOL bRes = ::GetMailslotInfo(mSlot, nullptr, &msgSize, &msgCnt, nullptr);

			if (!bRes)
				DbgErrorBrk("failed to get slot info");

			if (!msgCnt)
				DbgErrorBrk("unexpected slot msgCnt");

			if (msgSize != sizeof(DWORD32))
				DbgErrorBrk("unexpected slot msgSize");

			DWORD bytesRead;
			bRes = ::ReadFile(mSlot, &ret, msgSize, &bytesRead, nullptr);

			if (!bRes || bytesRead != msgSize)
				DbgErrorBrk("failed to read slot");

			break;
		}
#endif
		if (ret)
		{
			if (LOWORD(ret))
			{
				// skip (don't step into)
				*pStepIntoFlags = Stepping::DkmLanguageStepIntoFlags::NoStepInto;
			}
			else
			{
				// don't skip (do step into)
				// treat disabled filter differently than non-existent filter by
				// not checking the step filter chain
				*pStepIntoFlags = Stepping::DkmLanguageStepIntoFlags::None;
			}

			return S_OK;
		}
	}

	// check step filter chain (which includes user-defined natStepFilter files)
	return pLanguageInstructionAddress->GetStepIntoFlags(pStepIntoFlags);
}

#if !defined(VISUAL_ASSIST_X)
HRESULT STDMETHODCALLTYPE VaDebuggerPluginService::SendHigher(_In_ DkmCustomMessage* pCustomMessage,
                                                              _Deref_out_opt_ DkmCustomMessage** ppReplyMessage)
{
	(void)pCustomMessage;
	(void)ppReplyMessage;
	// this was an attempt at the callback mentioned at
	// https://developercommunity.visualstudio.com/content/problem/408961/concord-debugger-plugin-not-called-when-load-debug.html
	return S_OK;
}
#endif
