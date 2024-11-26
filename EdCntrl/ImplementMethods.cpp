#include "StdAfxEd.h"
#include "ImplementMethods.h"
#include "GenericTreeDlg.h"
#include "PROJECT.H"
#include "FileTypes.h"
#include "VACompletionBox.h"
#include "PARSE.H"
#include "FDictionary.h"
#include "UndoContext.h"
#include "VARefactor.h"
#include "FILE.H"
#include "AutotextManager.h"
#include "FreezeDisplay.h"
#include "RegKeys.h"
#include "VAAutomation.h"
#include "VAParse.h"
#include "ProjectInfo.h"
#include "StatusWnd.h"
#include "Settings.h"
#include "VASmartSelect_Utils.h"
#include "CreateFromUsage.h"
#include "UnrealPostfixType.h"
#include "SemiColonDelimitedString.h"
#include "VAWatermarks.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define ADD_HIDE_IMPLEMENTED_METHODS 0

struct MdExportTypeReplacements
{
	WTString mMdType;
	WTString mReplacementText;
};

// convert from the naming .net naming convention used in vanetobj to winrt:
// see VANetObjMD.cpp and
// http://msdn.microsoft.com/en-us/library/0wf2yk2k.aspx
// http://msdn.microsoft.com/en-us/library/ya5y69ds.aspx
// http://msdn.microsoft.com/en-us/library/windows/apps/hh755822.aspx
// http://msdn.microsoft.com/en-us/library/windows/apps/hh700121.aspx
// Also convert WinRT System projections:
// http://msdn.microsoft.com/en-us/library/windows/apps/br211377
// http://msdn.microsoft.com/en-us/library/windows/apps/hh995050.aspx
// http://msdn.microsoft.com/en-us/library/windows/apps/br230301.aspx
const MdExportTypeReplacements kNetToWinRtTypeReplacements[] = {
    // vanetobj conventions
    {"Void", "void"},
    {"Char", "char16"},
    {"SByte", "int8"},
    {"Byte", "uint8"},
    {"Int16", "int16"},
    {"UInt16", "uint16"},
    {"Int32", "int32"},
    {"UInt32", "uint32"},
    {"Int64", "int64"},
    {"UInt64", "uint64"},
    {"Single", "float32"},
    {"Double", "float64"},
    {"System::Guid", "Platform::Guid"},
    {"System::Object", "Platform::Object"}, // or should this be IInspectable ?
    {"System::String", "Platform::String"},

    // http://msdn.microsoft.com/en-us/library/windows/apps/hh995050.aspx
    // since vanetobj uses System projections
    {"System::AttributeUsageAttribute", "Windows::Foundation::Metadata::AttributeUsageAttribute"},
    {"System::AttributeTargets", "Windows::Foundation::Metadata::AttributeTargets"},
    {"System::DateTimeOffset", "Windows::Foundation::DateTime"},
    {"System::EventHandler", "Windows::Foundation::EventHandler"},
    {"System::Runtime::InteropServices::WindowsRuntime::EventRegistrationToken",
     "Windows::Foundation::EventRegistrationToken"},
    {"System::Exception", "Windows::Foundation::HResult"},
    {"System::Nullable", "Windows::Foundation::IReference"},
    {"System::TimeSpan", "Windows::Foundation::TimeSpan"},
    {"System::Uri", "Windows::Foundation::Uri"},
    {"System::IDisposable", "Windows::Foundation::IClosable"},
    {"System::Collections::Generic::IEnumerable", "Windows::Foundation::Collections::IIterable"},
    {"System::Collections::Generic::IList", "Windows::Foundation::Collections::IVector"},
    {"System::Collections::Generic::IReadOnlyList", "Windows::Foundation::Collections::IVectorView"},
    {"System::Collections::Generic::IDictionary", "Windows::Foundation::Collections::IMap"},
    {"System::Collections::Generic::IReadOnlyDictionary", "Windows::Foundation::Collections::IMapView"},
    {"System::Collections::Generic::KeyValuePair", "Windows::Foundation::Collections::IKeyValuePair"},
    {"System::Collections::IEnumerable", "Windows::UI::Xaml::Interop::IBindableIterable"},
    {"System::Collections::IList", "Windows::UI::Xaml::Interop::IBindableVector"},
    {"System::Collections::Specialized::INotifyCollectionChanged",
     "Windows::UI::Xaml::Interop::INotifyCollectionChanged"},
    {"System::Collections::Specialized::NotifyCollectionChangedEventHandler",
     "Windows::UI::Xaml::Interop::NotifyCollectionChangedEventHandler"},
    {"System::Collections::Specialized::NotifyCollectionChangedEventArgs",
     "Windows::UI::Xaml::Interop::NotifyCollectionChangedEventArgs"},
    {"System::Collections::Specialized::NotifyCollectionChangedAction",
     "Windows::UI::Xaml::Interop::NotifyCollectionChangedAction"},
    {"System::ComponentModel::INotifyPropertyChanged", "Windows::UI::Xaml::Data::INotifyPropertyChanged"},
    {"System::ComponentModel::PropertyChangedEventHandler", "Windows::UI::Xaml::Data::PropertyChangedEventHandler"},
    {"System::ComponentModel::PropertyChangedEventArgs", "Windows::UI::Xaml::Data::PropertyChangedEventArgs"},
    {"System::Type", "Windows::UI::Xaml::Interop::TypeName"},

    {"", ""}};

// http://msdn.microsoft.com/en-us/library/0wf2yk2k.aspx
// http://msdn.microsoft.com/en-us/library/ya5y69ds.aspx
const MdExportTypeReplacements kMdExportToManagedSystemAliasReplacements[] = {
    // vanetobj conventions
    {"System.Boolean", "bool"},
    {"Boolean", "bool"},
    {"System.Byte", "byte"},
    {"Byte", "byte"},
    {"System.SByte", "sbyte"},
    {"SByte", "sbyte"},
    {"System.Char", "char"},
    {"Char", "char"},
    {"System.Decimal", "decimal"},
    {"Decimal", "decimal"},
    {"System.Double", "double"},
    {"Double", "double"},
    {"System.Single", "float"},
    {"Single", "float"},
    {"System.Int32", "int"},
    {"Int32", "int"},
    {"System.UInt32", "uint"},
    {"UInt32", "uint"},
    {"System.Int64", "long"},
    {"Int64", "long"},
    {"System.UInt64", "ulong"},
    {"UInt64", "ulong"},
    {"System.Object", "object"},
    {"System.Int16", "short"},
    {"Int16", "short"},
    {"System.UInt16", "ushort"},
    {"UInt16", "ushort"},
    {"System.String", "string"},
    {"System.Type", "Type"},
    {"System.Void", "void"},
    {"Void", "void"},
    {"System.Guid", "Guid"},
    {"System.DateTime", "DateTime"},
    {"System.TimeSpan", "TimeSpan"},
    {"System.Uri", "Uri"},
    {"System.IDisposable", "IDisposable"},
    {"System.Nullable", "Nullable"},
    {"System.Exception", "Exception"},
    {"", ""}};

enum ImplMethodsStates
{
	Implemented = GenericTreeNodeItem::State_Owner_0,
	ImplementedInBase = GenericTreeNodeItem::State_Owner_1,
	RootNode = GenericTreeNodeItem::State_Owner_2,
	CheckedHidden = GenericTreeNodeItem::State_Owner_3,
	FilterMatch = GenericTreeNodeItem::State_Owner_4,
	Visible = GenericTreeNodeItem::State_Owner_5
};

struct IVMO // Implement Virtual Methods Options
{
	enum
	{
		None = 0x00,
		HideImplemented = 0x01,
		SortAlphabetically = 0x02,
		FilterByNameOnly = 0x04
	};

	static bool Get(DWORD bit)
	{
		return (Psettings->mImplementVirtualMethodsOptions & bit) == bit;
	}

	static void Set(DWORD bit, bool val)
	{
		if (val)
			Psettings->mImplementVirtualMethodsOptions |= bit;
		else
			Psettings->mImplementVirtualMethodsOptions &= ~bit;
	}
};

ImplementMethods::ImplementMethods(bool displayPrompt /*= false*/)
    : mThisType(new DType()), mDisplayPrompt(displayPrompt)
{
}

DTypeList GetBaseClasses(EdCntPtr ed, DType* sym, bool includeIfaces, bool ignoreThis, bool forceBclUpdate)
{
	DTypeList typeList;

	WTString symSymScope(sym->SymScope());
	MultiParsePtr mp(ed->GetParseDb());
	WTString bcl(mp->GetBaseClassList(symSymScope, forceBclUpdate));
	if (ignoreThis)
		bcl.ReplaceAll((symSymScope + WTString("\f")).c_str(), "");

	// http://stackoverflow.com/questions/3236305/do-interfaces-derive-from-system-object-c-sharp-spec-says-yes-eric-says-no-re
	if (sym->MaskedType() == C_INTERFACE)
		bcl.ReplaceAll(":System:Object\f", "");

	bool usedDbNetType = false;
	bool usedDbCppType = false;
	WTString lst = "";
	token t = bcl;
	while (t.more())
	{
		const WTString bc = t.read("\f");
		if (bc.IsEmpty())
			continue;

		DType* data = mp->FindExact(bc.c_str());
		if (!data)
			continue;

		if (data->IsDbNet())
		{
			// [case: 52138] this block is a workaround for
			// System.Windows.Forms.Control bcl.  It implements many COM
			// interfaces according to one of our metadataImports of it.
			// A second import does not list the COM interfaces.
			// The underlying issue may need to be addressed in VaNetObj.
			// For now, prevent the COM interfaces from appearing in the list.
			if (usedDbCppType)
				continue;

			usedDbNetType = true;
		}
		else if (data->IsDbCpp())
		{
			if (usedDbNetType)
				continue;

			usedDbCppType = true;
		}

		const uint type = data->MaskedType();

		switch (type)
		{
		case CLASS:
		case STRUCT:
			break;
		case C_INTERFACE:
			if (!includeIfaces)
				continue;
			break;
		default:
			continue;
		}

		if (lst.contains((bc + '\f').c_str()))
			continue;

		lst += bc + '\f';
		typeList.push_back(data);
	}

	return typeList;
}

