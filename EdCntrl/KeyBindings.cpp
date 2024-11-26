#include "StdAfxEd.h"
#include "PROJECT.H"
#include "DevShellAttributes.h"
#include "GenericTreeDlg.h"
#include "BuildInfo.h"
#include "TokenW.h"
#include "DevShellService.h"
#include "ShellListener.h"
#include "VAService.h"
#include "Registry.h"
#include "RegKeys.h"
#include "DllNames.h"
#include "Library.h"
#include "SubClassWnd.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#if !defined(RAD_STUDIO)
typedef INT_PTR(_cdecl* ShowOptKeyBindsDlgFn)(bool);
typedef INT_PTR(_cdecl* ShowOptKeyBindsDlgForcedFn)();
typedef INT_PTR(_cdecl* ShowExistingKeyBindsDlgFn)(void*);

struct VaBindingOverride
{
	VaBindingOverride() : mUserEnabled(0xff), mDefault(false)
	{
	}

	VaBindingOverride(LPCWSTR cmd, LPCWSTR binding, LPCWSTR scope, bool byDefault)
	    : mCommandName(cmd), mBinding(binding), mCommandScope(scope), mUserEnabled(0xff), mDefault(byDefault)
	{
	}

	CStringW mCommandName;
	CStringW mBinding;
	CStringW mCommandScope;
	byte mUserEnabled; // true, false or unset (0xff)
	bool mDefault;
	UINT mRegistryIdx = UINT_MAX; // used also to determine if the defaults have been read
};

typedef std::vector<VaBindingOverride> VaBindingOverrides;

#define kBindingsSubKeyName _T("\\CommandBindings")
#define kBindingsDlgFuncName "ShowRcmdKeyBindingsDialog"
#define kExistingKeyBindsDlgFuncName "ShowExistingKeyBindingsDialog"
#define kKeyBindingsCheck "KeyBindingsCheck"
#define kKeyBindingsDay "KeyBindingsDay"
#define kKeyBindingsDialogDisplayed "KeyBindingsDialogDisplayed"
#define kOptKeyBindsDialogsClass L"VaOptKeyBinds.Dialogs"
#ifdef _WIN64
#define kOptKeyBindsDLL L"VaOptKeyBinds.17.dll"
#else
#define kOptKeyBindsDLL L"VaOptKeyBinds.dll"
#endif

#define ID_INTL_BASE 13000u

#define ID_SCOPE_Global ID_INTL_BASE + 18
#define ID_SCOPE_TextEditor ID_INTL_BASE + 22

#define ID_KEY_AccelControl ID_INTL_BASE + 358
#define ID_KEY_AccelAlt ID_INTL_BASE + 359
#define ID_KEY_AccelShift ID_INTL_BASE + 360
#define ID_KEY_VK_Back ID_INTL_BASE + 361
#define ID_KEY_VK_Tab ID_INTL_BASE + 362
#define ID_KEY_VK_Cancel ID_INTL_BASE + 363
#define ID_KEY_VK_Pause ID_INTL_BASE + 364
#define ID_KEY_VK_Spacebar ID_INTL_BASE + 365
#define ID_KEY_VK_Prior ID_INTL_BASE + 366
#define ID_KEY_VK_Next ID_INTL_BASE + 367
#define ID_KEY_VK_Home ID_INTL_BASE + 368
#define ID_KEY_VK_Insert ID_INTL_BASE + 369
#define ID_KEY_VK_Delete ID_INTL_BASE + 370
#define ID_KEY_VK_F1 ID_INTL_BASE + 371
#define ID_KEY_VK_F2 ID_INTL_BASE + 372
#define ID_KEY_VK_F3 ID_INTL_BASE + 373
#define ID_KEY_VK_F4 ID_INTL_BASE + 374
#define ID_KEY_VK_F5 ID_INTL_BASE + 375
#define ID_KEY_VK_F6 ID_INTL_BASE + 376
#define ID_KEY_VK_F7 ID_INTL_BASE + 377
#define ID_KEY_VK_F8 ID_INTL_BASE + 378
#define ID_KEY_VK_F9 ID_INTL_BASE + 379
#define ID_KEY_VK_F10 ID_INTL_BASE + 380
#define ID_KEY_VK_F11 ID_INTL_BASE + 381
#define ID_KEY_VK_F12 ID_INTL_BASE + 382
#define ID_KEY_VK_F13 ID_INTL_BASE + 383
#define ID_KEY_VK_F14 ID_INTL_BASE + 384
#define ID_KEY_VK_F15 ID_INTL_BASE + 385
#define ID_KEY_VK_F16 ID_INTL_BASE + 386
#define ID_KEY_VK_F17 ID_INTL_BASE + 2231
#define ID_KEY_VK_F18 ID_INTL_BASE + 2232
#define ID_KEY_VK_F19 ID_INTL_BASE + 2233
#define ID_KEY_VK_F20 ID_INTL_BASE + 2234
#define ID_KEY_VK_F21 ID_INTL_BASE + 2235
#define ID_KEY_VK_F22 ID_INTL_BASE + 2236
#define ID_KEY_VK_F23 ID_INTL_BASE + 2237
#define ID_KEY_VK_F24 ID_INTL_BASE + 2238
#define ID_KEY_VK_Left ID_INTL_BASE + 387
#define ID_KEY_VK_Right ID_INTL_BASE + 388
#define ID_KEY_VK_Up ID_INTL_BASE + 389
#define ID_KEY_VK_Down ID_INTL_BASE + 390
#define ID_KEY_VK_End ID_INTL_BASE + 391
#define ID_KEY_VK_Return ID_INTL_BASE + 392
#define ID_KEY_VK_Escape ID_INTL_BASE + 393
#define ID_KEY_VK_CLEAR ID_INTL_BASE + 394
#define ID_KEY_VK_LWin ID_INTL_BASE + 875
#define ID_KEY_VK_RWin ID_INTL_BASE + 876
#define ID_KEY_VK_NUMPAD0 ID_INTL_BASE + 1143
#define ID_KEY_VK_NUMPAD1 ID_INTL_BASE + 1144
#define ID_KEY_VK_NUMPAD2 ID_INTL_BASE + 1145
#define ID_KEY_VK_NUMPAD3 ID_INTL_BASE + 1146
#define ID_KEY_VK_NUMPAD4 ID_INTL_BASE + 1147
#define ID_KEY_VK_NUMPAD5 ID_INTL_BASE + 1148
#define ID_KEY_VK_NUMPAD6 ID_INTL_BASE + 1149
#define ID_KEY_VK_NUMPAD7 ID_INTL_BASE + 1150
#define ID_KEY_VK_NUMPAD8 ID_INTL_BASE + 1151
#define ID_KEY_VK_NUMPAD9 ID_INTL_BASE + 1152
#define ID_KEY_VK_MULTIPLY ID_INTL_BASE + 1153
#define ID_KEY_VK_ADD ID_INTL_BASE + 1154
#define ID_KEY_VK_SUBTRACT ID_INTL_BASE + 1155
#define ID_KEY_VK_DECIMAL ID_INTL_BASE + 1156
#define ID_KEY_VK_DIVIDE ID_INTL_BASE + 1157

