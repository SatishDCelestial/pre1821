#include "stdafxed.h"
#if defined(RAD_STUDIO) || defined(VA_CPPUNIT)

#include "RadStudioMenu.h"
#include "CppBuilder.h"
#include "VaPkg/VaPkgUI/PkgCmdID.h"
#include "IVaService.h"
#include "VaService.h"
#include "EDCNT.H"
#include "RadStudioFrame.h"
#include "RadStudioPlugin.h"
#include "VARefactor.h"
#include "VACompletionBox.h"
#include "FindReferences.h"
#include "FILE.H"
#include "Directories.h"
#include "rbuffer.h"
#include "DevShellService.h"


std::vector<std::unique_ptr<IVaRSMenu>> sRSMenus;
std::unique_ptr<IVaRSMenu> sRSFeatureMenus[(size_t)FeatureMenuId::Count];

int VaRSMenuManager::NextMenuId()
{
	return (int)sRSMenus.size() + 1;
}

int VaRSMenuManager::GetMenuId(const IVaRSMenu* pMenu)
{
	int index = 0;

	for (const auto& mnu : sRSMenus)
	{
		index++;
		if (mnu.get() == pMenu)
		{
			return index;
		}
	}

	return 0;
}

IVaRSMenu* VaRSMenuManager::FindMenu(std::function<bool(const IVaRSMenu*)> predicate)
{
	for (const auto& mnu : sRSMenus)
	{
		if (predicate(mnu.get()))
		{
			return mnu.get();
		}
	}
	return nullptr;
}

IVaRSMenu* VaRSMenuManager::FindMenu(int id)
{
	auto index = id - 1;

	if (index >= 0 && index < (int)sRSMenus.size())
	{
		return sRSMenus[(size_t)index].get();
	}

	return nullptr;
}

int VaRSMenuManager::AddMainMenu(IVaRSMenu* newMenu, const IVaRSMenu* insertAfter /*= nullptr*/, const IVaRSMenu* parent /*= nullptr*/)
{
	if (gRadStudioHost && newMenu)
	{
		int id = NextMenuId();
		int afterId = 0;
		int parentId = 0;

		if (insertAfter)
		{
			afterId = insertAfter->GetId();
		}

		if (parent)
		{
			parentId = parent->GetId();
		}

		if (gRadStudioHost->AddMainMenuItem(newMenu->caption.c_str(), id, afterId, parentId))
		{
			sRSMenus.emplace_back(newMenu);
			return id;
		}
	}

	return 0;
}

int VaRSMenuManager::AddLocalMenu(IVaRSMenu* newMenu, const IVaRSMenu* insertAfter /*= nullptr*/, const IVaRSMenu* parent /*= nullptr*/)
{
	if (gRadStudioHost && newMenu)
	{
		int id = NextMenuId();
		int afterId = 0;
		int parentId = 0;

		if (insertAfter)
		{
			afterId = insertAfter->GetId();
		}

		if (parent)
		{
			parentId = parent->GetId();
		}

		if (gRadStudioHost->AddLocalMenuItem(newMenu->caption.c_str(), id, afterId, parentId))
		{
			sRSMenus.emplace_back(newMenu);
			return id;
		}
	}

	return 0;
}

BOOL VaRSMenuManager::AddFeatureMenu(FeatureMenuId menuId, IVaRSMenu* newMenu)
{
	if (gRadStudioHost && newMenu)
	{
		if (gRadStudioHost->AddFeatureMenu(menuId))
		{
			sRSFeatureMenus[(size_t)menuId] = std::unique_ptr<IVaRSMenu>(newMenu);
			return TRUE;
		}
	}

	return FALSE;
}

IVaRSMenu* VaRSMenuManager::GetFeatureMenu(FeatureMenuId menuId)
{
	return sRSFeatureMenus[(size_t)menuId].get();
}

void VaRSMenuManager::Cleanup()
{
	sRSMenus.clear();
}

int IVaRSMenu::GetId() const
{
	return VaRSMenuManager::GetMenuId(this);
}

struct VaRSCommandMenu : IVaRSMenu
{
	bool m_enabled = false;                  // set by UpdateState
	IVaService::CommandTargetType m_cmdType; // VA command type
	DWORD m_cmd;                             // command to execute
	std::function<void()> m_pre_execute;     // open related dialog and so on
	CStringW m_menuAdd;