BOOL GetCurrentMacro(EdCntPtr ed, DType* sym, WTString& macroCode, long* sPos = nullptr, long* ePos = nullptr)
{
	using namespace VASmartSelect::Utils;

	WTString buf = ed->GetBuf();

	long sp, ep = sp = ed->GetBufIndex(buf, (long)ed->CurPos());

	WTString macro;
	if (!ReadSymbol(buf, sp, IsCFile(ed->m_ftype), macro, sp, ep))
		return FALSE;

	if (sym->Sym() != macro)
		return FALSE;

	WTString args;
	StateIterator sit(ep, IsCFile(ed->m_ftype), buf);

	while (sit.move_next())
	{
		if (!sit.is_in_code())
			continue;

		if (wt_isspace(sit.get_char()))
			continue;

		break;
	}

	auto ch = sit.get_char();
	if (ch == '(')
	{
		int parenDepth = 0;
		do
		{
			ch = sit.get_char();

			if (!sit.is_in_comment() && !sit.is_continuation())
				args += (TCHAR)ch;

			if (sit.is_in_code())
			{
				if (ch == '(')
				{
					parenDepth++;
				}
				else if (ch == ')')
				{
					--parenDepth;
					if (!parenDepth)
					{
						ep = sit.pos() + 1;
						break;
					}
				}
			}
		} while (sit.move_next());
	}

	if (sPos)
		*sPos = sp;

	if (ePos)
		*ePos = ep;

	macroCode = macro + args;
	return TRUE;
}

// tmp is normally only a type representing a declaration
// of the template, however for proper working we need
// an instantiation of the template, so try to get it.
BOOL GetFullyQualifiedTemplate(EdCntPtr ed, DTypePtr& dType)
{
	using namespace VASmartSelect::Utils;

	_ASSERTE(dType && dType->IsTemplate());

	WTString buff = ed->GetBuf(TRUE);
	long ep = ed->GetBufIndex(buff, (long)ed->CurPos());

	StateIterator sit(ep, IsCFile(ed->m_ftype), buff);

	while (sit.move_next())
	{
		if (!sit.is_in_code())
			continue;

		ep = sit.pos();
		auto ch = sit.get_char();

		if (ch == ';' || ch == '{' || ch == '[' || ch == '(')
		{
			ep = -1;
			break;
		}

		if (ch == '<')
			break;
	}

	if (ep >= 0 && buff[(uint)ep] == '<')
	{
		IsTemplateCls it(ed->m_ftype);
		if (it.IsTemplate(buff.c_str() + ep + 1, buff.GetLength() - ep - 1, 0))
		{
			WTString tmp_args = buff.Mid(ep, it.GetCp() + 2);

			// remove comments and line continuations if any
			CodeCleanup(tmp_args, IsCFile(ed->m_ftype), BitOR(ccDoComment, ccDoCodeLineCont),
			            [](CodeCleanupPart part, WCHAR ch) -> WCHAR {
				            if ((part & ccPartComment) || (part & ccPartLineCont))
					            return ' ';
				            else
					            return ch;
			            });

			tmp_args.ReplaceAllRE(L"\\s+", false, CStringW(L" "));

			EncodeTemplates(tmp_args);

			WTString newSym = dType->SymScope() + tmp_args;
			MultiParsePtr mp(ed->GetParseDb());
			auto tmp = mp->FindExact(newSym.c_str());
			if (tmp)
				dType = std::make_shared<DType>(tmp);
			else
			{
				dType = std::make_shared<DType>(dType.get()); // make a copy
				dType->SetSym(newSym);
			}

			return TRUE;
		}
	}

	return FALSE;
}

BOOL ImplementMethods::CanImplementMethods(bool forceBclUpdate /*= false*/)
{
	mPreconditionsMet = false;
	if (!GlobalProject || GlobalProject->IsBusy())
		return FALSE;

	EdCntPtr ed = g_currentEdCnt;
	if (!ed || !(Is_C_CS_File(ed->m_ftype)))
		return FALSE;

	mMp = ed->GetParseDb();
	if (mMp->IsSysFile())
		return FALSE;

	DType dt(ed->GetSymDtype());
	bool invokedOnImplementer = false;
	if (mMp->m_isDef && (!mMp->m_isMethodDefinition || mMp->m_LastWord == "class" || mMp->m_LastWord == "struct"))
	{
		if (mMp->m_inClassImplementation)
			return FALSE;
		invokedOnImplementer = true;
	}
	else if (mMp->m_isDef && (dt.MaskedType() == CLASS || dt.MaskedType() == STRUCT) &&
	         (!mMp->m_isMethodDefinition || mMp->m_firstWord == "class" || mMp->m_firstWord == "struct") &&
	         mMp->m_LastWord != dt.Sym())
	{
		// [case: 117943] Class names may be prefixed with valueless defines, such as in Unreal Engine 4. In these
		// cases the above m_LastWord check will fail.
		if (mMp->m_inClassImplementation)
			return FALSE;
		invokedOnImplementer = true;
	}
	else if (!mMp->m_inClassImplementation)
		return FALSE;

	const uint baseSymType = dt.MaskedType();
	if (CLASS != baseSymType && STRUCT != baseSymType && C_INTERFACE != baseSymType)
		return FALSE;
	// [case: 91817]
	// 	if (CLASS != baseSymType && STRUCT != baseSymType && C_INTERFACE != baseSymType && DEFINE != baseSymType)
	// 		return FALSE;
	//
	// 	if (baseSymType == DEFINE && invokedOnImplementer)
	// 		return FALSE;

	DTypePtr tmp = mMp->GetCwData();
	if (!tmp || tmp->IsEmpty())
		return FALSE;

	_ASSERTE(ed->GetSymScope() == tmp->SymScope());

	if (invokedOnImplementer)
	{
		// [case: 52445] allow to be invoked on the derived class decl rather than
		// the base class specification
		mThisType = tmp;
		mInvokingScope = tmp->SymScope();
		if (mInvokingScope.IsEmpty())
			return FALSE;
	}
	else
	{
		mInvokingScope = ed->m_lastScope;
		if (mInvokingScope.IsEmpty())
			return FALSE;
		if (mInvokingScope[mInvokingScope.GetLength() - 1] == DB_SEP_CHR)
			mInvokingScope = mInvokingScope.Left(mInvokingScope.GetLength() - 1);
		auto tmp2 = mMp->FindExact(mInvokingScope.c_str());
		if (!tmp2)
			return FALSE;

		mThisType = std::make_shared<DType>(tmp2);

		// [case: 91817]
		// 		if (baseSymType == DEFINE)
		// 		{
		// 			// invoked on macro style base class such as MY_MACRO in:
		// 			// class foo : MY_MACRO(int, double)
		//
		// 			// read fully qualified macro from buffer and expand it
		//
		// 			WTString fullMacro;
		//
		// 			if (!GetCurrentMacro(ed, tmp.get(), fullMacro))
		// 				return FALSE;
		//
		// 			WTString macroExpanded = VAParseExpandAllMacros(mMp, fullMacro);
		//
		// 			macroExpanded.ReplaceAllRE("\\s+", false, CStringA(" "));
		//
		// 			// try to find DType for expansion
		// 			// - if success, use type as base class
		// 			// - else force action on implementer
		//
		// 			EncodeTemplates(macroExpanded);
		// 			tmp2 = mMp->FindExact(macroExpanded);
		// 			if (tmp2 && (tmp2->MaskedType() == CLASS || tmp2->MaskedType() == STRUCT))
		// 			{
		// 				invokedOnImplementer = false;
		// 				tmp = std::make_shared<DType>(tmp2);
		// 			}
		// 			else if (macroExpanded == fullMacro)
		// 			{
		// 				// what user invoked on is not expandable to a valid base type,
		// 				// so we don't allow dialog to open
		// 				return FALSE;
		// 			}
		// 			else
		// 			{
		// 				// If macro is not single type, force behavior as if user clicked on implementer,
		// 				// BaseClassFinder resolves all macros, so user will have an option to choose
		// 				// what he wants to implement.
		// 				invokedOnImplementer = true;
		// 			}
		// 		}
	}

	const uint thisSymType = mThisType->MaskedType();
	if (CLASS != thisSymType && STRUCT != thisSymType)
		return FALSE;

	{
		DB_READ_LOCK;

		// get existing base classes of the implementer (this type)
		mBaseTypes = GetBaseClasses(ed, mThisType.get(), true, true, forceBclUpdate);

		if (invokedOnImplementer)
			mImplTypes = mBaseTypes;
		else
		{
			// tmp is normally only a type representing a declaration
			// of the template (w/o template arguments), however for proper
			// working we need an instantiation of the template, so try to get it.
			if (baseSymType != DEFINE && tmp->IsTemplate())
				GetFullyQualifiedTemplate(ed, tmp);

			// implement base classes of chosen base class
			mImplTypes = GetBaseClasses(ed, tmp.get(), true, false, forceBclUpdate);
		}

		if (mImplTypes.begin() == mImplTypes.end())
			return FALSE;
	}

	mPreconditionsMet = true;
	return TRUE;
}

