.text:40004D58; const CDebugProgram::`vftable'{for `IExprProcess'}
.text:40004D58 ? ? _7CDebugProgram@@6BIExprProcess@@@ dd offset ? QueryInterface@CDebugProgram@@W7AGJABU_GUID@@PAPAX@Z
.text:40004D58; DATA XREF : CDebugProgram::~CDebugProgram(void) + 1Co
.text:40004D58; CDebugProgram::CDebugProgram(bool, bool, IDebugEnvoy *, ulong) + 54o
.text:40004D58;[thunk]:CDebugProgram::QueryInterface`adjustor{8}' (_GUID const &,void * *)
.text:40004D5C                 dd offset ? AddRef@CDebugProgram@@W7AGKXZ;[thunk]:CDebugProgram::AddRef`adjustor{8}' (void)
.text:40004D60                 dd offset ? Release@CDebugProgram@@W7AGKXZ;[thunk]:CDebugProgram::Release`adjustor{8}' (void)
.text:40004D64                 dd offset ? GetTargetProcessor@CNativeProcess@@UAGJPAW4_MPT@@@Z; CNativeProcess::GetTargetProcessor(_MPT *)
.text:40004D68                 dd offset ? ReadMemory@CNativeProcess@@UAGJPBUADDR@@PAEKPAK@Z; CNativeProcess::ReadMemory(ADDR const *, uchar *, ulong, ulong *)
.text:40004D6C                 dd offset ? WriteMemory@CNativeProcess@@UAGJPBUADDR@@PAEKPAK@Z; CNativeProcess::WriteMemory(ADDR const *, uchar *, ulong, ulong *)
.text:40004D70                 dd offset ? FixupAddr@CNativeProcess@@UAGJPAUADDR@@@Z; CNativeProcess::FixupAddr(ADDR *)
.text:40004D74                 dd offset ? UnFixupAddr@CNativeProcess@@UAGJPAUADDR@@@Z; CNativeProcess::UnFixupAddr(ADDR *)
.text:40004D78                 dd offset ? GetSymbolHandler@CNativeProcess@@UAGJPAPAUISymbolHandler@@@Z; CNativeProcess::GetSymbolHandler(ISymbolHandler * *)
.text:40004D7C                 dd offset ? GetSymbolHandler2@CNativeProcess@@UAGJPAPAUISymbolHandler@@@Z; CNativeProcess::GetSymbolHandler2(ISymbolHandler * *)
.text:40004D80                 dd offset ? GetRegistryRoot@CNativeProcess@@UAGJPAGPAKH@Z; CNativeProcess::GetRegistryRoot(ushort *, ulong *, int)
.text:40004D84                 dd offset ? ? _ECDebugProgram@@O7AEPAXI@Z;[thunk]:CDebugProgram::`vector deleting destructor'`adjustor{8}' (uint)
.text:40004D88                 dd offset ? Destroy@CDebugProgram@@UAEXXZ; CDebugProgram::Destroy(void)
.text:40004D8C                 dd offset ? CreateBreakpoint@CNativeProcess@@MAEPAVCBreakpoint@@XZ; CNativeProcess::CreateBreakpoint(void)
.text:40004D90                 dd offset ? CreateModule@CDebugProgram@@EAEPAVCModule@@XZ; CDebugProgram::CreateModule(void)
.text:40004D94                 dd offset ? CreateStackFrame@CNativeProcess@@MAEPAVCStackFrame@@XZ; CNativeProcess::CreateStackFrame(void)
.text:40004D98                 dd offset ? CreateCallStack@CNativeProcess@@UAEPAVCCallStack@@XZ; CNativeProcess::CreateCallStack(void)
.text:40004D9C                 dd offset ? CreateExpression@CNativeProcess@@UAEPAUIExpression@@XZ; CNativeProcess::CreateExpression(void)
.text:40004DA0                 dd offset ? ClearCachedStackRanges@CDebugProgram@@UAEJK@Z; CDebugProgram::ClearCachedStackRanges(ulong)
.text:40004DA4                 dd offset ? IsNoStepInto@CDebugProgram@@EAE_NABVCStringW@dbg@@@Z; CDebugProgram::IsNoStepInto(dbg::CStringW const &)
.text:40004DA8                 dd offset ? GetCategoryMap@CDebugEngine@@SGPBU_ATL_CATMAP_ENTRY@ATL@@XZ; CDebugEngine::GetCategoryMap(void)
.text:40004DAC                 dd offset ? OnUserBreakpoint@CDebugProgram@@MAE ? AW4XOSD@@W4DBC@@PAVCNativeThread@@W4DEBUGBREAKTYPE@@@Z; CDebugProgram::OnUserBreakpoint(DBC, CNativeThread *, DEBUGBREAKTYPE)
.text:40004DB0                 dd offset ? OnAsyncStop@CDebugProgram@@MAE ? AW4XOSD@@W4DBC@@PAVCNativeThread@@JI@Z; CDebugProgram::OnAsyncStop(DBC, CNativeThread *, long, uint)
.text:40004DB4                 dd offset ? OnEntryPoint@CDebugProgram@@MAE ? AW4XOSD@@W4DBC@@PAVCNativeThread@@PAX@Z; CDebugProgram::OnEntryPoint(DBC, CNativeThread *, void *)
.text:40004DB8                 dd offset ? OnLoadComplete@CDebugProgram@@MAE ? AW4XOSD@@W4DBC@@PAVCNativeThread@@PAX@Z; CDebugProgram::OnLoadComplete(DBC, CNativeThread *, void *)
.text:40004DBC                 dd offset ? OnException@CDebugProgram@@MAE ? AW4XOSD@@W4DBC@@PAVCNativeThread@@PAU_EPR@@@Z; CDebugProgram::OnException(DBC, CNativeThread *, _EPR *)
.text:40004DC0                 dd offset ? OnIgnoredException@CDebugProgram@@MAE ? AW4XOSD@@W4DBC@@PAVCNativeThread@@PAU_EPR@@@Z; CDebugProgram::OnIgnoredException(DBC, CNativeThread *, _EPR *)
.text:40004DC4                 dd offset ? OnExceptionNotify@CDebugProgram@@MAE ? AW4XOSD@@W4DBC@@PAVCNativeThread@@PAU_EPR@@@Z; CDebugProgram::OnExceptionNotify(DBC, CNativeThread *, _EPR *)
.text:40004DC8                 dd offset ? OnRuntimeError@CDebugProgram@@MAE ? AW4XOSD@@W4DBC@@PAVCNativeThread@@PAU_tagRUNTIME_ERRORINFO@@@Z; CDebugProgram::OnRuntimeError(DBC, CNativeThread *, _tagRUNTIME_ERRORINFO *)
.text:40004DCC                 dd offset ? OnExecuteDone@CDebugProgram@@MAE ? AW4XOSD@@W4DBC@@PAVCNativeThread@@H@Z; CDebugProgram::OnExecuteDone(DBC, CNativeThread *, int)
.text:40004DD0                 dd offset ? OnCrtHook@CDebugProgram@@MAE ? AW4XOSD@@W4DBC@@PAVCNativeThread@@PAU_tagCRTHOOK_ERRORINFO@@@Z; CDebugProgram::OnCrtHook(DBC, CNativeThread *, _tagCRTHOOK_ERRORINFO *)
.text:40004DD4                 dd offset ? OnExecuteFailed@CDebugProgram@@MAE ? AW4XOSD@@W4DBC@@PAVCNativeThread@@@Z; CDebugProgram::OnExecuteFailed(DBC, CNativeThread *)
.text:40004DD8                 dd offset ? OnStep@CDebugProgram@@MAE ? AW4XOSD@@W4DBC@@PAVCNativeThread@@PAXPAUIEnumDebugBoundBreakpoints2@@@Z; CDebugProgram::OnStep(DBC, CNativeThread *, void *, IEnumDebugBoundBreakpoints2 *)
.text:40004DDC                 dd offset ? OnProcTerm@CDebugProgram@@MAE ? AW4XOSD@@W4DBC@@K@Z; CDebugProgram::OnProcTerm(DBC, ulong)
.text:40004DE0                 dd offset ? OnCreateThread@CDebugProgram@@MAE ? AW4XOSD@@W4DBC@@_K@Z; CDebugProgram::OnCreateThread(DBC, unsigned __int64)
.text:40004DE4                 dd offset ? OnThreadTerm@CDebugProgram@@MAE ? AW4XOSD@@W4DBC@@PAVCNativeThread@@K@Z; CDebugProgram::OnThreadTerm(DBC, CNativeThread *, ulong)
.text:40004DE8                 dd offset ? OnModLoad@CDebugProgram@@MAE ? AW4XOSD@@W4DBC@@PAVCNativeThread@@PAVCModule@@@Z; CDebugProgram::OnModLoad(DBC, CNativeThread *, CModule *)
.text:40004DEC                 dd offset ? OnModFree@CDebugProgram@@MAE ? AW4XOSD@@W4DBC@@PAVCNativeThread@@PAVCModule@@@Z; CDebugProgram::OnModFree(DBC, CNativeThread *, CModule *)
.text:40004DF0                 dd offset ? OnInfoAvail@CDebugProgram@@MAE ? AW4XOSD@@W4DBC@@PAVCNativeThread@@PAU_INFOAVAIL@@@Z; CDebugProgram::OnInfoAvail(DBC, CNativeThread *, _INFOAVAIL *)
.text:40004DF4                 dd offset ? OnError@CDebugProgram@@MAE ? AW4XOSD@@W4DBC@@PAVCNativeThread@@JI@Z; CDebugProgram::OnError(DBC, CNativeThread *, long, uint)
.text:40004DF8                 dd offset ? OnExitedFunction@CDebugProgram@@MAE ? AW4XOSD@@W4DBC@@PAVCNativeThread@@PAUADDR@@@Z; CDebugProgram::OnExitedFunction(DBC, CNativeThread *, ADDR *)
.text:40004DFC                 dd offset ? OnThreadNameChanged@CDebugProgram@@MAE ? AW4XOSD@@W4DBC@@PAVCNativeThread@@@Z; CDebugProgram::OnThreadNameChanged(DBC, CNativeThread *)
.text:40004E00                 dd offset ? DefaultHandler@CNativeProcess@@MAE ? AW4XOSD@@W4DBC@@PAVCNativeThread@@@Z; CNativeProcess::DefaultHandler(DBC, CNativeThread *)
.text:40004E04                 dd offset ? OnPreModLoad@CDebugProgram@@UAEXPAVCNativeThread@@PAVCModule@@@Z; CDebugProgram::OnPreModLoad(CNativeThread *, CModule *)