	VaRSCommandMenu(const wchar_t* caption, IVaService::CommandTargetType cmdType, DWORD cmdId, std::function<void()> pre_execute = nullptr)
	    : IVaRSMenu(caption), m_cmdType(cmdType), m_cmd(cmdId), m_pre_execute(pre_execute)
	{
	}

	static VaRSCommandMenu* EditorCmd(const wchar_t* caption, DWORD cmdId, std::function<void()> pre_execute = nullptr)
	{
		return new VaRSCommandMenu(caption, IVaService::ct_editor, cmdId, pre_execute);
	}

	static VaRSCommandMenu* RefactorCmd(const wchar_t* caption, DWORD cmdId, std::function<void()> pre_execute = nullptr)
	{
		return new VaRSCommandMenu(caption, IVaService::ct_refactor, cmdId, pre_execute);
	}

	static VaRSCommandMenu* GlobalCmd(const wchar_t* caption, DWORD cmdId, std::function<void()> pre_execute = nullptr)
	{
		return new VaRSCommandMenu(caption, IVaService::ct_global, cmdId, pre_execute);
	}

	void Execute() override
	{
		if (m_enabled && gVaService)
		{
			if (m_pre_execute)
				m_pre_execute();

			gVaService->Exec(m_cmdType, m_cmd);
		}
	}

	void UpdateState(bool& visible, bool& enabled, bool& checked) override
	{
		visible = enabled = checked = false;

		auto ed = g_currentEdCnt;
		if (ed)
		{
			ed->Scope(true);
			ed->CurScopeWord();
		}

		if (gVaService)
		{
			auto status = gVaService->QueryStatus(m_cmdType, m_cmd);
			visible = status != (DWORD)-1;
			m_enabled = enabled = visible && status > 0;
		}
	}

	LPCWSTR UpdateCaption(bool& additive) override
	{
		if (m_cmd == icmdVaCmd_RefactorAddInclude)
		{
			VARefactorCls rfctr;
			CStringW incFile;
			int atLine;
			BOOL isSys = FALSE;
			if (rfctr.GetAddIncludeInfo(atLine, incFile, &isSys))
			{
				m_menuAdd = ::Basename(incFile);

				// [case: 163967]
				if (isSys)
				{
					m_menuAdd.Insert(0, L'<');
					m_menuAdd.AppendChar(L'>');
				}
				else
				{
					m_menuAdd.Insert(0, L'"');
					m_menuAdd.AppendChar(L'"');
				}

				additive = true;
				return m_menuAdd;
			}
		}

		return nullptr;
	}
};

struct VaRSLambdaMenu : IVaRSMenu
{
	std::function<void()> m_exec;
	std::function<bool()> m_enabled;
	std::function<void()> m_destroy;

	VaRSLambdaMenu(
	    const wchar_t* caption,
	    std::function<void()> execute,
	    std::function<bool()> enabled = nullptr,
	    std::function<bool()> destroy = nullptr)
	    : IVaRSMenu(caption), m_exec(execute), m_enabled(enabled), m_destroy(destroy)
	{
	}

	virtual ~VaRSLambdaMenu()
	{
		if (m_destroy)
		{
			m_destroy();
		}
	}

	void Execute() override
	{
		if (m_exec)
		{
			m_exec();
		}
	}

	void UpdateState(bool& visible, bool& enabled, bool& checked) override
	{
		visible = !!m_exec;
		checked = false;
		enabled = m_enabled ? m_enabled() : visible;
	}

	LPCWSTR UpdateCaption(bool& additive) override
	{
		return nullptr;
	}
};

class VaRSTraceFrameMenu : public IVaRSMenu
{
	VaRSFrameCWnd* frame = nullptr;

  public:
	VaRSTraceFrameMenu(const wchar_t* caption)
	    : IVaRSMenu(caption)
	{
	}

	virtual ~VaRSTraceFrameMenu()
	{
	}

	void Execute() override
	{
		delete frame;
		frame = VaRSFrameFactory::CreateTraceFrame();
	}

	void UpdateState(bool& visible, bool& enabled, bool& checked) override
	{
		visible = enabled = true;
		checked = false;
	}