BOOL ImplementMethods::DoImplementMethods()
{
	CWaitCursor cur;
	if (!CanImplementMethods(true))
		return FALSE;

	mFileInvokedFrom = g_currentEdCnt->FileName();
	if (IsCFile(gTypingDevLang))
	{
		const ProjectVec projForActiveFile(GlobalProject->GetProjectForFile(mFileInvokedFrom));
		for (ProjectVec::const_iterator it1 = projForActiveFile.begin(); it1 != projForActiveFile.end(); ++it1)
		{
			_ASSERTE(*it1);
			if ((*it1)->CppUsesWinRT())
			{
				mIsCppWinrt = true;
				break;
			}
		}

		if (mIsCppWinrt)
		{
			for (DTypeList::iterator it = mImplTypes.begin(); it != mImplTypes.end(); ++it)
			{
				DType& cur2 = *it;
				if (cur2.IsDbNet() && cur2.SymScope() == ":System:IDisposable")
				{
					// IDisposable::Dispose is implemented in C++ winrt as public
					// virtual dtor, not via explicit implementation of Dispose.
					// http://social.msdn.microsoft.com/Forums/en-US/winappswithnativecode/thread/a8be8822-f570-41e9-abfa-11013d146b29
					mImplTypes.erase(it);
					break;
				}
			}

			// replace net interfaces with winrt interfaces
			for (DTypeList::iterator it = mImplTypes.begin(); it != mImplTypes.end(); ++it)
			{
				DType& cur2 = *it;
				if (!cur2.IsDbNet())
					continue;

				for (int idx = 0; !kNetToWinRtTypeReplacements[idx].mReplacementText.IsEmpty(); ++idx)
				{
					if (-1 == kNetToWinRtTypeReplacements[idx].mMdType.Find("::"))
						continue;

					WTString curSymScope(cur2.SymScope());
					WTString netName("::" + kNetToWinRtTypeReplacements[idx].mMdType);
					netName.ReplaceAll("::", DB_SEP_STR.c_str());
					if (-1 == curSymScope.Find(netName.c_str()))
						continue;

					WTString platformName("::" + kNetToWinRtTypeReplacements[idx].mReplacementText);
					platformName.ReplaceAll("::", DB_SEP_STR.c_str());
					curSymScope.ReplaceAll(netName.c_str(), platformName.c_str());
					DType* found = mMp->FindExact(curSymScope.c_str());
					if (found)
						cur2 = DType(*found);
				}
			}
		}
	}

	{
		// [case: 55139]
		const WTString tmpBuf(g_currentEdCnt->GetBuf());
		VAScopeInfo_MLC thisInfo(g_currentEdCnt ? g_currentEdCnt->m_ftype : 0);
		const WTString defFromLine(thisInfo->GetDefFromLine(tmpBuf, (ULONG)g_currentEdCnt->CurLine()));
		thisInfo->ParseEnvArgs();
		mThisIsTemplate = thisInfo->GetTemplateStr().GetLength() > 0;
	}

	for (DTypeList::iterator it = mImplTypes.begin(); it != mImplTypes.end(); ++it)
		BuildListOfMethods(*it);

	CheckForImplementedMethods();
	SortMethodList();
	ReviewCheckedItems();
	cur.Restore();

	if (!mDlgParams.mNodeItems.size())
	{
		::ErrorBox("No methods to implement were identified.", MB_OK | MB_ICONEXCLAMATION);
		return FALSE;
	}

	if (mHaveNonInterfacePureMethods || mHaveNonInterfaceVirtualMethods)
		mDisplayPrompt = true; // display by default only if found non-interface base methods

	if (mDisplayPrompt)
	{
		mDlgParams.mCaption = "Implement Methods";
		mDlgParams.mDirectionsText = "Select methods to implement/override:";
		mDlgParams.mHelpTopic = "dlgImplementInterface";
		VAUpdateWindowTitle(VAWindowType::ImplementMethods, mDlgParams.mCaption);

		GenericTreeDlg dlg(mDlgParams);

		int total_methods = 0;
		for (auto& item : mDlgParams.mNodeItems)
		{
			item.SetState(RootNode, true); // necessary for filtering out empty roots
			item.SetState(Visible, true);

			for (auto& sub_item : item.mChildren)
			{
				sub_item.SetState(Visible, true);
				sub_item.SetState(FilterMatch, true);
				total_methods++;
			}
		}

		if (total_methods > 1)
		{
			// at load of the dialog, if hide implemented
			// is checked and if the list is empty,
			// then uncheck hide implemented

#if (ADD_HIDE_IMPLEMENTED_METHODS == 1)
			if (IVMO::Get(IVMO::HideImplemented))
			{
				bool any_visible = false;
				for (auto& parent : mDlgParams.mNodeItems)
				{
					for (auto& item : parent.mChildren)
					{
						if (!item.GetState(Implemented) && !item.GetState(ImplementedInBase))
						{
							any_visible = true;
							break;
						}
					}
					if (any_visible)
						break;
				}

				if (!any_visible)
					IVMO::Set(IVMO::HideImplemented, false);
			}
#else
			IVMO::Set(IVMO::HideImplemented, false);
#endif

			// the storages for states
			std::shared_ptr<WTString> mFilter = std::make_shared<WTString>();

			mDlgParams.mApproveItem = [](const GenericTreeNodeItem& item) { return item.GetState(Visible); };

			auto apply_visibility = [this]() -> void {
				for (auto& parent : mDlgParams.mNodeItems)
				{
					bool any_approved = false;
					bool any_unchecked = false;

					for (auto& item : parent.mChildren)
					{
						bool approved = item.GetState(FilterMatch);

#if (ADD_HIDE_IMPLEMENTED_METHODS == 1)
						if (approved && IVMO::Get(IVMO::HideImplemented))
							approved = !item.GetState(Implemented) && !item.GetState(ImplementedInBase);
#endif

						if (!approved)
						{
							item.SetState(CheckedHidden, item.mChecked || item.GetState(CheckedHidden));
							item.mChecked = false;
						}
						else if (item.GetState(CheckedHidden))
						{
							item.mChecked = true;
							item.SetState(CheckedHidden, false);
						}

						if (approved)
						{
							any_approved = true;

							if (!item.mChecked)
								any_unchecked = true;
						}

						item.SetState(Visible, approved);
					}

					parent.SetState(Visible, any_approved);
					// parent.mChecked = any_unchecked ? false : true;
				}
			};

			// ************************************************************
			// *					 METHODS FILTERING
			// ************************************************************

			// method starts timer 'fltr' when user changes filter's text,
			// timer is handled in WM_TIMER MessageHandler below
			auto set_filter = [&dlg, mFilter](WTString str, uint delay) mutable {
				str.TrimRightChar('-');
				str.Trim();

				// check if filter has changed
				if (*mFilter != str)
				{
					*mFilter = str;

					// filter has changed, restart timer
					dlg.SetTimer('fltr', delay, nullptr);
				}
			};

			auto apply_filter = [this, mFilter, &dlg, apply_visibility](bool update_items) {
				// save last used item etc..
				if (update_items)
					dlg.SaveView();

				if (IVMO::Get(IVMO::FilterByNameOnly))
				{
					// filter nodes by symbol name
					// Note: sym is captured by value into mutable lambda to provide a storage for symbol name

					WTString sym;
					auto getSymbol = [sym](GenericTreeNodeItem& item) mutable -> LPCSTR {
						DType* dt = static_cast<DType*>(item.mData);
						sym = dt->Sym();
						return sym.c_str();
					};

					GenericTreeDlgParams::FilterNodes(mDlgParams.mNodeItems, 1, -1, *mFilter,
					                                  (GenericTreeNodeItem::State)FilterMatch, getSymbol);
				}
				else
				{
					// filter nodes by whole text
					GenericTreeDlgParams::FilterNodes(mDlgParams.mNodeItems, 1, -1, *mFilter,
					                                  (GenericTreeNodeItem::State)FilterMatch);
				}

				apply_visibility();

				if (update_items)
					dlg.UpdateNodes(true, true);
			};

			// This message handler processes timer started in lambda apply_filter
			// used in OnChange event of filter edit control.
			dlg.WndProcEvents.AddMessageHandler(
			    WM_TIMER, [&dlg, apply_filter](WndProcEventArgs<CThemedVADlg>& args) mutable -> bool {
				    if (args.wParam != (WPARAM)'fltr')
					    return false;

				    dlg.KillTimer('fltr');

				    apply_filter(true);

				    return false;
			    });

			// This notify handler gathers information about last focused control,
			// but only if it is not a check box
			std::shared_ptr<UINT> mLastFocus(new UINT(0));
			dlg.WndProcEvents.AddNotifyHandler(NM_SETFOCUS, [&dlg, mLastFocus](UINT id_from) -> bool {
				CWnd* wnd = dlg.GetDlgItem((int)id_from);

				auto is_check_box = [](CWnd* wnd) -> bool {
					if (::GetWindowClassString(wnd->m_hWnd).CompareNoCase("BUTTON") == 0)
					{
						DWORD style = wnd->GetStyle();
						return (style & BS_CHECKBOX) == BS_CHECKBOX || (style & BS_AUTOCHECKBOX) == BS_AUTOCHECKBOX;
					}
					return false;
				};

				if (!is_check_box(wnd))
					*mLastFocus = id_from;

				return false;
			});

			auto apply_last_focus = [&dlg, mLastFocus]() mutable {
				if (mLastFocus && *mLastFocus)
				{
					CWnd* wnd = dlg.GetDlgItem((int)*mLastFocus);
					if (wnd)
						wnd->SetFocus();
				}
			};

			LPCSTR dim_text = nullptr;
			if (Psettings->mForceCaseInsensitiveFilters)
				dim_text = "substring andSubstring -exclude .beginWith endWith.";
			else
				dim_text = "substring andsubstring matchCase -exclude .beginwith endwith.";

			// Inform a dialog to add a CThemedDimEdit control in initialization process
			// When user changes it's text, apply_filter is called.
			mDlgParams
			    .AddDimEdit(
			        RowOrder_BottomToTop + 2, dim_text,
			        [set_filter](CThemedDimEdit& edit) mutable {
				        CStringW str;
				        if (edit.GetText(str))
					        set_filter(WTString(str), 250u);
			        },
			        [mLastFocus](CThemedDimEdit& edit) {
				        edit.WndProcEvents.AddMessageHandler(
				            WM_SETFOCUS, [mLastFocus, &edit](CThemedDimEdit::EventArgsT& args) mutable -> bool {
					            *mLastFocus = (UINT)edit.GetDlgCtrlID();
					            return false;
				            });
			        })
			    .SetPlacement(true, 0, 100)
			    .SetDynamics(0, 100, 100, 100);

			// ************************************************************
			// *					 METHODS SORTING
			// ************************************************************

			// method used to sort methods alphabetically or by line number
			auto do_sort = [this, &dlg](bool update_items) mutable {
				if (update_items)
					dlg.SaveView();

				GenericTreeDlgParams::SortNodes(
				    mDlgParams.mNodeItems, 1, -1,
				    [](const GenericTreeNodeItem& n1, const GenericTreeNodeItem& n2) -> bool {
					    DType* n1dt = static_cast<DType*>(n1.mData);
					    DType* n2dt = static_cast<DType*>(n2.mData);

					    if (!IVMO::Get(IVMO::SortAlphabetically))
						    return n1dt->Line() < n2dt->Line();
					    else
					    {
						    int cmp = NaturalCompare(n1dt->Sym().Wide(), n2dt->Sym().Wide());
						    if (cmp == 0)
							    return NaturalCompare(n1.mNodeText.Wide(), n2.mNodeText.Wide()) < 0;
						    return cmp < 0;
					    }
				    });

				if (update_items)
					dlg.UpdateNodes(true, true);
			};

			// Inform a dialog to add a CThemedCheckBox control in initialization process
			// When user clicks on it, do_sort is applied.
			mDlgParams
			    .AddCheckBox(
			        RowOrder_BottomToTop + 1, "&Sort methods by name",
			        [do_sort, apply_last_focus](CThemedCheckBox& chb) mutable {
				        IVMO::Set(IVMO::SortAlphabetically, chb.GetCheck() != FALSE);
				        do_sort(true);
				        apply_last_focus();
			        },
			        [](CThemedCheckBox& chb) { chb.SetCheck(IVMO::Get(IVMO::SortAlphabetically)); })
			    .SetPlacement(true, 50, 100)
			    .SetDynamics(50, 100, 100, 100);

			// ************************************************************
			// *		SHOW ONLY ABSTRACT (PURE VIRTUAL) METHODS
			// ************************************************************

			// Inform a dialog to add a CThemedCheckBox control in initialization process
			// When user clicks on it, HideImplemented is applied.
#if (ADD_HIDE_IMPLEMENTED_METHODS == 1)

			mDlgParams
			    .AddCheckBox(
			        RowOrder_BottomToTop + 0, "&Hide implemented classes/methods",
			        [this, &dlg, apply_visibility, apply_last_focus](CThemedCheckBox& chb) mutable {
				        dlg.SaveView();
				        IVMO::Set(IVMO::HideImplemented, chb.GetCheck() != FALSE);
				        apply_visibility();
				        dlg.UpdateNodes(true, true);
				        apply_last_focus();
			        },
			        [](CThemedCheckBox& chb) { chb.SetCheck(IVMO::Get(IVMO::HideImplemented)); })
			    .SetPlacement(true, 0, 100)
			    .SetDynamics(0, 100, 100, 100);
#endif
			// ************************************************************
			// *		FILTER BY NAME ONLY
			// ************************************************************

			// Inform a dialog to add a CThemedCheckBox control in initialization process
			// When user clicks on it, FilterByNameOnly is applied.
			mDlgParams
			    .AddCheckBox(
			        RowOrder_BottomToTop + 1, "Filter by &name only",
			        [apply_filter, apply_last_focus](CThemedCheckBox& chb) mutable {
				        IVMO::Set(IVMO::FilterByNameOnly, chb.GetCheck() != FALSE);
				        apply_filter(true);
				        apply_last_focus();
			        },
			        [](CThemedCheckBox& chb) { chb.SetCheck(IVMO::Get(IVMO::FilterByNameOnly)); })
			    .SetPlacement(true, 0, 50)
			    .SetDynamics(0, 50, 100, 100);

			mDlgParams.mOnInitialised = [apply_visibility, do_sort]() mutable {
				apply_visibility();
				do_sort(false);
			};
		}

		const INT_PTR dlgRet = dlg.DoModal();
		if (IDOK != dlgRet)
		{
			if (-1 == dlgRet)
				::ErrorBox("The dialog failed to be created.", MB_OK | MB_ICONERROR);
			return FALSE;
		}
	}

	const int cnt = ImplementMethodList();
	if (!cnt && !mExecutionError)
	{
		if (gTestLogger)
			gTestLogger->LogStr("MsgBox: No methods implemented.");
		else
			WtMessageBox("No methods implemented.", IDS_APPNAME, MB_OK);
	}
	else if (cnt)
	{
		CString msg;
		CString__FormatA(msg, "%d methods implemented.", cnt);
		SetStatus(msg);
	}

	if (mExecutionError)
		::OnRefactorErrorMsgBox();

	return !mExecutionError;
}