LONG __userpurge CDebugEngine::InitModeEngine<eax>(int a1<edi>, int a2)
{
	LONG result; // eax@1
	void *v3; // ecx@2
	LONG v4; // esi@4
	LONG v5; // [sp+0h] [bp-10h]@4

	result = InterlockedIncrement(&CDebugEngine::_gSentEngineCreate);
	if(result == 1)
	{
		v3 = *(void **)(a1 + 232);
		if(v3)
			CExecutionMap::LoadPatterns(v3);
		result = operator new(44);
		v4 = result;
		v5 = result;
		if(result)
		{
			result = CDebugEvent::CDebugEvent(result, 4, &_GUID_fe5b734c_759d_4e59_ab04_f103343bdd06, a1, 0);
			*(_DWORD *)v4 = &CGenericEvent<CDebugEngineCreateEvent_IDebugEngineCreateEvent2>::_vftable_;
			*(_DWORD *)(v4 + 40) = &CGenericEvent<CDebugEngineCreateEvent_IDebugEngineCreateEvent2>::_vftable_;
			*(_DWORD *)v4 = &CDebugEngineCreateEvent::_vftable_;
			*(_DWORD *)(v4 + 40) = &CDebugEngineCreateEvent::_vftable_;
		}
		else
		{
			v4 = 0;
		}
		if(v4)
		{
			(*(void(__cdecl **)(int, int, _DWORD, _DWORD, _DWORD, LONG, _DWORD, _DWORD, LONG))(*(_DWORD *)a2 + 12))(
				a2,
				a1,
				0,
				0,
				0,
				v4,
				*(_DWORD *)(v4 + 28),
				*(_DWORD *)(v4 + 24),
				v5);
			result = (*(int(__cdecl **)(LONG))(*(_DWORD *)v4 + 8))(v4);
		}
	}
	return result;
}