	LPCWSTR UpdateCaption(bool& additive) override
	{
		return nullptr;
	}
};

class VaRSOutlineMenu : public IVaRSMenu
{
	VaRSFrameCWnd* frame = nullptr;

  public:
	VaRSOutlineMenu(const wchar_t* caption)
	    : IVaRSMenu(caption)
	{
	}

	virtual ~VaRSOutlineMenu()
	{
	}

	void Execute() override
	{
		delete frame;
		frame = VaRSFrameFactory::CreateFileOutlineFrame();
	}

	void UpdateState(bool& visible, bool& enabled, bool& checked) override
	{
		visible = enabled = true;
		checked = false;
	}

	LPCWSTR UpdateCaption(bool& additive) override
	{
		return nullptr;
	}
};

class VaRSFindReferencesMenu : public IVaRSMenu
{
	bool mLocal;

  public:
	VaRSFindReferencesMenu(const wchar_t* caption, bool local)
	    : IVaRSMenu(caption), mLocal(local)
	{
	}

	DTypePtr GetSymbol()
	{
		if (!gVaService)
			return nullptr;

		wchar_t buff[MAX_PATH * 8];
		memset(buff, 0, sizeof(buff));
		int line = 0, column = 0;
		HWND hWnd = nullptr;

		VADEBUGPRINT("#RAD GetFocusedEditorCursorPos");
		if (gRadStudioHost->GetFocusedEditorCursorPos(buff, sizeof(buff), &line, &column, &hWnd))
		{
			LC_RAD_2_VA(&line, &column);
			EdCntPtr ed = GetOpenEditWnd(hWnd);
			if (ed != nullptr)
			{
				VARefactorCls rfctr;

				WTString invokingScope;

				VADEBUGPRINT("#RAD GetRefactorSym");

				DTypePtr tmpType;
				if (GetRefactorSym(ed, tmpType, &invokingScope, false) && !!tmpType)
				{
					return tmpType;
				}
			}
		}

		return nullptr;
	}

	void Execute() override
	{
		auto mSymbol = GetSymbol();
		if (mSymbol && gVaService)
		{
			auto imgIdx = GetTypeImgIdx(mSymbol->MaskedType(), mSymbol->Attributes());
			auto symScope = mSymbol->SymScope();
			mSymbol = nullptr;

			VADEBUGPRINT("#RAD ImgIdx = " << imgIdx << " SymScope = " << symScope.c_str());

			VADEBUGPRINT("#RAD Starting Find References");

			int flags = FREF_Flg_Reference | FREF_Flg_Reference_Include_Comments | FREF_FLG_FindAutoVars;

			if (mLocal)
			{
				flags |= FREF_Flg_InFileOnly | FREF_Flg_CorrespondingFile;
			}

			gVaService->FindReferences(flags, imgIdx, symScope);
		}
	}

	void UpdateState(bool& visible, bool& enabled, bool& checked) override
	{
		visible = true;
		checked = false;
		enabled = !!GetSymbol();
	}

	LPCWSTR UpdateCaption(bool& additive) override
	{
		return nullptr;
	}
};