WTString ImplementMethods::GetCommandText()
{
	if (mPreconditionsMet)
	{
		for (DTypeList::iterator it = mImplTypes.begin(); it != mImplTypes.end(); ++it)
		{
			const uint sType = (*it).MaskedType();
			if (CLASS == sType || STRUCT == sType)
			{
				const WTString baseDef((*it).Def());
				if (-1 == baseDef.Find("interface ") && -1 == baseDef.Find("MIDL_INTERFACE"))
					return WTString("Implement Vir&tual Methods...");
			}
		}
	}

	return WTString("Implement In&terface");
}

WTString AddUFunctionPostfixToSymScope(WTString symScope, UnrealPostfixType unrealPostfixType)
{
	switch (unrealPostfixType)
	{
	case UnrealPostfixType::Implementation:
		symScope.append("_Implementation");
		break;

	case UnrealPostfixType::Validate:
	case UnrealPostfixType::ValidateFollowingImplementation:
		symScope.append("_Validate");
		break;

	default:
		break;
	}

	return symScope;
}

WTString ChangeReturnToBool(WTString def, const WTString& sym)
{
	// [case: 131103] modified to not remove "virtual"
	int symStart = def.Find(sym.c_str());
	if (symStart != -1)
	{
		int virtualStart = def.Find(L"virtual");
		if (virtualStart == -1)
		{
			def.ReplaceAt(0, symStart, "bool ");
		}
		else
		{
			int virtualEnd = virtualStart + 7;
			if (virtualEnd < symStart)
				def.ReplaceAt(virtualEnd, symStart - virtualEnd, " bool ");
		}
	}

	return def;
}