enum class GuidIdx : UINT
{
	ShellPkg,

	GlobalScope,
	TextEditorScope,
};

enum class ScopeIdx : UINT
{
	Global,
	TextEditor,

	Count,

	Invalid = 0xffffffff
};

struct KeyData
{
	UINT vk;
	UINT rsc_id;
	LPCWSTR name;
};

struct ScopeData
{
	UINT rsc_id;
	LPCWSTR name;
	GuidIdx package;
};

std::map<UINT, CStringW> localizedKeys;   // Localized Name for each vk in rgkdKeys
std::map<UINT, CStringW> localizedScopes; // Localized Name for each ID_SCOPE
bool localizedMapsInitialized = false;

const GUID guids[]{
    {0xDA9FB551, 0xC724, 0x11d0, {0xAE, 0x1F, 0x00, 0xA0, 0xC9, 0x0F, 0xFF, 0xC3}}, // GuidId::ShellPkg
    {0x5EFC7975, 0x14BC, 0x11CF, {0x9B, 0x2B, 0x00, 0xAA, 0x00, 0x57, 0x38, 0x19}}, // GuidId::GlobalScope
    {0x8B382828, 0x6202, 0x11D1, {0x88, 0x70, 0x00, 0x00, 0xF8, 0x75, 0x79, 0xD2}}, // GuidId::TextEditorScope
};

// #KeyBindingsScopeDefinitions
const ScopeData rgkdScopes[] = {
    // The names for all the scopes are under the KeyBindingTables registry key.
    // see: HKEY_CURRENT_USER\Software\Microsoft\VisualStudio\10.0_Config\KeyBindingTables
    //
    // The subkey name is the scope GUID, the default value for the subkey is
    // the resource ID used to retrieve the name and the package value under the
    // subkey is the package to pass to LoadPackageString (along with the resource id).
    // The one caveat is the global one is actually incorrect,
    // the Global scope is 5efc7975-14bc-11cf-9b2b-00AA00573819
    // and it says that the resource id is 13021, but it is actually 13018

    {ID_SCOPE_Global, L"Global", GuidIdx::ShellPkg},         // ScopeIdx::Global
    {ID_SCOPE_TextEditor, L"Text Editor", GuidIdx::ShellPkg} // ScopeIdx::TextEditor
};

// #KeyBindingsKeyDefinitions
const KeyData rgkdKeys[] = {
    {VK_F1, ID_KEY_VK_F1, L"F1"},
    {VK_F2, ID_KEY_VK_F2, L"F2"},
    {VK_F3, ID_KEY_VK_F3, L"F3"},
    {VK_F4, ID_KEY_VK_F4, L"F4"},
    {VK_F5, ID_KEY_VK_F5, L"F5"},
    {VK_F6, ID_KEY_VK_F6, L"F6"},
    {VK_F7, ID_KEY_VK_F7, L"F7"},
    {VK_F8, ID_KEY_VK_F8, L"F8"},
    {VK_F9, ID_KEY_VK_F9, L"F9"},
    {VK_F10, ID_KEY_VK_F10, L"F10"},
    {VK_F11, ID_KEY_VK_F11, L"F11"},
    {VK_F12, ID_KEY_VK_F12, L"F12"},
    {VK_F13, ID_KEY_VK_F13, L"F13"},
    {VK_F14, ID_KEY_VK_F14, L"F14"},
    {VK_F15, ID_KEY_VK_F15, L"F15"},
    {VK_F16, ID_KEY_VK_F16, L"F16"},
    {VK_F17, ID_KEY_VK_F17, L"F17"},
    {VK_F18, ID_KEY_VK_F18, L"F18"},
    {VK_F19, ID_KEY_VK_F19, L"F19"},
    {VK_F20, ID_KEY_VK_F20, L"F20"},
    {VK_F21, ID_KEY_VK_F21, L"F21"},
    {VK_F22, ID_KEY_VK_F22, L"F22"},
    {VK_F23, ID_KEY_VK_F23, L"F23"},
    {VK_F24, ID_KEY_VK_F24, L"F24"},
    {VK_CANCEL, ID_KEY_VK_Cancel, L"Break"},
    {VK_BACK, ID_KEY_VK_Back, L"Bkspce"},
    {VK_TAB, ID_KEY_VK_Tab, L"Tab"},
    {VK_PAUSE, ID_KEY_VK_Pause, L"Break"},
    {VK_SPACE, ID_KEY_VK_Spacebar, L"Space"},
    {VK_PRIOR, ID_KEY_VK_Prior, L"PgUp"},
    {VK_NEXT, ID_KEY_VK_Next, L"PgDn"},
    {VK_HOME, ID_KEY_VK_Home, L"Home"},
    {VK_INSERT, ID_KEY_VK_Insert, L"Ins"},
    {VK_DELETE, ID_KEY_VK_Delete, L"Del"},
    {VK_LEFT, ID_KEY_VK_Left, L"Left Arrow"},
    {VK_RIGHT, ID_KEY_VK_Right, L"Right Arrow"},
    {VK_UP, ID_KEY_VK_Up, L"Up Arrow"},
    {VK_DOWN, ID_KEY_VK_Down, L"Down Arrow"},
    {VK_RETURN, ID_KEY_VK_Return, L"Enter"},
    {VK_END, ID_KEY_VK_End, L"End"},
    {VK_ESCAPE, ID_KEY_VK_Escape, L"Esc"},
    {VK_LWIN, ID_KEY_VK_LWin, L"Left Windows"},
    {VK_RWIN, ID_KEY_VK_RWin, L"Right Windows"},
    {VK_NUMPAD0, ID_KEY_VK_NUMPAD0, L"Num 0"},
    {VK_NUMPAD1, ID_KEY_VK_NUMPAD1, L"Num 1"},
    {VK_NUMPAD2, ID_KEY_VK_NUMPAD2, L"Num 2"},
    {VK_NUMPAD3, ID_KEY_VK_NUMPAD3, L"Num 3"},
    {VK_NUMPAD4, ID_KEY_VK_NUMPAD4, L"Num 4"},
    {VK_NUMPAD5, ID_KEY_VK_NUMPAD5, L"Num 5"},
    {VK_NUMPAD6, ID_KEY_VK_NUMPAD6, L"Num 6"},
    {VK_NUMPAD7, ID_KEY_VK_NUMPAD7, L"Num 7"},
    {VK_NUMPAD8, ID_KEY_VK_NUMPAD8, L"Num 8"},
    {VK_NUMPAD9, ID_KEY_VK_NUMPAD9, L"Num 9"},
    {VK_MULTIPLY, ID_KEY_VK_MULTIPLY, L"Num *"},
    {VK_ADD, ID_KEY_VK_ADD, L"Num +"},
    {VK_SUBTRACT, ID_KEY_VK_SUBTRACT, L"Num -"},
    {VK_DECIMAL, ID_KEY_VK_DECIMAL, L"Num ."},
    {VK_DIVIDE, ID_KEY_VK_DIVIDE, L"Num /"},
    {VK_CLEAR, ID_KEY_VK_CLEAR, L"Clear"}, // Numpad 5 when Num Lock is off
};