void VaRSMenuManager::InitMenus()
{
	AddFeatureMenu(FeatureMenuId::vaFindReferences, new VaRSFindReferencesMenu(L"Fin&d References", false));
	AddFeatureMenu(FeatureMenuId::vaFindReferencesInUnit, new VaRSFindReferencesMenu(L"Find Reference&s Local", true));
	AddFeatureMenu(FeatureMenuId::vaRename, VaRSCommandMenu::RefactorCmd(L"Rename", icmdVaCmd_RefactorRename));
	AddFeatureMenu(FeatureMenuId::vaFindSymbol, new VaRSLambdaMenu(
	                                                L"Fin&d Symbol",
	                                                [] { gVaRadStudioPlugin->FindSymbolInProjectGroup(); },
	                                                [] {
		                                                bool enabled = false;
		                                                bool visible = false;
		                                                gVaRadStudioPlugin->FindSymbolInProjectGroupUpdate(&enabled, &visible);
		                                                return enabled && visible;
	                                                }));

 	AddFeatureMenu(FeatureMenuId::vaGoToRelated, VaRSCommandMenu::EditorCmd(L"GoTo Related", icmdVaCmd_SuperGoto));
	AddFeatureMenu(FeatureMenuId::vaGoToMember, VaRSCommandMenu::EditorCmd(L"GoTo Member", icmdVaCmd_GotoMember));
 	AddFeatureMenu(FeatureMenuId::vaAddInclude, VaRSCommandMenu::RefactorCmd(L"Add Include", icmdVaCmd_RefactorAddInclude));
 	AddFeatureMenu(FeatureMenuId::vaCreateDeclaration, VaRSCommandMenu::RefactorCmd(L"Create Declaration", icmdVaCmd_RefactorCreateDeclaration));
 	AddFeatureMenu(FeatureMenuId::vaCreateImplementation, VaRSCommandMenu::RefactorCmd(L"Create Implementation", icmdVaCmd_RefactorCreateImplementation));

	// added: 6th October 2023
	AddFeatureMenu(FeatureMenuId::vaCreateAllImplementations, VaRSCommandMenu::RefactorCmd(L"Create All Implementations", icmdVaCmd_RefactorImplementInterface));
	AddFeatureMenu(FeatureMenuId::vaGotoDeclaration, VaRSCommandMenu::EditorCmd(L"Goto De&claration/Definition", icmdVaCmd_GotoDeclOrImpl));
	AddFeatureMenu(FeatureMenuId::vaGotoImplementation, VaRSCommandMenu::EditorCmd(L"Goto &Implementation", icmdVaCmd_GotoImplementation));
	// AddFeatureMenu(FeatureMenuId::vaCreateFromUsage, VaRSCommandMenu::RefactorCmd(L"Create From Usage", icmdVaCmd_RefactorCreateFromUsage));

	AddFeatureMenu(FeatureMenuId::vaRebuildDB,
	            new VaRSLambdaMenu(
	                L"Rebuild Database",
	                [] {
		                VaDirs::FlagForDbDirPurge();
		                VaDirs::CleanDbTmpDirs();
		                g_ExpHistory->Clear();
		                g_rbuffer.Clear();
		                WtMessageBox(_T("You must restart your IDE before your changes take effect."), _T("Restart Required"),
		                            MB_OK | MB_ICONEXCLAMATION);
	                },
	                [] { return !VaDirs::IsFlaggedForDbDirPurge(); }));

// 	AddMainMenu(new VaRSLambdaMenu(L"Fin&d Symbol", 
// 		[] { gVaRadStudioPlugin->FindSymbolInProjectGroup(); }, 
// 		[] {
// 		    bool enabled = false;
// 		    bool visible = false;
// 		    gVaRadStudioPlugin->FindSymbolInProjectGroupUpdate(&enabled, &visible);
// 		    return enabled && visible;
// 		}
// 		));
// 
// 	AddMainMenu(new VaRSFindReferencesMenu(L"Fin&d References", false));
// 	AddMainMenu(new VaRSFindReferencesMenu(L"Find Reference&s Local", true));
// 	AddMainMenu(new VaRSOutlineMenu(L"VA &Outline"));
// 	AddMainMenu(new VaRSTraceFrameMenu(L"VA &Trace"));
// 	AddMainMenu(VaRSCommandMenu::EditorCmd(L"Goto &Implementation", icmdVaCmd_GotoImplementation));
// 	AddMainMenu(VaRSCommandMenu::EditorCmd(L"Goto De&claration/Definition", icmdVaCmd_GotoDeclOrImpl));
// 	AddMainMenu(VaRSCommandMenu::RefactorCmd(L"Rename", icmdVaCmd_RefactorRename));
// 	AddMainMenu(VaRSCommandMenu::RefactorCmd(L"Change Signature", icmdVaCmd_RefactorChangeSignature));
// 	AddMainMenu(VaRSCommandMenu::GlobalCmd(L"Open File In Project Group", icmdVaCmd_OpenFileInWorkspaceDlg));
// 
// 	AddLocalMenu(VaRSCommandMenu::EditorCmd(L"Smart Select Shrink", icmdVaCmd_SmartSelectShrink));
// 	AddLocalMenu(VaRSCommandMenu::EditorCmd(L"Smart Select Extend", icmdVaCmd_SmartSelectExtend));

	// 	VaRSMenuManager::AddMainMenu(
	// 	    new VaRSLambdaMenu(L"*TEST* Line/Column",
	// 	                       []() {
	// 		                       int line, charIndex;
	// 		                       HWND hWnd = 0;
	// 		                       WCHAR buff[MAX_PATH * 4];
	// 		                       if (gRadStudioHost->GetFocusedEditorCursorPos(buff, sizeof(buff), &line, &charIndex, &hWnd))
	// 		                       {
	// 			                       int sl, si, el, ei;
	// 			                       gRadStudioHost->GetActiveViewSelectionRange(buff, &sl, &si, &el, &ei);
	// 			                       int size = gRadStudioHost->TextRangeSize(buff, sl, si, el, ei);
	// 			                       if (size)
	// 			                       {
	// 				                       std::vector<char> buffer;
	// 				                       buffer.resize((size_t)size + 1, '\0');
	// 				                       size = gRadStudioHost->GetActiveViewText(buff, &buffer[0], size, sl, si, el, ei);
	// // 				                       if (size)
	// // 				                       {
	// // 					                       auto lineStr = WTString(&buffer.front()).Wide();
	// // 					                       EncodeEscapeChars(lineStr);
	// //
	// // 					                       CStringW wstr;
	// // 					                       wstr.Format(L" caret: %d : %d\n start: %d : %d\n end: %d : %d\nText: '%s'", line, charIndex, sl, si, el, ei, (LPCWSTR)lineStr);
	// // 					                       ::MessageBoxW(hWnd, (LPCWSTR)wstr, L"Test Caret Line", MB_OK);
	// // 				                       }
	// 								   }
	// 		                       }
	// 	                       }));

#ifdef _DEBUG
	AddMainMenu(
	    new VaRSLambdaMenu(L"*TEST* Line/Column",
	                       []() {
		                       auto ed = g_currentEdCnt;
		                       int sl, si, el, ei;
		                       gRadStudioHost->ExGetSelectionRangeForVA(ed.get(), &sl, &si, &el, &ei);
		                       auto selString = ed->GetSubString(TERRCTOLONG((ulong)sl, (ulong)si), TERRCTOLONG((ulong)el, (ulong)ei)).Wide();
		                       CStringW wstr;
		                       wstr.Format(L" start: %d : %d\n end: %d : %d\nText: '%s'", sl, si, el, ei, (LPCWSTR)selString);
		                       ::MessageBoxW(ed->m_hWnd, (LPCWSTR)wstr, L"Test Caret Line", MB_OK);
	                       }));
#endif

	// 	VaRSMenuManager::AddMainMenu(
	// 	    new VaRSLambdaMenu(L"*TEST* Show Caret Info",
	// 	                       []() {
	// 		                       int line, charIndex;
	// 		                       HWND hWnd = 0;
	// 		                       if (gRadStudioHost->GetFocusedEditorCursorPos(nullptr, 0, &line, &charIndex, &hWnd))
	// 		                       {
	// 			                       CStringW wstr;
	// 			                       wstr.Format(L"Line: %d Char Index: %d", line, charIndex);
	// 			                       ::MessageBoxW(hWnd, (LPCWSTR)wstr, L"Test Line Column", MB_OK);
	// 		                       }
	// 	                       }));
	//
	// 		VaRSMenuManager::AddMainMenu(
	// 	    new VaRSLambdaMenu(L"*TEST* Reset Caret Position",
	// 	                       []() {
	// 		                       int line, charIndex;
	// 		                       HWND hWnd = 0;
	// 		                       WCHAR buffer[MAX_PATH * 4];
	// 		                       if (gRadStudioHost->GetFocusedEditorCursorPos(buffer, sizeof(buffer), &line, &charIndex, &hWnd))
	// 		                       {
	// 			                       gRadStudioHost->LoadFileAtLocation(buffer, line, charIndex, line, charIndex);
	// 		                       }
	// 	                       }));
	//
	// 		VaRSMenuManager::AddMainMenu(
	// 	        new VaRSLambdaMenu(L"*TEST* Try Create Local Menu",
	// 	                           []() {
	// 		                           gRadStudioHost->AddLocalMenuItem(L"Local Menu Test", 123, 0, 0);
	// 	                           }));
}

#endif