void ImplementMethods::BuildListOfMethods(DType& baseDtype)
{
	GenericTreeNodeItem baseType;
	DTypeList members;

	bool assumeInterface = false;
	if (IsCFile(gTypingDevLang))
	{
		// search baseDtype def for interface or MIDL_INTERFACE - override mHaveNonInterfacePureMethods if found
		const WTString baseDef(baseDtype.Def());
		if (-1 != baseDef.Find("interface ") || -1 != baseDef.Find("MIDL_INTERFACE"))
			assumeInterface = true;
	}

	WTString baseDtypeSymScope(baseDtype.SymScope());
	g_pGlobDic->GetMembersList(NULLSTR, baseDtypeSymScope, members);
	if (!members.size() || Src == ::GetFileType(mFileInvokedFrom))
	{
		// if invoked in Src file, check localHcbDb even if stuff found in project.
		// cpp file defined class will have GOTO_DEF dtypes in project db (members.size() != 0)
		mMp->LocalHcbDictionary()->GetMembersList(NULLSTR, baseDtypeSymScope, members);
	}
	if (!members.size())
	{
		g_currentEdCnt->GetSysDicEd()->GetMembersList(NULLSTR, baseDtypeSymScope, members);
		if (baseDtypeSymScope == ":IUnknown")
		{
			// special-case IUnknown
			if (baseDtype.MaskedType() != C_INTERFACE)
				baseDtype.setType(C_INTERFACE, baseDtype.Attributes(), baseDtype.DbFlags());

			for (DTypeList::iterator it = members.begin(); it != members.end(); ++it)
			{
				DType& cur = *it;
				if (cur.Sym() == "QueryInterface" && !cur.IsDbNet())
					cur.SetDef("virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject)");
			}
		}
	}

	for (DTypeList::iterator it = members.begin(); it != members.end(); ++it)
	{
		bool chkDefaultValue = true;
		bool itemEnabled = true;

		DType& cur = *it;
		if (cur.MaskedType() != FUNC)
			continue;

		WTString def(cur.Def());
		if (def.Find('\f') != -1)
			def = TokenGetField(def.c_str(), "\f");

		def = ::DecodeTemplates(def); // [case: 55727] [case: 57625]

		CStringW uFunctionParams;
		if (Psettings->mUnrealEngineCppSupport)
		{
			// [case: 117506] [UE4] support unreal UFUNCTIONs in Implement Virtual Methods
			GetUFunctionParametersForSym(cur.SymScope().c_str(), &uFunctionParams);
			if (DoCreateImpForUFunction(uFunctionParams) || DoCreateValForUFunction(uFunctionParams))
			{
				if (!strstrWholeWord(def, "virtual"))
				{
					// [case: 131103] add missing virtual keyword to implicitly virtual UFUNCTION
					def = L"virtual " + def;
				}
			}
			// remove the undocumented PURE_VIRTUAL macro
			// see for use example: https://answers.unrealengine.com/questions/762428/pure-virtual-interface.html
			int pvStart = def.Find("PURE_VIRTUAL");
			if (pvStart != -1)
			{
				def = def.Left(pvStart);
				def.TrimRight(); // in case there were several spaces between the method name and the macro
				def += " = 0";
			}
		}

		bool doAdd = false;
		if (::strstrWholeWord(def, "sealed") || ::strstrWholeWord(def, "final"))
		{
			// [case: 65703] sealed/final methods can't be overridden
			doAdd = true;
			itemEnabled = chkDefaultValue = false;
			// [case: 93821]
			mFinalMethods.push_back(cur);
		}
		else if (C_INTERFACE == baseDtype.MaskedType())
		{
			doAdd = true;
		}
		else if (IsCFile(gTypingDevLang))
		{
			if (-1 != def.Find("IFACEMETHOD"))
			{
				// interface of some sort
				doAdd = true;
			}
			else
			{
				// base must be class or struct
				const int pos = def.ReverseFind(')');
				if (-1 != pos)
				{
					if (Psettings->mUnrealEngineCppSupport &&
					    (DoCreateImpForUFunction(uFunctionParams) || DoCreateValForUFunction(uFunctionParams)))
					{
						// [case: 117506] [UE4] support unreal UFUNCTIONs in Implement Virtual Methods
						if (!(cur.Attributes() & V_CONSTRUCTOR))
						{
							doAdd = true;
							mHaveNonInterfaceVirtualMethods = true;
							chkDefaultValue = false;
						}
					}
					else if (-1 != def.Find("=0", pos) || -1 != def.Find("= 0", pos) || -1 != def.Find("PURE", pos) ||
					         strstrWholeWord(def, "abstract"))
					{
						// it's an interface (or close enough)
						// no dtors
						if (!(cur.Attributes() & V_CONSTRUCTOR))
						{
							doAdd = true;
							if (!assumeInterface)
								mHaveNonInterfacePureMethods = true;
						}
					}
					else if (strstrWholeWord(def, "virtual") || // [case: 112700]
					         strstrWholeWord(def, "override") ||
					         strstrWholeWord(baseDtype.Def(), "abstract")) // abstract base class, add all methods
					{
						// allow virtual functions (but not dtor)
						if (!(cur.Attributes() & V_CONSTRUCTOR))
						{
							doAdd = true;
							mHaveNonInterfaceVirtualMethods = true;
							chkDefaultValue = false;
						}
					}
				}
			}
		}
		else if (CS == gTypingDevLang)
		{
			if (-1 != def.Find("abstract "))
			{
				// close enough
				doAdd = true;
				mHaveNonInterfacePureMethods = true;
			}
			else if (-1 != def.Find("virtual ") || -1 != def.Find("override "))
			{
				// allow virtual and override functions
				// http://msdn.microsoft.com/en-us/library/ebca9ah3.aspx
				doAdd = true;
				mHaveNonInterfaceVirtualMethods = true;
				chkDefaultValue = false;
			}
		}

		if (!doAdd)
			continue;

		def.ReplaceAll("BEGIN_INTERFACE", "", TRUE);
		def.ReplaceAll("  ", " ");
		def.TrimLeft();
		const int parenPos = def.ReverseFind(')');
		if (-1 != parenPos)
		{
			int pos = def.Find("=0", parenPos);
			if (-1 != pos)
				def = def.Left(pos);
			pos = def.Find("= 0", parenPos);
			if (-1 != pos)
				def = def.Left(pos);
			pos = def.Find("PURE", parenPos);
			if (-1 != pos)
				def = def.Left(pos);
			def.ReplaceAll("{...}", "");
			def.TrimRight();
		}

		// store cleaned up version back into dtype
		cur.SetDef(def);

		auto CheckForExistingMethod = [&]() {
			// make sure cur.Sym() does not already exist in mMethodDts
			for (DTypeList::iterator it2 = mMethodDts.begin(); it2 != mMethodDts.end(); ++it2)
			{
				if ((*it2).Sym() == cur.Sym())
				{
					// [case: 54587] allow overloads
					if (::AreSymbolDefsEquivalent(cur, *it2, true))
					{
						if ((*it2).Scope() == cur.Scope())
						{
							// identical sym in same scope, don't display at all
							doAdd = false;
							break;
						}
						else
						{
							// same sym name but in different scope, display as read-only
							itemEnabled = false;
							chkDefaultValue = false;
							// don't break, continue to check for same scope
						}
					}
				}
			}
		};

		auto AddMethod = [&]() {
			GenericTreeNodeItem baseMethod;
			mMethodDts.push_back(cur);
			baseMethod.mNodeText = cur.Def();
			baseMethod.mIconIndex = ::GetTypeImgIdx(cur.MaskedType(), cur.Attributes());
			baseMethod.mChecked = chkDefaultValue;
			baseMethod.mEnabled = itemEnabled;
			if (!chkDefaultValue)
			{
				if (!itemEnabled)
					baseMethod.SetState(Implemented, true);
				else
					baseMethod.SetState(ImplementedInBase, true);
			}
			baseMethod.mData = &(mMethodDts.back());
			baseType.mData = &baseDtype;
			baseType.mChildren.push_back(baseMethod);
		};

		if (Psettings->mUnrealEngineCppSupport &&
		    (DoCreateImpForUFunction(uFunctionParams) || DoCreateValForUFunction(uFunctionParams)))
		{
			// [case: 117506] [UE4] support unreal UFUNCTIONs in Implement Virtual Methods
			WTString symScope = cur.SymScope();
			WTString def2 = cur.Def();
			if (DoCreateImpForUFunction(uFunctionParams))
			{
				cur.SetStrs(AddUFunctionPostfixToSymScope(symScope, UnrealPostfixType::Implementation),
				            AddUFunctionPostfix(def2, UnrealPostfixType::Implementation));
				// save off any variables that may be changed by CheckForExistingUeMethod, in case *_Validate needs to
				// be checked as well below
				bool oldDoAdd = doAdd;
				bool oldItemEnabled = itemEnabled;
				bool oldChkDefaultValue = chkDefaultValue;
				CheckForExistingMethod();
				if (doAdd)
					AddMethod();
				doAdd = oldDoAdd;
				itemEnabled = oldItemEnabled;
				chkDefaultValue = oldChkDefaultValue;
			}

			if (DoCreateValForUFunction(uFunctionParams))
			{
				cur.SetSym(AddUFunctionPostfixToSymScope(symScope, UnrealPostfixType::Validate));
				cur.SetDef(ChangeReturnToBool(AddUFunctionPostfix(def2, UnrealPostfixType::Validate), cur.Sym()));
				CheckForExistingMethod();
				if (doAdd)
					AddMethod();
			}
		}
		else
		{
			CheckForExistingMethod();
			if (doAdd)
				AddMethod();
		}
	}

	if (baseType.mChildren.size())
	{
		baseType.mNodeText = ::DecodeTemplates(::StrGetSym(baseDtypeSymScope));
		baseType.mIconIndex = ::GetTypeImgIdx(baseDtype.MaskedType(), baseDtype.Attributes());
		mDlgParams.mNodeItems.push_back(baseType);
	}
}