void UpdateKeyBindings(const VaBindingOverrides& bindingVals);

WTString GetDTEBinding(LPCSTR cmdStr)
{
	WTString binding;
	if (!gDte)
		return binding;

	CComPtr<EnvDTE::Commands> pCmds;
	CComPtr<EnvDTE::Command> pCmd;
	gDte->get_Commands(&pCmds);
	if (pCmds)
		pCmds->Item(CComVariant(cmdStr), 0, &pCmd);

	if (pCmd)
	{
		CComVariant bindingVar;
		pCmd->get_Bindings(&bindingVar);
		if (bindingVar.parray)
		{
			// [case: 73420] give preference to Global or Edit
			WTString editBinding, globalBinding;
			CComVariant bindVar;
			for (long idx = 0; SafeArrayGetElement(bindingVar.parray, &idx, &bindVar) == S_OK; ++idx)
			{
				WTString curBinding = (const char*)CString(CStringW(bindVar));
				if (binding.IsEmpty())
				{
					// default to first
					binding = curBinding;
				}

				if (-1 != curBinding.Find("Text Editor"))
				{
					if (editBinding.IsEmpty())
					{
						// only keep first
						editBinding = curBinding;
					}
				}
				else if (-1 != curBinding.Find("Global"))
				{
					if (globalBinding.IsEmpty())
					{
						// only keep first
						globalBinding = curBinding;
					}
				}
			}

			if (!editBinding.IsEmpty())
				binding = editBinding;
			else if (!globalBinding.IsEmpty())
				binding = globalBinding;
		}
	}

	return binding;
}

bool InitLocalizedMaps()
{
	if (!localizedMapsInitialized)
	{
		if (g_IdeSettings && g_IdeSettings->IsLocalized())
		{
			CComPtr<IVsShell> shell(GetVsShell());
			_ASSERTE(shell != nullptr);

			if (shell)
			{
				LCID lcid = g_IdeSettings->GetLocaleID();

				for (const auto& kd : rgkdKeys)
				{
					CComBSTR bstrOut;
					if (SUCCEEDED(shell->LoadPackageString(guids[(int)GuidIdx::ShellPkg], kd.rsc_id, &bstrOut)))
					{
						// don't add equal strings
						if (CSTR_EQUAL !=
						    CompareStringW(lcid, NORM_IGNORECASE, kd.name, -1, bstrOut, (int)bstrOut.Length()))
							localizedKeys[kd.vk] = bstrOut;
					}
				}

				for (const auto& sd : rgkdScopes)
				{
					CComBSTR bstrOut;
					if (SUCCEEDED(shell->LoadPackageString(guids[(int)sd.package], sd.rsc_id, &bstrOut)))
					{
						// don't add equal strings
						if (CSTR_EQUAL !=
						    CompareStringW(lcid, NORM_IGNORECASE, sd.name, -1, bstrOut, (int)bstrOut.Length()))
							localizedScopes[sd.rsc_id] = bstrOut;
					}
				}
			}
		}

		localizedMapsInitialized = true;
	}

	return !localizedScopes.empty() || !localizedKeys.empty();
}

CStringW GetScopeName(ScopeIdx scope, bool localized)
{
	_ASSERTE(scope < ScopeIdx::Count);

	if (!localized)
		return rgkdScopes[(int)scope].name;

	InitLocalizedMaps();

	UINT rscId = rgkdScopes[(int)scope].rsc_id;

	auto found = localizedScopes.find(rscId);

	if (found == localizedScopes.end())
		return rgkdScopes[(int)scope].name;

	return found->second;
}

ScopeIdx GetScopeNameId(CStringW scope, bool localized)
{
	if (scope.IsEmpty())
		return ScopeIdx::Invalid;

	if (!localized)
	{
		for (UINT i = 0; i < (UINT)ScopeIdx::Count; i++)
		{
			CStringW cmp = GetScopeName((ScopeIdx)i, false);
			if (scope.CompareNoCase(cmp) == 0)
				return (ScopeIdx)i;
		}
	}
	else
	{
		LCID lcid = g_IdeSettings->GetLocaleID();

		for (UINT i = 0; i < (UINT)ScopeIdx::Count; i++)
		{
			CStringW cmp = GetScopeName((ScopeIdx)i, true);
			if (CSTR_EQUAL == CompareStringW(lcid, NORM_IGNORECASE, cmp, cmp.GetLength(), scope, scope.GetLength()))
				return (ScopeIdx)i;
		}
	}

	return ScopeIdx::Invalid;
}