LSTATUS __thiscall CExecutionMap::LoadPatterns(void *this)
{
	unsigned int i; // edi@3
	int v2; // eax@4
	int v3; // esi@4
	int v4; // esi@7
	LSTATUS result; // eax@8
	int v6; // eax@11
	int v7; // eax@11
	char v8; // [sp+0h] [bp-23Ch]@2
	DWORD v9; // [sp+4h] [bp-238h]@2
	char v10; // [sp+8h] [bp-234h]@11
	char v11; // [sp+Ch] [bp-230h]@11
	unsigned int v12; // [sp+10h] [bp-22Ch]@2
	void *v13; // [sp+14h] [bp-228h]@1
	int v14; // [sp+18h] [bp-224h]@1
	DWORD cbData; // [sp+1Ch] [bp-220h]@1
	char *Str1; // [sp+20h] [bp-21Ch]@5
	HKEY hKey; // [sp+24h] [bp-218h]@1
	char v18; // [sp+28h] [bp-214h]@4
	const WCHAR SubKey; // [sp+2Ch] [bp-210h]@1
	const CHAR *v20; // [sp+218h] [bp-24h]@4
	int v21; // [sp+21Ch] [bp-20h]@4
	int v22; // [sp+220h] [bp-1Ch]@4
	BYTE *v23; // [sp+224h] [bp-18h]@4
	DWORD *v24; // [sp+228h] [bp-14h]@4
	int v25; // [sp+238h] [bp-4h]@4

	hKey = 0;
	v14 = 0;
	v13 = this;
	cbData = 256;
	CExecutionMap::Empty();
	CDebugSession::GetRegistryRoot((unsigned __int16 *)&SubKey, 0);
	StringCchCatW((unsigned __int16 *)&SubKey, 0x100u, L"\\StepOver");
	if(!RegOpenKeyExW(HKEY_LOCAL_MACHINE, &SubKey, 0, 0x20019u, &hKey))
	{
		if(!CExecutionMap::RetrieveValueNames((DWORD *)&v12, hKey, (void **)&v14, (DWORD *)&v8, &v9))
		{
			for(i = 0; i < v12; ++i)
			{
				dbg::CString::CString(&v18);
				v25 = 0;
				dbg::CString::CString((char *)&byte_40006440);
				LOBYTE(v25) = 1;
				v24 = &cbData;
				cbData = v9;
				v23 = (BYTE *)dbg::CString::GetBuffer(v9);
				v22 = 0;
				v21 = 0;
				v20 = *(const CHAR **)(v14 + 4 * i);
				RegQueryValueExA(hKey, v20, 0, 0, v23, v24);
				dbg::CString::GetBufferSetLength(cbData);
				v2 = dbg::CString::ReverseFind(61);
				v3 = v2;
				if(v2 >= 0)
				{
					v6 = dbg::CString::Mid(&v10, v2 + 1);
					LOBYTE(v25) = 2;
					dbg::CString::operator_(v6);
					LOBYTE(v25) = 1;
					dbg::CString::_CString(&v10);
					dbg::CString::TrimLeft(&Str1);
					dbg::CString::TrimRight(&Str1);
					v7 = dbg::CString::Left(&v11, v3);
					LOBYTE(v25) = 3;
					dbg::CString::operator_(v7);
					LOBYTE(v25) = 1;
					dbg::CString::_CString(&v11);
				}
				dbg::CString::TrimLeft(&v18);
				dbg::CString::TrimRight(&v18);
				if(__stricmp(Str1, "StepInto"))
					v24 = (DWORD *)1;
				else
					v24 = 0;
				CExecutionMap::Insert(&v18, v24);
				v4 = v14;
				v24 = *(DWORD **)(v14 + 4 * i);
				operator delete(v24);
				*(_DWORD *)(v4 + 4 * i) = 0;
				LOBYTE(v25) = 0;
				dbg::CString::_CString(&Str1);
				v25 = -1;
				dbg::CString::_CString(&v18);
			}
		}
	}
	result = operator delete__(v14);
	if(hKey)
		result = RegCloseKey(hKey);
	return result;
}