void ImplementMethods::CheckForImplementedMethods()
{
	// iterate over mDlgParams.mNodeItems and see if already implemented in mInvokingScope
	// if so, mark as checked and disabled
	for (GenericTreeNodeItem::NodeItems::iterator it = mDlgParams.mNodeItems.begin(); it != mDlgParams.mNodeItems.end();
	     ++it)
	{
		GenericTreeNodeItem& parentNode = *it;
		DType* parentType = static_cast<DType*>(parentNode.mData);

		for (GenericTreeNodeItem::NodeItems::iterator it2 = parentNode.mChildren.begin();
		     it2 != parentNode.mChildren.end(); ++it2)
		{
			GenericTreeNodeItem& curNode = *it2;
			DType* curType = static_cast<DType*>(curNode.mData);

			bool foundImplementation = false;
			bool foundImplementationInThisTypeOrWasFinal = false;

			auto processBaseType = [&](DType& baseType) {
				bool isThisType = (baseType.SymHash() == mThisType->SymHash());
				if (!isThisType && baseType.SymHash() == parentType->SymHash())
					return;

				WTString newFn(baseType.SymScope() + DB_SEP_STR);
				newFn += ::StrGetSym(curType->SymScope());
				DTypeList dats;
				int hits = mMp->FindExactList(newFn.c_str(), dats);
				if (hits)
				{
					for (DTypeList::iterator it3 = dats.begin(); it3 != dats.end(); ++it3)
					{
						DType& dat = *it3;

						// [case: 54587] allow overloads
						if (::AreSymbolDefsEquivalent(dat, *curType, true))
						{
							foundImplementation = true;
							if (isThisType)
							{
								foundImplementationInThisTypeOrWasFinal = true;
								break;
							}
						}
					}
				}
			};

			processBaseType(*mThisType);

			// [case: 93821]
			auto disableFinals = [&](DType& finalType) {
				if (curType->SymHash() != finalType.SymHash())
					return;

				if (curType->Sym() != finalType.Sym())
					return;

				if (::AreSymbolDefsEquivalent(*curType, finalType, true))
				{
					// no derived class may implement
					// (watch out for overloads like Dispose on System::Windows::Forms::Button)
					foundImplementation = foundImplementationInThisTypeOrWasFinal = true;
				}
			};

			for (auto& it3 : mFinalMethods)
				disableFinals(it3);

			if (!foundImplementation)
				for (DTypeList::iterator it3 = mBaseTypes.begin(); it3 != mBaseTypes.end(); ++it3)
					processBaseType(*it3);

			if (foundImplementationInThisTypeOrWasFinal)
			{
				// already implemented in this type
				curNode.mChecked = false;
				curNode.mEnabled = false;
				curNode.SetState(Implemented, true);
			}
			else if (foundImplementation)
			{
				// already implemented in base type
				curNode.mChecked = false;
				curNode.SetState(ImplementedInBase, true);
			}
			else
			{
				// not implemented
				// use defaults
			}
		}
	}
}

int ImplementMethods::ImplementMethodList()
{
	CWaitCursor curs;
	FreezeDisplay _f;
	int cnt = 0;
	UndoContext undoContext("ImplementMethods");
	for (GenericTreeNodeItem::NodeItems::iterator it = mDlgParams.mNodeItems.begin(); it != mDlgParams.mNodeItems.end();
	     ++it)
	{
		GenericTreeNodeItem& parentNode = *it;
		DType* parentType = static_cast<DType*>(parentNode.mData);

		for (GenericTreeNodeItem::NodeItems::iterator it2 = (*it).mChildren.begin(); it2 != (*it).mChildren.end();
		     ++it2)
		{
			GenericTreeNodeItem& curNode = *it2;
			if (!(curNode.mEnabled && curNode.mChecked))
				continue;

			DType* curType = static_cast<DType*>(curNode.mData);
			if (ImplementBaseMethod(*parentType, *curType, _f))
			{
				if (g_currentEdCnt)
					g_currentEdCnt->GetBuf(TRUE);
				++cnt;
			}
			else
				mExecutionError = true;
		}
	}

	return cnt;
}

BOOL ImplementMethods::ImplementBaseMethod(DType& baseType, DType& curType, FreezeDisplay& _f)
{
	// curType is a FUNC DType from a base class
	// implement in mInvokingScope
	BOOL rslt = FALSE;
	const WTString sym(curType.Sym());
	const WTString newMethodScope(mInvokingScope + DB_SEP_STR + sym);
	WTString codeToAdd(GenerateSourceToAdd(baseType, curType));
	if (codeToAdd.IsEmpty())
		return rslt;

	// this is based on CreateFromUsage::InsertXref (which is based on the add member refactoring)
	if (::GotoDeclPos(newMethodScope, mFileInvokedFrom, FUNC))
	{
		EdCntPtr curEd(g_currentEdCnt);
		_f.ReadOnlyCheck();

		// case 55728 Implement Interface to respect visibility of the methods in the base class
		if (IsCFile(curEd->m_ftype))
		{
			const uint kCurPos = curEd->CurPos();
			ULONG ln = TERROW(kCurPos);
			WTString fileText(curEd->GetBuf(TRUE));
			MultiParsePtr mp = curEd->GetParseDb();
			LineMarkers markers; // outline data
			GetFileOutline(fileText, markers, mp);
			SymbolVisibility symVisibility = vPublic;
			if (curType.IsPrivate())
				symVisibility = vPrivate;
			if (curType.IsProtected())
				symVisibility = vProtected;
			if (curType.IsPublished())
				symVisibility = vPublished;
			CreateFromUsage::FindLnToInsert insert(symVisibility, ln, newMethodScope, fileText);
			std::pair<int, bool> insertLocation = insert.GetLocationByVisibility(markers.Root());
			if (insertLocation.first >= 0)
			{
				curEd->SetPos((uint)curEd->GetBufIndex(fileText, (long)curEd->LinePos(insertLocation.first)));
				ln = (ULONG)insertLocation.first;
			}
			if (insertLocation.second)
			{
				// const WTString implCodelnBrk(EolTypes::GetEolStr(CString(codeToAdd)));
				codeToAdd = CreateFromUsage::GetLabelStringWithColon(symVisibility) + /*implCodelnBrk + */ codeToAdd;
			}
		}

		// do this after DelayFileOpen - dependent upon target file EOF
		if (TERCOL(curEd->CurPos()) > 1)
		{
			const WTString implCodelnBrk(EolTypes::GetEolStr(codeToAdd));
			codeToAdd = implCodelnBrk + codeToAdd; // eof needs extra CRLF
			rslt = gAutotextMgr->InsertAsTemplate(curEd, WTString(" ") + codeToAdd, TRUE);
		}
		else
			rslt = gAutotextMgr->InsertAsTemplate(curEd, codeToAdd, TRUE);
	}

	if (!rslt)
		return rslt;

	if (mThisIsTemplate)
		return rslt;

	if (!IsCFile(GetFileType(mFileInvokedFrom)))
		return rslt;

	const CStringW srcFile = ::GetFileByType(mFileInvokedFrom, Src);
	if (srcFile.IsEmpty())
		return rslt;
	if (srcFile == mFileInvokedFrom)
	{
		// MoveImplementationToSrcFile does not work in this case (I guess that's why
		// CreateFromUsage doesn't do it either).
		// Trouble in VARefactorCls ctor caused by parsing CurLine:
		//		mDecLine = mInfo->GetDefFromLine(tmpBuf, g_currentEdCnt->CurLine());
		return rslt;
	}

	// [case: 57625] don't do move implementation on operators
	if (sym.GetLength() && strchr("!%^&*-+<>/=[|(", sym[0]))
		return rslt;

	// Move to source file
	// Pass method scope because caret is not on new method
	if (g_currentEdCnt) // declFile
		g_currentEdCnt->GetBuf(TRUE);
	VARefactorCls ref(newMethodScope.c_str());
	const DType dt(g_currentEdCnt->GetSymDtype());
	if (ref.CanMoveImplementationToSrcFile(&dt, "", TRUE))
		rslt = ref.MoveImplementationToSrcFile(&dt, "", TRUE);
	return rslt;
}

// http://msdn.microsoft.com/en-us/library/bfft1t3c.aspx
// http://msdn.microsoft.com/en-us/library/s1ax56ch.aspx
// http://msdn.microsoft.com/en-us/library/ya5y69ds.aspx
const WTString kNetAndWrtValueTypes[] = {
    "bool", "Boolean", "Byte", "char16", "Char", "Decimal", "Double", "enum", "float", "float32",
    "float64", "Int", "Int16", "Int32", "Int64", "Int8", "long", "SByte", "short", "Single",
    "struct", "uint", "uint16", "uint32", "uint64", "uint8", "ulong", "ushort", "Void", ""};

static WTString ConvertNetToCpp(WTString str, bool isReturnTypeString, bool isWinrt)
{
	// [case: 64052] add handle and convert type[] to array<type>
	if (str.IsEmpty())
		return str;

	str.ReplaceAll(".", "::");

	WTString paramList;
	token2 params(str);
	int cnt = 0;
	do
	{
		WTString cur(params.read(','));
		cur.Trim();
		if (cur.IsEmpty())
			break;

		if (cnt++)
			paramList += ", ";

		const int kArrayPos = cur.Find("[]");
		if (-1 != kArrayPos)
		{
			WTString tmp;
			if (isWinrt)
				tmp = "const Platform::Array<";
			else
				tmp = "array<";

			const WTString tmp2(cur.Left(kArrayPos));
			tmp += tmp2;
			if (-1 != tmp2.Find("System::String") || -1 != tmp2.Find("System::Object"))
				tmp += "^";

			tmp += ">";
			tmp += cur.Mid(kArrayPos + 2);
			cur = tmp;
		}

		bool addHat = true;
		for (int idx = 0; addHat && !kNetAndWrtValueTypes[idx].IsEmpty(); ++idx)
		{
			if (strstrWholeWord(cur, kNetAndWrtValueTypes[idx], FALSE))
				addHat = false;
		}

		if (addHat)
		{
			// winrt structs are by value, don't use hat.
			// ideally we should be looking up each param and checking def
			// this is a hack for platform structs.
			// http://msdn.microsoft.com/en-us/library/windows/apps/hh700121.aspx
			if (strstrWholeWord(cur, "TimeSpan") || strstrWholeWord(cur, "Guid") ||
			    strstrWholeWord(cur, "DateTimeOffset") || strstrWholeWord(cur, "EventRegistrationToken") ||
			    strstrWholeWord(cur, "Exception") || strstrWholeWord(cur, "IntPtr") ||
			    strstrWholeWord(cur, "UIntPtr") || strstrWholeWord(cur, "Point") || strstrWholeWord(cur, "Rect") ||
			    strstrWholeWord(cur, "SizeT") || strstrWholeWord(cur, "Size"))
			{
				addHat = false;
			}
		}

		if (addHat || -1 != kArrayPos)
		{
			if (isReturnTypeString)
				cur += "^";
			else
			{
				const int spacePos = cur.ReverseFind(' ');
				if (-1 != spacePos)
					cur.insert(spacePos, "^");
			}
		}

		paramList += cur;
	} while (params.more());

	if (!paramList.IsEmpty() && '>' == paramList[paramList.GetLength() - 1])
		paramList += "^";

	if (isWinrt)
	{
		for (int idx = 0; !kNetToWinRtTypeReplacements[idx].mReplacementText.IsEmpty(); ++idx)
			paramList.ReplaceAll(kNetToWinRtTypeReplacements[idx].mMdType.c_str(),
			                     kNetToWinRtTypeReplacements[idx].mReplacementText.c_str(), TRUE);

		if (-1 != paramList.Find("System::"))
		{
			vLog("ERROR: Failed to convert usage of 'System::' in paramList for WinRt implementation (%s)",
			     paramList.c_str());
		}

		// In WinRT, all parameters are either for input or for output; there are no ref
		// parameters.
		// Remove any out or ref notations.
		int replaceCnt = paramList.ReplaceAll("out", "", TRUE);
		replaceCnt += paramList.ReplaceAll("ref", "", TRUE);
		if (replaceCnt)
		{
			paramList.ReplaceAll("< ", "<", FALSE);
			paramList.ReplaceAll(", ", ",", FALSE);
			paramList.Trim();
		}
	}

	return paramList;
}