CStringW LocalizeScope(CStringW scope, bool unlocalize = false)
{
	if (scope.IsEmpty())
		return scope;

	auto id = GetScopeNameId(scope, unlocalize);

	_ASSERTE(id != ScopeIdx::Invalid);

	if (id != ScopeIdx::Invalid)
		return GetScopeName(id, !unlocalize);

	return scope;
}

CStringW UnlocalizeScope(CStringW scope, bool unlocalize = false)
{
	return LocalizeScope(scope, true);
}

CStringW LocalizeKey(CStringW key, bool unlocalize = false)
{
	if (key.IsEmpty())
		return key;

	if (g_IdeSettings == nullptr || !g_IdeSettings->IsLocalized())
		return key;

	if (!InitLocalizedMaps())
		return key;

	LCID lcid = g_IdeSettings->GetLocaleID();

	if (unlocalize)
	{
		for (const auto& kvp : localizedKeys)
		{
			// find matching
			if (CSTR_EQUAL ==
			    CompareStringW(lcid, NORM_IGNORECASE, kvp.second, kvp.second.GetLength(), key, key.GetLength()))
			{
				// find English variant
				for (const auto& kd : rgkdKeys)
					if (kd.vk == kvp.first)
						return kd.name;

				_ASSERTE(!"Not found key name!");

				break;
			}
		}
	}
	else
	{
		for (const auto& kd : rgkdKeys)
		{
			if (CSTR_EQUAL == CompareStringW(lcid, NORM_IGNORECASE, kd.name, -1, key, key.GetLength()))
			{
				// find localized variant

				auto it = localizedKeys.find(kd.vk);

				if (it != localizedKeys.end())
					return it->second;

				return key; // not mapped, return default name
			}
		}
	}

	return key;
}

CStringW UnlocalizeKey(CStringW key)
{
	return LocalizeKey(key, true);
}

int SplitKeyBindingString(CStringW binding, CStringW& scope, CStringW& gesture1, CStringW& gesture2)
{
	int pos1 = binding.Find(L"::");
	if (pos1 != -1)
	{
		scope = binding.Mid(0, pos1);

		pos1 += 2; // step over ::

		int pos2 = binding.Find(L", ", pos1);
		if (pos2 == -1)
		{
			gesture1 = binding.Mid(pos1);
			return 1;
		}

		gesture1 = binding.Mid(pos1, pos2 - pos1);
		gesture2 = binding.Mid(pos2 + 2);
		return 2;
	}

	return 0;
}

CStringW CombineKeyBindingString(const CStringW& scope, const CStringW& gesture1, const CStringW& gesture2)
{
	CStringW rslt = scope + L"::" + gesture1;

	if (!gesture2.IsEmpty())
		rslt += L", " + gesture2;

	return rslt;
}

template <typename TFunc> void ForEachKeyInKeyGesture(CStringW gesture, TFunc func)
{
	if (gesture.IsEmpty())
		return;

	_ASSERTE(gesture.Find(L"::") == -1);
	_ASSERTE(gesture.Find(L", ") == -1);

	CStringW token;
	int curPos = 0;

	// splits by '+' not preceded by ' ' or '+'
	auto read_key = [](CStringW& token, const CStringW& src_str, int& curPos) -> bool {
		if (curPos < src_str.GetLength())
		{
			for (int i = curPos + 1; i < src_str.GetLength(); i++)
			{
				if (src_str[i] == '+' && src_str[i - 1] != ' ' && src_str[i - 1] != '+')
				{
					token = src_str.Mid(curPos, i - curPos).Trim();
					curPos = i + 1;
					return true;
				}
			}

			token = src_str.Mid(curPos).Trim();
			curPos = src_str.GetLength();
			return true;
		}

		return false;
	};

	// pass key by key
	while (read_key(token, gesture, curPos))
	{
		func(token);
	};
}

CStringW LocalizeKeyGesture(CStringW gesture, bool unlocalize = false)
{
	if (gesture.IsEmpty())
		return gesture;

	if (!g_IdeSettings || !g_IdeSettings->IsLocalized())
		return gesture;

	CStringW rslt;
	bool first = true;

	// localize key by key
	ForEachKeyInKeyGesture(gesture, [&](CStringW token) {
		if (first)
			first = false;
		else
			rslt += L"+";

		rslt += LocalizeKey(token, unlocalize);
	});

	return rslt;
}

CStringW UnlocalizeKeyGesture(CStringW gesture)
{
	return LocalizeKeyGesture(gesture, true);
}

CStringW SortModifierKeysInKeyGesture(CStringW gesture)
{
	if (gesture.IsEmpty())
		return gesture;

	std::vector<CStringW> keys;

	ForEachKeyInKeyGesture(gesture, [&](CStringW token) { keys.push_back(token); });

	if (keys.size() > 1)
		std::sort(keys.begin(), keys.end() - 1);

	CStringW rslt;
	for (const CStringW& k : keys)
	{
		if (!rslt.IsEmpty())
			rslt.AppendChar('+');

		rslt.Append(k);
	}

	return rslt;
}

bool KeyBindingScopeIsSupported(CStringW binding, bool localized)
{
	if (g_IdeSettings == nullptr || !g_IdeSettings->IsLocalized())
		return true;

	CStringW scope, s1, s2;
	if (SplitKeyBindingString(binding, scope, s1, s2))
		return GetScopeNameId(scope, localized) != ScopeIdx::Invalid;

	return false;
}

