#pragma once

#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS // some CString constructors will be explicit
#define VSDEBUGENG_NO_ATL

#include <atlcom.h>
#pragma warning(push)
#pragma warning(disable : 4091)
#include <cor.h>
#pragma warning(pop)
#include <cordebug.h>

#ifndef _DEF_COR_GC_REFERENCE_
// CorGCReferenceType is not in: C:\Program Files (x86)\Microsoft SDKs\Windows\v7.1A\cordebug.h
// But is defined in: C:\Program Files (x86)\Windows Kits\8.1\Include\um\cordebug.h
// Typedef here to avoid having to change SDK version since we don't actually use CorGCReferenceType.
typedef void* CorGCReferenceType;

#ifdef _Field_size_opt_
#undef _Field_size_opt_
#endif

#endif

#pragma warning(push, 1)

#include <vsdebugeng.h>
#include <vsdebugeng.templates.h>

#if defined(VISUAL_ASSIST_X)
// EdCntrl project for concord plugin in VS2012-VS2013

#include "v1/VaDebuggerPluginService.Contract.h" // generated from VaDebuggerPlugin.vsdconfigxml by vsdconfigtool.exe

#else
// VaDebuggerPlugin project for concord plugin in VS2015+

#include "v2/VaDebuggerPluginService.Contract.h" // generated from VaDebuggerPluginV2.vsdconfigxml by vsdconfigtool.exe

#endif

#pragma warning(pop)

#if !defined(VISUAL_ASSIST_X)
#include "..\..\common\AutoHandle.h"
#endif

class ATL_NO_VTABLE VaDebuggerPluginService :
    // Inherit from VaDebuggerPluginServiceContract to provide the list of interfaces that
    // this class implements (interface list comes from VaDebuggerPluginV2.vsdconfigxml)
    public VaDebuggerPluginServiceContract,

    // Inherit from CComObjectRootEx to provide ATL support for reference counting and
    // object creation.
    public CComObjectRootEx<CComMultiThreadModel>,

    // Inherit from CComCoClass to provide ATL support for exporting this class from
    // DllGetClassObject
    public CComCoClass<VaDebuggerPluginService, &VaDebuggerPluginServiceContract::ClassId>
{
  protected:
	VaDebuggerPluginService();
	~VaDebuggerPluginService()
	{
	}

  public:
	DECLARE_NO_REGISTRY();
	DECLARE_NOT_AGGREGATABLE(VaDebuggerPluginService);

  public:
	// IDkmLanguageStepIntoFilterCallback methods
	HRESULT STDMETHODCALLTYPE GetStepIntoFlags(
	    Microsoft::VisualStudio::Debugger::Evaluation::DkmLanguageInstructionAddress* pLanguageInstructionAddress,
	    Microsoft::VisualStudio::Debugger::Stepping::DkmLanguageStepIntoFlags::e* pStepIntoFlags);

#if !defined(VISUAL_ASSIST_X)
	// IDkmCustomMessageCallbackReceiver
	HRESULT STDMETHODCALLTYPE
	SendHigher(_In_ Microsoft::VisualStudio::Debugger::DkmCustomMessage* pCustomMessage,
	           _Deref_out_opt_ Microsoft::VisualStudio::Debugger::DkmCustomMessage** ppReplyMessage);

  private:
	AutoHandle mSlot;
	DWORD32 mVaStepFilterServiceConnectionId = 0;

	void MakeSlot();
#endif
};

OBJECT_ENTRY_AUTO(VaDebuggerPluginService::ClassId, VaDebuggerPluginService)