WTString ImplementMethods::GenerateSourceToAdd(DType& baseType, DType& curType)
{
	// based on CreateFromUsage::GenerateSourceToAddForFunction
	bool checkVirtualAndOverride = IsCFile(gTypingDevLang);
	const bool kCppNetConvert = IsCFile(gTypingDevLang) && curType.IsDbNet();
	WTString tmp, methodSig(curType.Def());
	WTString methodName(curType.Sym());
	methodSig.TrimRight();
	if (methodSig[methodSig.GetLength() - 1] == ';')
	{
		methodSig = methodSig.Left(methodSig.GetLength() - 1);
		methodSig.TrimRight();
	}

	// parse out param list
	int tmpPos = methodSig.Find(methodName.c_str());
	if (-1 == tmpPos)
		return NULLSTR;
	const int openPos = methodSig.Find("(", tmpPos);
	if (-1 == openPos)
		return NULLSTR;
	const int closePos = methodSig.Find(")", openPos);
	if (-1 == closePos)
		return NULLSTR;
	tmp = methodSig.Mid(openPos + 1, closePos - openPos - 1);
	tmp.Trim();
	if (curType.IsDbNet())
	{
		if (kCppNetConvert)
		{
			tmp = ::ConvertNetToCpp(tmp, false, mIsCppWinrt);
		}
		else
		{
			for (int idx = 0; !kMdExportToManagedSystemAliasReplacements[idx].mReplacementText.IsEmpty(); ++idx)
				tmp.ReplaceAll(kMdExportToManagedSystemAliasReplacements[idx].mMdType.c_str(),
				               kMdExportToManagedSystemAliasReplacements[idx].mReplacementText.c_str(), TRUE);
		}
	}
	const WTString paramList(tmp);

	// parse out qualifiers
	tmp = methodSig.Mid(closePos + 1);
	tmp.TrimLeft();
	WTString qualifiers(tmp);

	// parse out newMethodName
	tmp = methodSig.Left(openPos);
	tmp.TrimRight();
	const int namePos = tmp.ReverseFind(methodName.c_str());
	if (-1 == namePos)
	{
		_ASSERTE(!"name not found in def");
		return NULLSTR;
	}

	// parse out return type
	tmp = tmp.Left(namePos);
	tmp.TrimRight();
	if (curType.IsDbNet())
	{
		if (kCppNetConvert)
		{
			tmp = ::ConvertNetToCpp(tmp, true, mIsCppWinrt);
		}
		else
		{
			for (int idx = 0; !kMdExportToManagedSystemAliasReplacements[idx].mReplacementText.IsEmpty(); ++idx)
				tmp.ReplaceAll(kMdExportToManagedSystemAliasReplacements[idx].mMdType.c_str(),
				               kMdExportToManagedSystemAliasReplacements[idx].mReplacementText.c_str(), TRUE);
		}
	}
	WTString returnType(tmp);

	if (-1 != returnType.Find("STDMETHOD") || -1 != returnType.Find("IFACEMETHOD"))
	{
		_ASSERTE(IsCFile(gTypingDevLang));
		if (-1 != returnType.Find("IFACEMETHOD"))
			checkVirtualAndOverride = false; // IFACEMETHOD* has __override in the macro def
		if (-1 != returnType.Find("STDMETHOD") && Psettings->mUseCppOverrideKeyword)
			checkVirtualAndOverride =
			    false; // STDOVERRIDEMETHODIMP* instead of STDMETHODIMP* when override support is enabled

		if (-1 != returnType.Find("STDMETHOD("))
		{
			if (checkVirtualAndOverride)
				returnType = "STDMETHODIMP";
			else
				returnType = "STDOVERRIDEMETHODIMP";
		}
		else if (-1 != returnType.Find("IFACEMETHOD("))
			returnType = "IFACEMETHODIMP";
		else if (-1 != returnType.Find("STDMETHODV("))
		{
			if (checkVirtualAndOverride)
				returnType = "STDMETHODIMPV";
			else
				returnType = "STDOVERRIDEMETHODIMPV";
		}
		else if (-1 != returnType.Find("IFACEMETHODV("))
			returnType = "IFACEMETHODIMPV";
		else
		{
			tmpPos = returnType.Find('(');
			const int tmpPos2 = returnType.Find(',');
			if (-1 != tmpPos && -1 != tmpPos2)
			{
				// #define STDMETHOD_(type,method) type (STDMETHODCALLTYPE * method)
				// #define STDMETHODV_(type,method) type (STDMETHODVCALLTYPE * method)
				//
				// #define IFACEMETHOD_(type,method)   __override STDMETHOD_(type,method)
				// #define IFACEMETHODV_(type,method)  __override STDMETHODV_(type,method)
				WTString retTypeInMacro(returnType.Mid(tmpPos + 1, tmpPos2 - tmpPos - 1));
				retTypeInMacro.Trim();

				if (-1 != returnType.Find("STDMETHOD_("))
				{
					if (checkVirtualAndOverride)
						returnType = "STDMETHODIMP_(" + retTypeInMacro + ")";
					else
						returnType = "STDOVERRIDEMETHODIMP_(" + retTypeInMacro + ")";
				}
				else if (-1 != returnType.Find("IFACEMETHOD_("))
					returnType = "IFACEMETHODIMP_(" + retTypeInMacro + ")";
				else if (-1 != returnType.Find("STDMETHODV_("))
				{
					if (checkVirtualAndOverride)
						returnType = "STDMETHODIMPV_(" + retTypeInMacro + ")";
					else
						returnType = "STDOVERRIDEMETHODIMPV_(" + retTypeInMacro + ")";
				}
				else if (-1 != returnType.Find("IFACEMETHODV_("))
					returnType = "IFACEMETHODIMPV_(" + retTypeInMacro + ")";
			}
		}
	}

	if (checkVirtualAndOverride)
	{
		_ASSERTE(IsCFile(gTypingDevLang));
		// http://msdn.microsoft.com/en-us/library/b0z6b513.aspx
		// if abstract is used, replace it with override regardless of mUseCppOverrideKeyword
		if (qualifiers.ReplaceAll("abstract", "", TRUE) || Psettings->mUseCppOverrideKeyword ||
		    curType.IsDbNet()) // #ToBeFixedCase141418 INCORRECT, override is expected only for "existing" or explicitly
		                       // "abstract" functions
		{
			qualifiers.TrimRight();
			if (!strstrWholeWord(qualifiers, "override"))
			{
				if (qualifiers.GetLength())
					qualifiers += " ";
				qualifiers += "override";
			}
		}

		if (IsCFile(gTypingDevLang))
		{
			bool haveOverride = !!strstrWholeWord(qualifiers, "override");
			if (!haveOverride || Psettings->mUseCppVirtualKeyword)
			{
				if (!strstrWholeWord(tmp, "virtual"))
					returnType.prepend("virtual "); // [case: 54586]
			}
			else if (strstrWholeWord(tmp, "virtual") && !Psettings->mUseCppVirtualKeyword)
			{
				if (returnType.ReplaceAll("virtual", "", TRUE))
					returnType.ReplaceAll("  ", " ", FALSE);
			}
		}
		else if (!strstrWholeWord(tmp, "virtual"))
			returnType.prepend("virtual "); // [case: 54586]

		if (curType.IsDbNet())
		{
			// [case: 67012]
			if (returnType.ReplaceAll("abstract", "", TRUE))
				returnType.ReplaceAll("  ", " ", FALSE);
		}
	}

	if (CS == gTypingDevLang)
	{
		if (C_INTERFACE == baseType.MaskedType())
		{
			// [case: 54585] no virtual in C# and no override if base is interface
			// http://msdn.microsoft.com/en-us/library/ebca9ah3.aspx
			returnType.ReplaceAll("virtual abstract", "", FALSE);
			returnType.ReplaceAll("abstract virtual", "", FALSE);
			returnType.ReplaceAll("virtual", "", TRUE);
			returnType.ReplaceAll("abstract", "", TRUE);
		}
		else
		{
			// [case: 54585] no virtual in C#, use override
			// http://msdn.microsoft.com/en-us/library/ebca9ah3.aspx
			returnType.ReplaceAll("virtual abstract", "override", FALSE);
			returnType.ReplaceAll("abstract virtual", "override", FALSE);
			returnType.ReplaceAll("virtual", "override", TRUE);
			returnType.ReplaceAll("abstract", "override", TRUE);

			// don't use override for interface implementations, only base class overrides
			if (mHaveNonInterfaceVirtualMethods || mHaveNonInterfacePureMethods)
			{
				if (!strstrWholeWord(returnType, "override"))
				{
					if (-1 != returnType.Find("internal"))
						returnType.ReplaceAll("internal", "internal override", TRUE);
					else if (-1 != returnType.Find("public"))
						returnType.ReplaceAll("public", "public override", TRUE);
					else if (-1 != returnType.Find("private"))
						returnType.ReplaceAll("private", "private override", TRUE);
					else if (-1 != returnType.Find("protected"))
						returnType.ReplaceAll("protected", "protected override", TRUE);
					else
						returnType.prepend("override ");
				}
			}
		}
	}

	if (CS == gTypingDevLang || VB == gTypingDevLang)
	{
		if (!strstrWholeWord(returnType, "public", FALSE) && !strstrWholeWord(returnType, "private", FALSE) &&
		    !strstrWholeWord(returnType, "protected", FALSE) && !strstrWholeWord(returnType, "internal"))
		{
			const WTString visStr(::GetVisibilityString(vPublic, gTypingDevLang));
			returnType = visStr + WTString(" ") + returnType;
		}
	}

	WTString autotextItemTitle;
	if (Src == ::GetFileType(mFileInvokedFrom))
		autotextItemTitle = "Refactor Create Implementation";
	else
		autotextItemTitle = "Refactor Extract Method";

	WTString implCode(gAutotextMgr->GetSource(autotextItemTitle.c_str()));
	implCode.ReplaceAll("$SymbolType$", returnType.c_str());
	implCode.ReplaceAll("$SymbolContext$", methodName.c_str());
	implCode.ReplaceAll("$SymbolName$", methodName.c_str());
	implCode.ReplaceAll("$ParameterList$", paramList.c_str());
	if (qualifiers.IsEmpty())
		implCode.ReplaceAll(" $MethodQualifier$", qualifiers.c_str());
	else
		implCode.ReplaceAll("$MethodQualifier$", qualifiers.c_str());

	BOOL isNet = curType.IsDbNet();
	if (!isNet)
	{
		WTString thisTypeDef(mThisType->Def());
		isNet = ::strstrWholeWord(thisTypeDef, "ref") != NULL;
		if (!isNet)
			isNet = ::strstrWholeWord(thisTypeDef, "value") != NULL;
	}

	bool inheritsFromUeUClass = false;
	if (Psettings->mUnrealEngineCppSupport)
	{
		// [case: 116702] [ue4] call "Super::MethodName" when implementing virtual methods from engine base classes
		SemiColonDelimitedString ue4Dirs = GetUe4Dirs();
		for (DType& baseMethod : mMethodDts)
		{
			if (baseMethod.Sym() == curType.Sym())
			{
				// check if the base method is from an unreal engine class
				CStringW baseFile = gFileIdManager->GetFile(baseMethod.FileId());
				ue4Dirs.Reset();
				while (ue4Dirs.HasMoreItems())
				{
					CStringW ue4Dir;
					ue4Dirs.NextItem(ue4Dir);
					if (StartsWithNC((LPCWSTR)baseFile, (LPCWSTR)ue4Dir, FALSE))
					{
						// the file containing the base method comes from a registered unreal engine source location
						inheritsFromUeUClass = true;
						break;
					}
				}
				if (!inheritsFromUeUClass)
				{
					const ProjectVec& baseProjects = GlobalProject->GetProjectForFile(baseMethod.FileId());
					for (const ProjectInfoPtr& baseProject : baseProjects)
					{
						CStringW baseProjectName = Basename(baseProject->GetProjectFile());
						if (strstrWholeWord((const wchar_t*)baseProjectName, L"ue4", FALSE) ||
						    strstrWholeWord((const wchar_t*)baseProjectName, L"ue5", FALSE))
						{
							// the file containing the base method is contained in the ue4 project
							inheritsFromUeUClass = true;
							break;
						}
					}
				}
				if (inheritsFromUeUClass)
				{
					const WTString baseComment = GetCommentForSym(baseMethod.Scope(), false, 0);
					if (baseComment.Find("UCLASS") == -1)
					{
						// the base class is not a uclass
						inheritsFromUeUClass = false;
					}
					else
					{
						break;
					}
				}
			}
		}
	}

	autotextItemTitle = ::GetBodyAutotextItemTitle(isNet, inheritsFromUeUClass);
	implCode.ReplaceAll("$MethodBody$", gAutotextMgr->GetSource(autotextItemTitle.c_str()).c_str());

	// [case: 9863] allow symbol related snippet reserved words in refactoring snippets
	implCode.ReplaceAll("$MethodName$", methodName.c_str());
	WTString methodArgs;
	token2 paramTokens = paramList;
	while (paramTokens.more())
	{
		WTString paramToken = paramTokens.read(',');
		int sepIdx = paramToken.ReverseFind(" ");
		if (sepIdx != -1)
		{
			WTString methodArg = paramToken.Mid(sepIdx);
			methodArg.TrimLeft();
			if (!methodArgs.IsEmpty())
				methodArgs.append(", ");
			methodArgs.append(methodArg.c_str());
		}
	}
	implCode.ReplaceAll("$MethodArgs$", methodArgs.c_str());

	// [case: 9863] remove unsupported symbol related snippet reserved words in refactoring snippets
	implCode.ReplaceAll("$ClassName$", "");
	implCode.ReplaceAll("$BaseClassName$", "");
	implCode.ReplaceAll("$NamespaceName$", "");
	implCode.ReplaceAll("(  )", "()");
	implCode.ReplaceAll("$SymbolPrivileges$", "");

	return implCode;
}