CStringW KeyBindingMakeComparable(CStringW binding, bool localized)
{
	_ASSERTE(binding.Find(L"::") > 0);

	if (g_IdeSettings == nullptr || !g_IdeSettings->IsLocalized())
		return binding;

	CStringW scope, s1, s2;
	if (SplitKeyBindingString(binding, scope, s1, s2))
	{
		if (localized)
		{
			scope = UnlocalizeScope(scope);
			s1 = UnlocalizeKeyGesture(s1);
			s2 = UnlocalizeKeyGesture(s2);
		}

		s1 = SortModifierKeysInKeyGesture(s1);

		CStringW rslt = scope;
		rslt += L"::";
		rslt += s1;

		if (!s2.IsEmpty())
		{
			s2 = SortModifierKeysInKeyGesture(s2);
			rslt += L", ";
			rslt += s2;
		}

		return rslt;
	}

	_ASSERTE(!"Invalid binding!");

	return binding;
}

int CompareKeyBindings(CStringW binding1, bool localized1, CStringW binding2, bool localized2)
{
	binding1 = KeyBindingMakeComparable(binding1, localized1);
	binding2 = KeyBindingMakeComparable(binding2, localized2);
	return binding1.CompareNoCase(binding2);
}

int CompareKeyBindings2(CStringW comparableBinding, CStringW binding2, bool localized2)
{
	binding2 = KeyBindingMakeComparable(binding2, localized2);
	return comparableBinding.CompareNoCase(binding2);
}

CStringW KeyBindingMakeAssignable(CStringW binding, bool localized)
{
	if (g_IdeSettings == nullptr || !g_IdeSettings->IsLocalized())
		return binding;

	// Assignable binging is one with localized scope and English key names

	_ASSERTE(binding.Find(L"::") > 0);

	CStringW scope, ges1, ges2;
	if (SplitKeyBindingString(binding, scope, ges1, ges2))
	{
		if (localized)
		{
			// unlocalize key names in gestures
			ges1 = UnlocalizeKeyGesture(ges1);
			ges2 = UnlocalizeKeyGesture(ges2);
		}
		else
		{
			// localize scope name
			scope = LocalizeScope(scope);
		}

		return CombineKeyBindingString(scope, ges1, ges2);
	}

	_ASSERTE(!"Invalid binding!");

	return binding;
}
#endif

// returns " (binding)" or "binding" if inParens is FALSE
WTString GetBindingTip(LPCSTR cmdStr, LPCSTR vc6Binding /*= NULL*/, BOOL inParens /*= TRUE*/)
{
#if !defined(RAD_STUDIO)
	try
	{
		WTString binding = gDte ? GetDTEBinding(cmdStr) : WTString(vc6Binding);
		if (binding.IsEmpty() && gShellAttr->IsDevenv7() && vc6Binding)
		{
			// [case: 42408] vs2002/3 bindings defined in ctc file only work
			// with the 'default settings' scheme; they are hardcoded
			binding = vc6Binding;
		}
	
		if (binding.GetLength())
		{
			// strip "Global::" from binding
			int colonPos = binding.find_first_of("::");
			if (colonPos != -1)
				binding = binding.substr(colonPos + 2);
	
			if (inParens)
				binding = " (" + binding + ")";
		}
		return binding;
	}
	catch (...)
	{
		_ASSERT(!"GetBindingTip failed with exception!");
	}
#endif
	return WTString(); // no bindings in CppBuilder yet
}

#if !defined(RAD_STUDIO)
bool PromptForBindingsWPF(VaBindingOverrides& bindings, bool autonomous)
{
	bool result = false;

	RunFromMainThread([&]() {
		_variant_t var_autonomous(autonomous);
		_variant_t var_result;

		if (gVaInteropService &&
		    gVaInteropService->InvokeDotNetMethod(
		        kOptKeyBindsDLL,
		        kOptKeyBindsDialogsClass,
		        L"OpenVaOptKeyBindsDlg",
		        &var_autonomous, 1,
		        &var_result))
		{
			if (IDOK == (int)var_result)
			{
				CString valName;
				const CString keyName(ID_RK_APP + kBindingsSubKeyName);

				for (VaBindingOverride& b : bindings)
				{
					UINT idx = b.mRegistryIdx;

					// check if command equals to registry entry with idx (always should)
					CString__FormatA(valName, "%d-Command", idx);
					CStringW cmdName = GetRegValueW(HKEY_CURRENT_USER, keyName, valName);

					_ASSERTE(cmdName == b.mCommandName);

					if (cmdName != b.mCommandName) // should never happen!
						continue;

					// check if binding equals to registry entry with idx (always should)
					CString__FormatA(valName, "%d-Binding", idx);
					CStringW cmdBind = GetRegValueW(HKEY_CURRENT_USER, keyName, valName);

					_ASSERTE(cmdBind == b.mBinding);

					if (cmdBind != b.mBinding) // should never happen!
						continue;

					// read if command is enabled by user
					CString__FormatA(valName, "%d-Enabled", idx);
					b.mUserEnabled = GetRegByte(HKEY_CURRENT_USER, keyName, valName, 0xff);
				}

				result = true;
			}
		}
	});

	return result;
}

void MarkCommandFound(VaBindingOverrides& defaultBindings, const CStringW& cmdName, int idx)
{
	for (VaBindingOverride& b : defaultBindings)
	{
		if (b.mCommandName == cmdName)
		{
			b.mRegistryIdx = (uint)idx;
			break;
		}
	}
}

UINT GetExistingBindingIndexes(std::set<UINT>& id_set)
{
	const CString keyName(ID_RK_APP + kBindingsSubKeyName);
	const CString cmdEnd("-Command");

	UINT count = 0;

	ForEachRegValueName(HKEY_CURRENT_USER, keyName, [&](LPCSTR name) {
		if (wt_isdigit(*name)) // bindings start by digit
		{
			LPCSTR idx_end = StrStrA(name, cmdEnd);
			if (idx_end != nullptr &&          // if we found "-Command" in the name
			    StrCmpA(cmdEnd, idx_end) == 0) // match only "-Command" and not "-CommandScope"
			{
				CStringA str(name, ptr_sub__int(idx_end, name));
				int x = atoi(str);

				_ASSERTE(x != 0);

				if (x != 0)
				{
					id_set.insert((uint)x);
					count++;
				}
			}
		}

		return true;
	});

	return count;
}