LSTATUS __userpurge CExecutionMap::RetrieveValueNames<eax>(DWORD *a1<edi>, HKEY hKey, void **a3, DWORD *lpcbMaxValueNameLen, DWORD *dwIndex)
{
	int v5; // eax@2
	CHAR *v6; // ebx@6
	size_t v7; // ecx@8
	void *v8; // eax@8
	size_t v9; // eax@10
	LSTATUS v11; // [sp+4h] [bp-Ch]@1
	DWORD cchValueName; // [sp+8h] [bp-8h]@5
	size_t NumOfElements; // [sp+Ch] [bp-4h]@3
	DWORD dwIndexa; // [sp+24h] [bp+14h]@3

	v11 = RegQueryInfoKeyA(hKey, 0, 0, 0, 0, 0, 0, a1, lpcbMaxValueNameLen, dwIndex, 0, 0);
	if(!v11)
	{
		v5 = operator new__(4 * *a1);
		*a3 = (void *)v5;
		if(v5)
		{
			NumOfElements = 0;
			for(dwIndexa = 0; dwIndexa < *a1; ++dwIndexa)
			{
				if(*lpcbMaxValueNameLen >= 0x7FFFFFFE)
					cchValueName = 2147483647;
				else
					cchValueName = *lpcbMaxValueNameLen + 1;
				v6 = (CHAR *)operator new__(cchValueName);
				if(v6)
				{
					v11 = RegEnumValueA(hKey, dwIndexa, v6, &cchValueName, 0, 0, 0, 0);
					if(!v11)
					{
						v7 = NumOfElements;
						v8 = *a3;
						++NumOfElements;
						*((_DWORD *)v8 + v7) = v6;
					}
				}
			}
			v9 = NumOfElements;
			*a1 = NumOfElements;
			_qsort(*a3, v9, 4u, keycompare);
		}
		else
		{
			v11 = 8;
		}
	}
	operator delete(0);
	return v11;
}