void ImplementMethods::SortMethodList()
{
	for (GenericTreeNodeItem::NodeItems::iterator it = mDlgParams.mNodeItems.begin(); it != mDlgParams.mNodeItems.end();
	     ++it)
	{
		// [case: 58301] sort mChildren by DType lineNumber
		std::sort((*it).mChildren.begin(), (*it).mChildren.end(),
		          [](const GenericTreeNodeItem& n1, const GenericTreeNodeItem& n2) -> bool {
			          // outer 'for' loop assumes single level of child nodes
			          const DType* n1dt = static_cast<DType*>(n1.mData);
			          const DType* n2dt = static_cast<DType*>(n2.mData);
			          return n1dt->Line() < n2dt->Line();
		          });

		if (Psettings->mUnrealEngineCppSupport)
		{
			// [case: 120043] ensure *_implementation methods appear before *_validation methods, this extra sort is
			// needed as *_implementation and *_validation methods share the same line
			for (size_t i = 0; i + 1 < (*it).mChildren.size(); ++i)
			{
				DType* n1dt = static_cast<DType*>((*it).mChildren[i].mData);
				DType* n2dt = static_cast<DType*>((*it).mChildren[i + 1].mData);
				if (n1dt->Line() == n2dt->Line())
				{
					int symEndIdx = n1dt->Sym().Find("_Validate");
					if (symEndIdx != -1)
					{
						WTString symNoPostfix = n1dt->Sym().Mid(0, symEndIdx);
						if (n2dt->Sym() == symNoPostfix + "_Implementation")
						{
							std::swap((*it).mChildren[i], (*it).mChildren[i + 1]);
							++i;
						}
					}
				}
			}
		}
	}
}

void ImplementMethods::ReviewCheckedItems()
{
	int totalCheckedItemCnt = 0;
	int totalActiveBaseNodes = 0;

	for (GenericTreeNodeItem::NodeItems::iterator it = mDlgParams.mNodeItems.begin(); it != mDlgParams.mNodeItems.end();
	     ++it)
	{
		GenericTreeNodeItem& parentNode = *it;
		int curParentcheckedItems = 0;
		int curParentUncheckedItems = 0; // does not include disabled items

		for (GenericTreeNodeItem::NodeItems::iterator it2 = (*it).mChildren.begin(); it2 != (*it).mChildren.end();
		     ++it2)
		{
			GenericTreeNodeItem& curNode = *it2;
			if (!curNode.mEnabled)
			{
				// don't care about disabled items
				continue;
			}

			if (curNode.mChecked)
				++curParentcheckedItems;
			else
				++curParentUncheckedItems;
		}

		totalCheckedItemCnt += curParentcheckedItems;

		if (curParentcheckedItems && !curParentUncheckedItems)
		{
			// check parent node if all enabled items are checked
			parentNode.mChecked = true;
		}

		if (curParentcheckedItems || curParentUncheckedItems)
			++totalActiveBaseNodes; // don't count completely disabled base nodes
	}

	if (totalCheckedItemCnt > 10 || totalActiveBaseNodes > 1)
	{
		// give user a chance to review and cancel if there are more than X
		// checked items or more than 1 interface/base
		mDisplayPrompt = true;

		if (totalCheckedItemCnt > 20)
		{
			// CONSIDER: uncheck every checked item in case VA got lost in bcl?
		}
	}
}