void GetBindingsInfo(VaBindingOverrides& defaultBindings, VaBindingOverrides& bindingsInfo)
{
	CString valName;
	const CString keyName(ID_RK_APP + kBindingsSubKeyName);
	bindingsInfo.reserve(defaultBindings.size());

	////////////////////////////////////////////////////
	// Read existing binding definitions from registry

	std::set<UINT> idxSet;
	if (0 < GetExistingBindingIndexes(idxSet))
	{
		for (UINT idx : idxSet)
		{
			VaBindingOverride b;

			CString__FormatA(valName, "%d-Command", idx);
			b.mCommandName = GetRegValueW(HKEY_CURRENT_USER, keyName, valName);

			if (b.mCommandName.IsEmpty())
				continue;

			CString__FormatA(valName, "%d-Binding", idx);
			b.mBinding = GetRegValueW(HKEY_CURRENT_USER, keyName, valName);

			if (b.mBinding.IsEmpty())
				continue;

			CString__FormatA(valName, "%d-EnableByDefault", idx);
			b.mDefault = GetRegBool(HKEY_CURRENT_USER, keyName, valName, false);

			CString__FormatA(valName, "%d-Enabled", idx);
			b.mUserEnabled = GetRegByte(HKEY_CURRENT_USER, keyName, valName, 0xff);

			CString__FormatA(valName, "%d-CommandScope", idx);
			b.mCommandScope = GetRegValue(HKEY_CURRENT_USER, keyName, valName, "");

			MarkCommandFound(defaultBindings, b.mCommandName, (int)idx);

			b.mRegistryIdx = idx;

			bindingsInfo.push_back(b);
		}
	}

	/////////////////////////////////////////////////
	// make sure the defaults have been set up

	UINT nextIdx = 1;
	bool findIdx = !idxSet.empty();
	auto findUnusedIdx = [&]() -> UINT {
		if (!findIdx)
			return nextIdx++;

		if (nextIdx != 0)
		{
			for (UINT x = nextIdx; x < UINT_MAX; x++)
			{
				if (idxSet.find(x) == idxSet.end())
				{
					idxSet.insert(x);
					nextIdx = x + 1;
					return x;
				}
			}

			_ASSERTE(!"GetBindingsInfo.findUnusedIdx failed");
		}

		nextIdx = 0;
		return 0;
	};

	for (VaBindingOverride& b : defaultBindings)
	{
		if (b.mRegistryIdx == UINT_MAX)
		{
			UINT idx = findUnusedIdx();

			if (idx != 0)
			{
				b.mRegistryIdx = idx;

				CString__FormatA(valName, "%d-Command", idx);
				SetRegValue(HKEY_CURRENT_USER, keyName, valName, b.mCommandName);
				CString__FormatA(valName, "%d-Binding", idx);
				SetRegValue(HKEY_CURRENT_USER, keyName, valName, b.mBinding);
				CString__FormatA(valName, "%d-EnableByDefault", idx);
				SetRegValueBool(HKEY_CURRENT_USER, keyName, valName, b.mDefault);
				CString__FormatA(valName, "%d-CommandScope", idx);
				SetRegValue(HKEY_CURRENT_USER, keyName, valName, b.mCommandScope);

				bindingsInfo.push_back(b);
			}
		}
	}
}

void InitDefaultBindings(VaBindingOverrides& defaultBindings)
{
	//
	// also update GetDefaultMapping for mapping to VS commands in VA Recommended Shortcuts dlg
	//
	VaBindingOverride bindings1[] = {{L"VAssistX.OpenFileInSolutionDialog", L"Shift+Alt+O", L"Global;", false},
	                                 {L"VAssistX.NavigateBack", L"Alt+Left Arrow", L"Global;", false},
	                                 {L"VAssistX.NavigateForward", L"Alt+Right Arrow", L"Global;Text Editor;", false},
	                                 {L"VAssistX.FindSelected", L"Alt+K", L"Text Editor;", false}};

	for (auto b : bindings1)
		defaultBindings.push_back(b);

	if (gShellAttr->IsDevenv10OrHigher())
	{
		if (gShellAttr->IsDevenv12OrHigher())
		{
			// default off due to line-up/down conflict
			VaBindingOverride bindings2[] = {{L"VAssistX.ScopeNext", L"Alt+Down Arrow", L"Text Editor;", false},
			                                 {L"VAssistX.ScopePrevious", L"Alt+Up Arrow", L"Text Editor;", false}};

			for (auto b : bindings2)
				defaultBindings.push_back(b);
		}

		if (gShellAttr->IsDevenv15u9OrHigher())
		{
			// [case: 119816]
			// default off due to conflict with:
			// Shift+Alt+[ => Text Editor: EditorContextMenus.Navigate.GoToContainingBlock
			// Shift+Alt+] => Text Editor: Edit.ExpandSelectiontoContainingBlock
			VaBindingOverride bindings5[] = {{L"VAssistX.SmartSelectExtend", L"Shift+Alt+]", L"Text Editor;", false},
			                                 {L"VAssistX.SmartSelectShrink", L"Shift+Alt+[", L"Text Editor;", false}};

			for (auto b : bindings5)
				defaultBindings.push_back(b);

			if (gShellAttr->IsDevenv16OrHigher())
			{
				// [case: 140474] [case: 140897]
				VaBindingOverride bindings16[] = {
				    {L"VAssistX.Paste", L"Shift+Ctrl+V", L"Text Editor;", false},
				    {L"VAssistX.SmartSelectExtendBlock", L"Alt+]", L"Text Editor;", false}};

				for (const auto& b : bindings16)
					defaultBindings.push_back(b);

				if (gShellAttr->IsDevenv17u8OrHigher())
				{
					VaBindingOverride bindings17[] = {
					    {L"VAssistX.OpenCorrespondingFile", L"Alt+O", L"Text Editor;", false}};

					for (const auto& b : bindings17)
						defaultBindings.push_back(b);
				}
			}
		}

		VaBindingOverride bindings4[] = {{L"VAssistX.ResetEditorZoom", L"Ctrl+0", L"Text Editor;", false}};

		for (auto b : bindings4)
			defaultBindings.push_back(b);
	}

	VaBindingOverride bindings3[] = {{L"Window.CloseDocumentWindow", L"Ctrl+W", L"Text Editor;", false}};

	for (auto b : bindings3)
		defaultBindings.push_back(b);

#ifdef DEBUG
	for (const auto& b : defaultBindings)
	{
		for (TokenW t(b.mCommandScope); t.more();)
		{
			auto en_binding = t.read(L";");

			if (en_binding.IsEmpty())
				continue;

			// this asserts if scope is not supported by localizer
			_ASSERTE(GetScopeNameId(en_binding, false) != ScopeIdx::Invalid);
		}
	}
#endif
}

bool HasPromptEverDisplayed()
{
	const CString keyName(ID_RK_APP + kBindingsSubKeyName);
	DWORD dialog_displayed = GetRegDword(HKEY_CURRENT_USER, keyName, kKeyBindingsDialogDisplayed, 0);
	return dialog_displayed != 0;
}

DWORD DaysFromFirstRun(bool initialize)
{
	const CString keyName(ID_RK_APP + kBindingsSubKeyName);

	DOUBLE days_now_dbl = 0;
	SYSTEMTIME systime_now;
	GetSystemTime(&systime_now);

	if (TRUE == SystemTimeToVariantTime(&systime_now, &days_now_dbl))
	{
		DWORD days_now_dw = (DWORD)floor(days_now_dbl);
		DWORD days = GetRegDword(HKEY_CURRENT_USER, keyName, kKeyBindingsDay, 0);

		_ASSERTE(days_now_dw >= days && days_now_dw != 0);

		if (days != 0)
			return days_now_dw - days;

		if (initialize)
			SetRegValue(HKEY_CURRENT_USER, keyName, kKeyBindingsDay, days_now_dw);
	}
	else
	{
		_ASSERTE(!"SystemTimeToVariantTime failed");
	}

	return 0;
}

// [case: 54741] [case: 97169] [case: 97170] optional keybindings
INT_PTR
CheckForKeyBindingUpdate(bool forcePrompt /*= false*/)
{
	if (!gShellAttr->IsDevenv10OrHigher() || !gDte)
		return IDABORT;

	const CString keyName(ID_RK_APP + kBindingsSubKeyName);
	const DWORD lastBindingCheck = GetRegDword(HKEY_CURRENT_USER, keyName, kKeyBindingsCheck, UINT_MAX);
	if (lastBindingCheck < 1846)
		SHDeleteKey(HKEY_CURRENT_USER, keyName);
	else if (!forcePrompt && lastBindingCheck == VA_VER_BUILD_NUMBER)
	{
		// bindings are up-to-date
		return IDABORT;
	}

#if !defined(VA_CPPUNIT)
	if (!forcePrompt && lastBindingCheck == UINT_MAX)
	{
		// this is new user, so let him first try VA,
		// we will ask him for bindings after a week

		const DWORD daysFromFirstRun = DaysFromFirstRun(true);
		if (daysFromFirstRun < 7)
			return IDABORT; // don't do anything yet
	}

	// [case: 118079]
	// record check before display of dialog so that crashes that occur
	// while the dialog is open don't cause the dialog to continue to display
	SetRegValue(HKEY_CURRENT_USER, keyName, kKeyBindingsCheck, VA_VER_BUILD_NUMBER);

	VaBindingOverrides defaultKeyBindCmds;
	InitDefaultBindings(defaultKeyBindCmds);
	VaBindingOverrides bindingsInfo;
	GetBindingsInfo(defaultKeyBindCmds, bindingsInfo);

	int countOfUserSetItems = 0;
	int countOfUninitItems = 0;
	bool needPrompt = forcePrompt || !HasPromptEverDisplayed();
	for (VaBindingOverrides::const_iterator it = bindingsInfo.begin(); !needPrompt && it != bindingsInfo.end(); ++it)
	{
		if ((*it).mUserEnabled == 0xff)
			++countOfUninitItems;
		else
			++countOfUserSetItems;
	}

	if (!needPrompt && countOfUserSetItems && countOfUninitItems)
		needPrompt = true;

	bool doUpdate = true;

	if (needPrompt)
	{
		if (gShellAttr && gShellAttr->IsDevenv14OrHigher() && !gShellAttr->IsDevenv15u8OrHigher() && !forcePrompt)
		{
			// [case: 118079]
			// prevent automatic display at startup due to incompat with async pkg load
			needPrompt = false;

			if (!countOfUserSetItems)
				doUpdate = false;
		}
	}

	if (needPrompt)
	{
		if (gShellAttr && gShellAttr->IsDevenv15u8OrHigher())
		{
			if (!g_vaManagedPackageSvc)
			{
				CComPtr<IVsShell> shell(GetVsShell());
				if (shell)
				{
					// #VaManagedPkgGUID
					GUID GUID_VaManagedPkg = {0x63bbd0d0, 0x98ce, 0x4ad4, {0xb6, 0x6d, 0x45, 0xf1, 0xcd, 0xf7, 0x24, 0x54}};
					CComPtr<IVsPackage> menuPkg;
					HRESULT hr = shell->LoadPackage(GUID_VaManagedPkg, &menuPkg);
					_ASSERT(SUCCEEDED(hr));
				}
			}

			// [case: 118111]
			// show automatic display on 15u80OrHigher but do initialize the JoinableTaskContext
			// on the UI thread before
			if (g_vaManagedPackageSvc)
			{
				g_vaManagedPackageSvc->ForceJtfInit();
				doUpdate = PromptForBindingsWPF(bindingsInfo, !forcePrompt);
				SetRegValue(HKEY_CURRENT_USER, keyName, kKeyBindingsDialogDisplayed, 1);
			}
			else
			{
				doUpdate = false;
			}
		}
		else
		{
			doUpdate = PromptForBindingsWPF(bindingsInfo, !forcePrompt);
			SetRegValue(HKEY_CURRENT_USER, keyName, kKeyBindingsDialogDisplayed, 1);
		}
	}

	DeleteRegValue(HKEY_CURRENT_USER, keyName, kKeyBindingsDay); // no more needed

	if (doUpdate)
	{
		UpdateKeyBindings(bindingsInfo);
		return IDOK;
	}
#endif

	return IDCANCEL;
}

bool ShowVAKeyBindingsDialog()
{
	bool result = false;

	RunFromMainThread([&]() 
	{
		auto func = (ShowOptKeyBindsDlgForcedFn)[]()->INT_PTR
		{
			return CheckForKeyBindingUpdate(true);
		};

		_variant_t var_func((__int64)func);
		_variant_t var_result;

		VsUI::CDpiScope dpi(true);

		if (gVaInteropService &&
		    gVaInteropService->InvokeDotNetMethod(
		        kOptKeyBindsDLL,
		        kOptKeyBindsDialogsClass,
		        L"OpenVaExistingKeyBindsDlg",
		        &var_func, 1,
		        &var_result))
		{
			result = IDOK == (int)var_result;
		}
	});

	return result;
}

DWORD
QueryStatusVaKeyBindingsDialog()
{
#if defined(RAD_STUDIO)
	return DWORD(-1);
#else
	if (!gShellAttr || !gShellAttr->IsDevenv10OrHigher())
		return DWORD(-1);

	return 1;
#endif
}

CStringW GetListOfKeyBindingsResourcesForAST()
{
	CStringW log_str;

	if (g_IdeSettings)
	{
		CComPtr<IVsShell> shell(GetVsShell());

		if (shell)
		{
			LCID lcid = g_IdeSettings->GetLocaleID();
			CString__AppendFormatW(log_str, L"LCID: %lu", lcid);

			log_str.Append(L"\r\n\r\n");
			log_str.Append(L"Keys:");
			for (const auto& kd : rgkdKeys)
			{
				CComBSTR bstrOut;
				if (SUCCEEDED(shell->LoadPackageString(guids[(int)GuidIdx::ShellPkg], kd.rsc_id, &bstrOut)))
				{
					log_str.Append(L"\r\n");
					CString__AppendFormatW(log_str, L"vk: %u, rscId: %u, should be: %s, is: %s", kd.vk, kd.rsc_id,
					                       kd.name, (LPCWSTR)bstrOut);
				}
			}

			log_str.Append(L"\r\n\r\n");
			log_str.Append(L"Scopes:");
			for (const auto& sd : rgkdScopes)
			{
				CComBSTR bstrOut;
				if (SUCCEEDED(shell->LoadPackageString(guids[(int)sd.package], sd.rsc_id, &bstrOut)))
				{
					log_str.Append(L"\r\n");
					CString__AppendFormatW(log_str, L"rscId: %u, should be: %s, is: %s", sd.rsc_id, sd.name,
					                       (LPCWSTR)bstrOut);
				}
			}
		}
	}

	return log_str;
}

void UpdateKeyBindings(const VaBindingOverrides& bindingVals)
{
	HRESULT res = S_OK;
	CComPtr<EnvDTE::Commands> pCmds;
	_ASSERTE(gDte);
	gDte->get_Commands(&pCmds);
	if (!pCmds)
		return;

	for (VaBindingOverrides::const_iterator it = bindingVals.begin(); it != bindingVals.end(); ++it)
	{
		const VaBindingOverride& p = *it;
		CComPtr<EnvDTE::Command> pCmd;
		pCmds->Item(CComVariant(p.mCommandName), 1, &pCmd);
		if (!pCmd)
			continue;

		if (1 != p.mUserEnabled)
		{
			if (-1 == p.mCommandName.Find(L"VAssistX."))
				continue;
		}

		std::vector<CComVariant> bindings;
		std::vector<CStringW> comparable_bindings;

		// read in current bindings and push them into the array
		if (p.mUserEnabled == 1)
		{
			CStringW en_binding; // never localized (those are our bindings)
			CComVariant bindVar;
			CComVariant oldBindings;
			res = pCmd->get_Bindings(&oldBindings);
			if (oldBindings.parray)
			{
				for (long idx = 0; SafeArrayGetElement(oldBindings.parray, &idx, &bindVar) == S_OK; ++idx)
				{
					CStringW loc_binding(bindVar); // this one is localized (on localized IDEs)

					// To avoid asserts, don't include bindings we don't use for our commands.
					// Currently we check only for Global and Text Editor scopes.
					if (KeyBindingScopeIsSupported(loc_binding, true))
						comparable_bindings.push_back(KeyBindingMakeComparable(loc_binding, true));

					bindings.push_back(CComVariant(KeyBindingMakeAssignable(loc_binding, true)));
				}
			}

			// append user selected bindings to the array
			bool have_updates = false;
			for (TokenW t(p.mCommandScope); t.more();)
			{
				en_binding = t.read(L";");
				if (en_binding.IsEmpty())
					continue;

				en_binding += L"::";
				en_binding += p.mBinding;
				bindVar = en_binding;

				CStringW comparable = KeyBindingMakeComparable(en_binding, false);

				// try to find out if already there
				bool found = false;
				for (const auto& cmp : comparable_bindings)
				{
					if (!comparable.CompareNoCase(cmp))
					{
						found = true;
						break;
					}
				}

				if (!found)
				{
					// binding does not exist, so append it
					comparable_bindings.push_back(comparable);
					bindings.push_back(CComVariant(KeyBindingMakeAssignable(en_binding, false)));
					have_updates = true;
				}
			}

			if (have_updates)
			{
				// convert our array into a COM array
				SAFEARRAYBOUND rgsabound[1];
				rgsabound[0].lLbound = 0;
				rgsabound[0].cElements = (uint)bindings.size();
				SAFEARRAY* psa = SafeArrayCreate(VT_VARIANT, 1, rgsabound);
				if (psa == NULL)
					continue;

				long idx = 0;
				for (std::vector<CComVariant>::iterator it2 = bindings.begin(); it2 != bindings.end(); ++it2, ++idx)
					res = SafeArrayPutElement(psa, &idx, &(*it2));

				// push the bindings
				CComVariant arrayVar(psa);
				res = pCmd->put_Bindings(arrayVar);

				SafeArrayDestroy(psa);
			}
		}
	}
}
#endif
