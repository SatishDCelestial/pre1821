#include "stdafxed.h"
#include "resource.h"
#include "Edcnt.h"
#include "VAParse.h"
#include "ScreenAttributes.h"
#include "DevShellAttributes.h"
#include "ParseThrd.h"
#include "FileTypes.h"
#include "TempSettingOverride.h"
#include "Settings.h"
#include "FontSettings.h"
#include "VaService.h"
#include "FeatureSupport.h"
#include "textmgr2.h"

#if _MSC_VER <= 1200
#include <../src/afximpl.h>
#else
#include <../atlmfc/src/mfc/afximpl.h>
#endif
#include "project.h"
#include "VaMessages.h"
#include "myspell/WTHashList.h"
#include "SpellCheckDlg.h"
#include "VASeException/VASeException.h"
#include "DevShellService.h"
#include "WindowUtils.h"
#include "VACompletionSet.h"
#include "WPF_ViewManager.h"
#include "StringUtils.h"
#include "KeyBindings.h"
#include "DpiCookbook/VsUIDpiHelper.h"
#include "FastBitmapReader.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define VASUG_WIDTH (VsUI::DpiHelper::ImgLogicalToDeviceUnitsX(16))
#define VASUG_HEIGHT (VsUI::DpiHelper::ImgLogicalToDeviceUnitsY(9))
#define VASUG_ARROW_WIDTH (VsUI::DpiHelper::ImgLogicalToDeviceUnitsX(6))
ScreenAttributes g_ScreenAttrs;
const WTString VATomatoTip::sButtonText("VaHoverButton");

// AttrMarkerClient
// ----------------------------------------------------------------------------
//
class AttrMarkerClient : public IVsTextMarkerClient, public IVsTextMarkerClientAdvanced
{
	LONG mRefCount;
	AttrClassPtr mAttr;

  public:
	AttrMarkerClient(AttrClassPtr attr) : mRefCount(0), mAttr(attr)
	{
		_ASSERTE(Psettings->mUseMarkerApi);
	}
	virtual ~AttrMarkerClient() = default;

#pragma region IUnknown
	virtual HRESULT STDMETHODCALLTYPE QueryInterface(
	    /* [in] */ REFIID riid,
	    /* [iid_is][out] */ void** ppvObject)
	{
		if (riid == IID_IUnknown || riid == __uuidof(IVsTextMarkerClient))
		{
			*ppvObject = static_cast<IVsTextMarkerClient*>(this);
			AddRef();
			return S_OK;
		}
		else if (riid == __uuidof(IVsTextMarkerClientAdvanced))
		{
			*ppvObject = static_cast<IVsTextMarkerClientAdvanced*>(this);
			AddRef();
			return S_OK;
		}
		*ppvObject = NULL;
		return E_NOINTERFACE;
	}

	virtual ULONG STDMETHODCALLTYPE AddRef()
	{
		return (ULONG)InterlockedIncrement(&mRefCount);
	}

	virtual ULONG STDMETHODCALLTYPE Release()
	{
		const LONG cRef = InterlockedDecrement(&mRefCount);
		if (cRef == 0)
			delete this;
		return (ULONG)cRef;
	}
#pragma endregion IUnknown

#pragma region IVsTextMarkerClient
	// IVsTextMarkerClient
	virtual void STDMETHODCALLTYPE MarkerInvalidated()
	{
	}

	virtual HRESULT STDMETHODCALLTYPE GetTipText(
	    /* [in] */ IVsTextMarker* /*pMarker*/,
	    /* [out] */ BSTR* /*pbstrText*/)
	{
		return E_NOTIMPL;
	}

	virtual void STDMETHODCALLTYPE OnBufferSave(LPCOLESTR /*pszFileName*/)
	{
	}

	virtual void STDMETHODCALLTYPE OnBeforeBufferClose()
	{
	}

	virtual HRESULT STDMETHODCALLTYPE GetMarkerCommandInfo(
	    /* [in] */ IVsTextMarker* /*pMarker*/,
	    /* [in] */ long /*iItem*/,
	    /* [out] */ BSTR* /*pbstrText*/,
	    /* [out] */ DWORD* /*pcmdf*/)
	{
		return E_NOTIMPL;
	}

	virtual HRESULT STDMETHODCALLTYPE ExecMarkerCommand(
	    /* [in] */ IVsTextMarker* /*pMarker*/,
	    /* [in] */ long /*iItem*/)
	{
		return E_NOTIMPL;
	}

	virtual void STDMETHODCALLTYPE OnAfterSpanReload()
	{
		if (mAttr)
			mAttr->Invalidate();
	}

	virtual HRESULT STDMETHODCALLTYPE OnAfterMarkerChange(
	    /* [in] */ IVsTextMarker* pMarker)
	{
		if (gShellAttr->IsDevenv7())
			InvalidateIfChanged(pMarker);
		return S_OK;
	}
#pragma endregion IVsTextMarkerClient

#pragma region IVsTextMarkerClientAdvanced
	virtual HRESULT STDMETHODCALLTYPE OnMarkerTextChanged(
	    /* [in] */ IVsTextMarker* pMarker)
	{
		InvalidateIfChanged(pMarker);
		return S_OK;
	}
#pragma endregion IVsTextMarkerClientAdvanced

  private:
	void InvalidateIfChanged(IVsTextMarker* pMarker)
	{
		CComQIPtr<IVsTextLineMarker> iTLM(pMarker);
		if (iTLM)
		{
			TextSpan ts;
			iTLM->GetCurrentSpan(&ts);
			CComPtr<IVsTextLines> iLines;
			iTLM->GetLineBuffer(&iLines);
			if (iLines)
			{
				CComBSTR str;
				iLines->GetLineText(ts.iStartLine, ts.iStartIndex, ts.iEndLine, ts.iEndIndex, &str);
				if (str != (LPCWSTR)mAttr->mSym)
				{
					mAttr->Invalidate();
				}
			}
		}
	}
};

// dynamic command ids - dependent upon spell check suggestion count
#define CmdId_AddItem (maxItems)
#define CmdId_IgnoreItem (maxItems + 1)
#define CmdId_MoreItems (maxItems + 2)

class SpellCheckAttrMarkerClient : public AttrMarkerClient
{
  public:
	SpellCheckAttrMarkerClient(WTString typoText, AttrClassPtr attr)
	    : AttrMarkerClient(attr), mTypoText(typoText), mCorrectionListPopulated(false)
	{
	}

#pragma region IUnknown
	virtual HRESULT STDMETHODCALLTYPE QueryInterface(
	    /* [in] */ REFIID riid,
	    /* [iid_is][out] */ void** ppvObject)
	{
		if (riid == IID_IUnknown || riid == __uuidof(IVsTextMarkerClient))
		{
			*ppvObject = static_cast<IVsTextMarkerClient*>(this);
			AddRef();
			return S_OK;
		}
		else if (riid == __uuidof(IVsTextMarkerClientAdvanced))
		{
			*ppvObject = static_cast<IVsTextMarkerClientAdvanced*>(this);
			AddRef();
			return S_OK;
		}
		*ppvObject = NULL;
		return E_NOINTERFACE;
	}
#pragma endregion IUnknown

#pragma region IVsTextMarkerClient
	virtual HRESULT STDMETHODCALLTYPE GetMarkerCommandInfo(
	    /* [in] */ IVsTextMarker* /*pMarker*/,
	    /* [in] */ long iItem,
	    /* [out] */ BSTR* pbstrText,
	    /* [out] */ DWORD* pcmdf)
	{
		if (!g_currentEdCnt)
			return OLECMDERR_E_NOTSUPPORTED;

		switch (iItem)
		{
		case mcvBodyDoubleClickCommand:
		case mcvGlyphDoubleClickCommand:
		case mcvGlyphSingleClickCommand:
			return E_NOTIMPL;
		}

		if (!(pbstrText && pcmdf))
			return S_OK;

		int maxItems = GetCorrectionListCount();
		if (maxItems > MaxCorrectionItems)
			maxItems = MaxCorrectionItems;

		if (CmdId_AddItem == iItem)
		{
			// AppendSeparator();
			CComBSTR wwd(L"&Add word to dictionary");
			pbstrText[0] = wwd.Detach();
		}
		else if (CmdId_IgnoreItem == iItem)
		{
			CComBSTR wwd(L"&Ignore all occurrences");
			pbstrText[0] = wwd.Detach();
		}
		else if (iItem < maxItems)
		{
			const CString mb = mCorrections.GetAt(mCorrections.FindIndex(iItem));
			// [case: 54431]
			const CStringW wdW(::MbcsToWide(mb, mb.GetLength()));
			CComBSTR wdB(wdW);
			pbstrText[0] = wdB.Detach();
		}
		else if (CmdId_MoreItems == iItem)
		{
			if (maxItems < GetCorrectionListCount())
			{
				// open spell check dialog
				CComBSTR wwd(L"&More suggestions...");
				pbstrText[0] = wwd.Detach();
			}
			else
				return OLECMDERR_E_NOTSUPPORTED;
		}
		else
			return OLECMDERR_E_NOTSUPPORTED;

		pcmdf[0] = (DWORD)(OLECMDF_SUPPORTED | OLECMDF_ENABLED);
		return S_OK;
	}

	virtual HRESULT STDMETHODCALLTYPE ExecMarkerCommand(
	    /* [in] */ IVsTextMarker* /*pMarker*/,
	    /* [in] */ long iItem)
	{
		EdCntPtr curEd = g_currentEdCnt;
		if (!curEd)
			return S_OK;

		int maxItems = GetCorrectionListCount();
		if (maxItems > MaxCorrectionItems)
			maxItems = MaxCorrectionItems;

		if (CmdId_AddItem == iItem)
		{
			// add word
			::FPSAddWord(mTypoText.c_str(), FALSE);
			// [case: 15004] clear screen attributes, invalidate and reparse
			curEd->OnModified(TRUE);
			return S_OK;
		}
		else if (CmdId_IgnoreItem == iItem)
		{
			// ignore word
			::FPSAddWord(mTypoText.c_str(), TRUE);
			// [case: 15004] clear screen attributes, invalidate and reparse
			curEd->OnModified(TRUE);
			return S_OK;
		}

		WTString correction;
		if (CmdId_MoreItems == iItem)
			correction = ::SpellWordDlg(mTypoText.c_str());
		else if (iItem >= 0 && iItem < maxItems)
			correction = mCorrections.GetAt(mCorrections.FindIndex(iItem));

		if (!correction.IsEmpty())
		{
			const uint pos = curEd->CurPos();

			const WTString left_word(curEd->WordLeftOfCursor());
			uint left;
			if (!left_word.length() || !::isspace(left_word[left_word.length() - 1]))
				left = curEd->WordPos(BEGWORD, pos);
			else
				left = pos; // don't go to the previous word if space detected

			const WTString right_word(curEd->WordRightOfCursor());
			uint right;
			if (!right_word.length() || !::isspace(right_word[0]))
				right = curEd->WordPos(ENDWORD, pos);
			else
				right = pos; // don't go to the next word if space detected

			curEd->SetSelection((long)left, (long)right);
			curEd->Insert(correction.c_str());
		}

		return S_OK;
	}
#pragma endregion IVsTextMarkerClient

  private:
	int GetCorrectionListCount()
	{
		if (!mCorrectionListPopulated)
		{
			::FPSSpell(mTypoText.c_str(), &mCorrections);
			mCorrectionListPopulated = true;
		}

		return (int)mCorrections.GetSize();
	}

  private:
	// markers get 10 commands; divide up as 3 CmdId_* and up to 7 suggestions
	enum
	{
		MaxCorrectionItems = 7
	};
	WTString mTypoText;
	bool mCorrectionListPopulated;
	CStringList mCorrections;
};

AttrClass::AttrClass(EdCntPtr pEd, const WTString& sym, ULONG line, ULONG xOffset, ScreenAttributeEnum flag,
                     ULONG pos /*= 0*/)
{
	Init(pEd, sym, line, xOffset, flag, pos);
}

AttrClass::AttrClass(EdCntPtr pEd, ScreenAttributeEnum flag, ULONG startPos, ULONG endPos,
                     BlockPositions bp /*= kUtf16Positions*/)
{
	Init(pEd, flag, startPos, endPos, bp);
}

void AttrClass::Init(EdCntPtr pEd, const WTString& sym, ULONG line, ULONG xOffset, ScreenAttributeEnum flag, ULONG pos)
{
	_ASSERTE(pEd);
	_ASSERTE(!((int)line >= kBPMin && (int)line <= kBPMax));

	Invalidate();

	mEd = pEd;
	mLine = line;
	mFlag = flag;
	mPos = pos;
	mEndPos = 0;

	mSymUtf8 = sym;
	mSym = sym.Wide();
	mXOffset = xOffset;
	mXOffsetEnd = xOffset + GetStrWidth(sym.c_str());
}

void AttrClass::Init(EdCntPtr pEd, ScreenAttributeEnum flag, ULONG startPos, ULONG endPos,
                     BlockPositions bp /*= kUtf16Positions*/)
{
	_ASSERTE(pEd);
	_ASSERTE(bp >= kBPMin && bp <= kBPMax);

	Invalidate();

	mEd = pEd;
	mLine = bp;
	mFlag = flag;
	mPos = startPos;
	mEndPos = endPos;

	mSymUtf8 = "";
	mSym = L"";
	mXOffset = 0;
	mXOffsetEnd = 0;
}

AttrClass::~AttrClass()
{
	_ASSERTE(NULL == mMarker && "AttrClass::~AttrClass()");
}

void AttrClass::Invalidate()
{
	if (Psettings && !Psettings->mUseMarkerApi && mFlag != SA_NONE)
		Redraw();

	mFlag = SA_NONE;

	CComPtr<IVsTextLineMarker> markerCopy = mMarker;
	mMarker = NULL;
	if (markerCopy)
	{
		HRESULT res = markerCopy->UnadviseClient();
		res = markerCopy->Invalidate();
	}

	int adornment = mAdornment;
	mAdornment = 0;
	if (adornment)
	{
		if (gVaInteropService)
			gVaInteropService->InvalidateMarker(mEd->m_IVsTextView, adornment);
	}

	mEd = NULL;
}

BOOL AttrClass::IsMatch(EdCntPtr ed, ULONG line, ULONG xOffset, LPCSTR sym)
{
	if (mFlag != SA_NONE && ed && mEd == ed && line == mLine && xOffset >= mXOffset && xOffset < mXOffsetEnd)
	{
		if (sym)
		{
			// this is the only reason to keep mSymUtf8
			if (strncmp(sym, mSymUtf8.c_str(), (uint)mSymUtf8.GetLength()) == 0)
				return TRUE;
		}
		else
		{
			return TRUE;
		}
	}
	return FALSE;
}

void AttrClass::Redraw()
{
	_ASSERTE(!Psettings->mUseMarkerApi);

	if (mEd)
	{
		int y = ((int)mLine - mEd->m_firstVisibleLine) * g_FontSettings->GetCharHeight();
		extern int g_screenXOffset;
		int x = (int)mXOffset + g_screenXOffset;
		CRect rc(x, y, (int)mXOffsetEnd + g_screenXOffset, y + g_FontSettings->GetCharHeight() + 1);
		mEd->InvalidateRect(&rc, FALSE);
	}
}

static bool ShouldCreateVs2010Marker(int flag)
{
	if (gShellAttr->IsDevenv10OrHigher())
	{
		switch (flag)
		{
		case SA_BRACEMATCH:
		case SA_MISBRACEMATCH:
		case SA_REFERENCE:
		case SA_REFERENCE_ASSIGN:
		case SA_REFERENCE_AUTO:
		case SA_REFERENCE_ASSIGN_AUTO:
		case SA_UNDERLINE:
		case SA_UNDERLINE_SPELLING:
		case SA_HASHTAG:
			return true;

		case SA_NONE:
		default:
			break;
		}
	}
	return false;
}

void AttrClass::Display()
{
	if (ShouldCreateVs2010Marker(mFlag))
	{
		if (gVaInteropService && mEd->m_IVsTextView)
		{
			CComPtr<IVsTextView> view(mEd->m_IVsTextView);
			if (view)
			{
				CComPtr<IVsTextLines> lines;
				mEd->m_IVsTextView->GetBuffer(&lines);
				if (lines)
				{
					if (mLine == kUtf16Positions) // positions in UTF16 (guaranteed)
					{
						mAdornment = gVaInteropService->CreateMarker(-1, (int)mPos, int(mEndPos - mPos), mFlag,
						                                             mEd->m_IVsTextView);
					}
					else if (mLine == kPotentiallyMbcsPositions) // positions potentially MBCS
					{
						mPos = (ULONG)mEd->GetBufIndex((int)mPos);
						mEndPos = (ULONG)mEd->GetBufIndex((int)mEndPos);
						mAdornment = gVaInteropService->CreateMarker(-1, (int)mPos, int(mEndPos - mPos), mFlag,
						                                             mEd->m_IVsTextView);
					}
					else // mLine is line number
					{
						// [case: 41798] support for marker creation in presence of MBCS characters by using
						// line numbers (and line relative offsets) rather than offsets from start of file
						// [case: 63166] more mbcs fixes
						uint linePos = (uint)mEd->LinePos((int)mLine);
						long bLine = (long)mEd->GetBufIndex((int)linePos);
						const WTString lineText(mEd->GetLine((int)mLine));
						if (!lineText.IsEmpty())
						{
							long thePos = (long)mPos;
							if (TERISRC(thePos))
								thePos = mEd->GetBufIndex((int)thePos);

							long cOffset = thePos - bLine;
							cOffset = ::ByteOffsetToUtf16ElementOffset(lineText, cOffset);

							// [case: 138734] len must be in utf16 elements
							int length = mSym.GetLength();
							mAdornment =
							    gVaInteropService->CreateMarker((int)mLine, cOffset, length, mFlag, mEd->m_IVsTextView);
						}
					}
				}
			}
		}

		if (!mAdornment)
			mFlag = SA_NONE;
	}
	else if (Psettings->mUseMarkerApi)
	{
		if (mEd->m_IVsTextView)
		{
			CComPtr<IVsTextView> view(mEd->m_IVsTextView);
			if (view)
			{
				CComPtr<IVsTextLines> lines;
				view->GetBuffer(&lines);
				if (lines)
				{
					CreateMarkerMsgStruct mkrMsg;
					mkrMsg.iLines = lines;
					//	mkrMsg.iMarkerType = 0;

					if (mLine == kUtf16Positions) // positions in UTF16 (guaranteed)
					{
						lines->GetLineIndexOfPosition((long)mPos, &mkrMsg.iStartLine, &mkrMsg.iStartIndex);
						lines->GetLineIndexOfPosition((long)mEndPos, &mkrMsg.iEndLine, &mkrMsg.iEndIndex);
					}
					else if (mLine == kPotentiallyMbcsPositions) // positions potentially MBCS
					{
						LONG s_line;
						long s_index;
						mEd->PosToLC((long)mPos, s_line, s_index, gShellAttr->IsMsdev());

						LONG e_line;
						long e_index;
						mEd->PosToLC((long)mEndPos, e_line, e_index, gShellAttr->IsMsdev());

						mkrMsg.iStartLine = s_line - 1;
						mkrMsg.iStartIndex = s_index;
						mkrMsg.iEndLine = e_line - 1;
						mkrMsg.iEndIndex = e_index;
					}
					else // mLine is line number
					{
						int bLine = mEd->GetBufIndex((int)mEd->LinePos((long)mLine));
						const WTString lineText(mEd->GetLine((long)mLine));
						int cOffset = int(mPos - bLine);
						cOffset = ::ByteOffsetToUtf16ElementOffset(lineText, cOffset);

						// [case: 138734] len must be in utf16 elements
						int length = mSym.GetLength();
						mkrMsg.iStartLine = int(mLine - 1);
						mkrMsg.iStartIndex = cOffset;
						mkrMsg.iEndLine = int(mLine - 1);
						mkrMsg.iEndIndex = cOffset + length;
					}

					if (SA_UNDERLINE_SPELLING == mFlag)
						mkrMsg.pClient = new SpellCheckAttrMarkerClient(mSym, mThis.lock());
					else
						mkrMsg.pClient = new AttrMarkerClient(mThis.lock());
					mkrMsg.pClient->AddRef();

					mkrMsg.ppMarker = &mMarker;

					if (VAM_CREATELINEMARKER == mEd->SendMessage(VAM_CREATELINEMARKER, (WPARAM)mFlag, (LPARAM)&mkrMsg))
						mMarker = *mkrMsg.ppMarker;

					mkrMsg.pClient->Release();
				}
			}

			if (!mMarker)
				mFlag = SA_NONE;
		}
	}
	else
	{
		Redraw();
	}
}

VATomatoTip::~VATomatoTip()
{
	// [case: 59385]
	if (m_hWnd && !IsWindow(m_hWnd))
		m_hWnd = NULL;

	if (mToolTipCtrl.m_hWnd && !IsWindow(mToolTipCtrl.m_hWnd))
		mToolTipCtrl.m_hWnd = NULL;

	if (m_layeredParentWnd.m_hWnd && !IsWindow(m_layeredParentWnd.m_hWnd))
		m_layeredParentWnd.m_hWnd = NULL;
}

void VATomatoTip::DoMenu(LPPOINT pt, bool notSymbol)
{
	m_bContextMenuUp = TRUE;
	try
	{
		EdCntPtr curEd = g_currentEdCnt;
		if (m_edPos > 0 && curEd->GetSelString().GetLength() == 0 && !notSymbol)
			curEd->SetPos((uint)m_edPos);
		const WTString cwd = curEd->WordRightOfCursor();
		if (cwd != m_newSymName)
		{
			m_orgSymScope.Empty();
			if (!notSymbol)
				m_newSymName = curEd->WordRightOfCursor();
		}
		if (g_ScreenAttrs.m_VATomatoTip && g_ScreenAttrs.m_VATomatoTip->GetSafeHwnd())
			Dismiss();
		Refactor(UINT_MAX, m_orgSymScope, m_newSymName, pt);
	}
	catch (...)
	{
		VALOGEXCEPTION("SA:");
	}
	m_bContextMenuUp = FALSE;
}

void VATomatoTip::DoMenu(PositionPlacement p)
{
	EdCntPtr curEd = g_currentEdCnt;
	if (!curEd)
		return;

	CPoint pt;
	bool notSymbol = false;
	switch (p)
	{
	case atCursor:
		::GetCursorPos(&pt);
		break;
	case atCaret:
		if (m_hWnd)
			pt = m_dispPt;
		else
		{
			if (gShellAttr->IsDevenv10OrHigher())
			{
				pt = curEd->GetCharPos(int(curEd->CurPos() + 1));
				pt.y += 1; // fixes 2010
			}
			else
				pt = curEd->GetCharPos((int)curEd->CurPos());
		}
		if (!GetDisplayPoint(curEd, pt, atCaret, &notSymbol))
			return;
		break;
	default:
		return;
	}

	DoMenu(&pt, notSymbol);
}

bool EditorIsAtPoint(CPoint pt)
{
	EdCntPtr curEd = g_currentEdCnt;
	if (!curEd || !curEd->GetSafeHwnd())
		return false;

	curEd->vClientToScreen(&pt);
	// see http://blogs.msdn.com/b/oldnewthing/archive/2010/12/30/10110077.aspx
	HWND wnd = WindowFromPoint(pt);
	if (curEd->GetSafeHwnd() == wnd)
		return true;

	if (gShellAttr->IsDevenv10OrHigher() && WPF_ViewManager::Get() && WPF_ViewManager::Get()->HasAggregateFocus())
	{
		static DWORD sVsProcessId = 0;
		if (!sVsProcessId)
		{
			::GetWindowThreadProcessId(MainWndH, &sVsProcessId);
			_ASSERTE(sVsProcessId);
		}

		DWORD wndProcId = 0;
		GetWindowThreadProcessId(wnd, &wndProcId);
		if (!wndProcId || wndProcId == sVsProcessId)
		{
			// [spaghetti] TODO: check ctrl id/atom/prop of wnd and return false if is graph wnd
			return true;
		}
	}
	return false;
}

bool VATomatoTip::CanDisplay()
{
	if (!Psettings)
		return false;
	if (!Psettings->m_enableVA)
		return false;
	if (gShellIsUnloading)
		return false;
	if (g_CompletionSet && g_CompletionSet->IsExpUp(NULL))
		return false; // Don't display over completion list. [case=36743]
	if (VATooltipBlocker::IsBlock())
		return false; // don't display when blocked
	if (Psettings->mMouseClickCmds.get((DWORD)VaMouseCmdBinding::MiddleClick) && (::GetKeyState(VK_MBUTTON) & 0x1000))
		return false; // don't display when middle-click is down and command it required

	return true;
}

bool IsWindowPopup(HWND hWnd)
{
	// fastest first...
	// ignore child windows, dialogs with caption, windows w/o WS_POPUP
	auto style = ::GetWindowLong(hWnd, GWL_STYLE);
	if ((style & (WS_CHILD | WS_CAPTION)) || !(style & WS_POPUP))
		return false;

	// check class name first
	WCHAR className[256] = {0};
	int classNameLen = ::GetClassNameW(hWnd, className, 256);
	if (classNameLen > 0)
	{
		if (StrStrW(className, L"GlowWindow"))
			return false;

		if (StrCmpW(className, L"VsCompletorPane") == 0 || StrStrW(className, L"TipW") ||
		    StrStrIW(className, L"tooltip") || StrStrIW(className, L"popup") || StrStrIW(className, L"dropdown"))
		{
			return true; // known popups
		}
	}

	// check .NET popups
	if (gVaInteropService)
	{
		auto status = gVaInteropService->IsWindowManagedPopup(hWnd);

		if (status < 0)
			return false; // managed, but NOT popup
		else if (status > 0)
			return true; // it is managed popup
	}

	// owner must be enabled
	auto owner = ::GetWindow(hWnd, GW_OWNER);
	return owner && ::IsWindowEnabled(owner);
}

bool VATomatoTip::EnumThreadPopups(std::function<bool(HWND)> func, DWORD threadId /*= 0*/)
{
	if (!func)
		return false;

	auto enumProc = [](HWND hWnd, LPARAM lParam) -> BOOL {
		const auto& fnc = *((std::function<bool(HWND)>*)(lParam));

		// #popupDetection

		// fastest first...
		// ignore child windows, dialogs with caption, windows w/o WS_POPUP
		auto style = ::GetWindowLong(hWnd, GWL_STYLE);
		if ((style & (WS_CHILD | WS_CAPTION)) || !(style & WS_POPUP))
			return TRUE; // TRUE to continue to next window

		// check class name first
		WCHAR className[256] = {0};
		int classNameLen = ::GetClassNameW(hWnd, className, 256);
		if (classNameLen > 0)
		{
			if (StrStrIW(className, L"GlowWindow"))
				return TRUE; // TRUE to continue to next window

			if (StrCmpW(className, L"VsCompletorPane") == 0 || StrStrW(className, L"TipW") ||
			    StrStrIW(className, L"tooltip") || StrStrIW(className, L"popup") || StrStrIW(className, L"dropdown"))
			{
				return fnc(hWnd) ? TRUE : FALSE; // TRUE = continue, FALSE = stop
			}
		}

		// check .NET popups if .NET service exists
		if (gVaInteropService)
		{
			auto status = gVaInteropService->IsWindowManagedPopup(hWnd);

			if (status < 0)                      // it is managed, but not a popup (our GlowForm, for example)
				return TRUE;                     // continue to next window
			else if (status > 0)                 // it is managed popup
				return fnc(hWnd) ? TRUE : FALSE; // TRUE = continue, FALSE = stop
		}

		// owner must be valid window, but not desktop,
		// and must be enabled, otherwise hWnd is modal window
		auto owner = ::GetWindow(hWnd, GW_OWNER);
		if (owner && owner != ::GetDesktopWindow() && ::IsWindowEnabled(owner))
		{
			return fnc(hWnd) ? TRUE : FALSE; // TRUE = continue, FALSE = stop
		}

		return TRUE; // TRUE to continue to next window
	};

	return TRUE == ::EnumThreadWindows(threadId ? threadId : ::GetCurrentThreadId(), enumProc, (LPARAM)&func);
}

bool VATomatoTip::MovePopupsFromRect(const CRect& screenRect, const std::initializer_list<HWND>& ignoreWnd,
                                     DWORD threadId /*= 0*/)
{
	if (screenRect.Width() == 0 || screenRect.Height() == 0)
		return false;

	int midPos = screenRect.left + screenRect.Width() / 2;
	auto getSide = [midPos](HWND wnd, int& mp) {
		CRect r;
		if (::GetWindowRect(wnd, &r))
		{
			mp = r.left + r.Width() / 2;
			return mp < midPos ? -1 : 1;
		}
		mp = 0;
		return 0;
	};

	auto rectsOverlap = [](const CRect& rect1, const CRect& rect2, bool excludeEdges = false) {
		LONG edge = excludeEdges ? 1 : 0;

		LONG left = __max(rect1.left, rect2.left);
		LONG right = __min(rect1.right, rect2.right);

		if (right - left > edge)
		{
			LONG top = __max(rect1.top, rect2.top);
			LONG bottom = __min(rect1.bottom, rect2.bottom);

			return bottom - top > edge;
		}

		return false;
	};

	auto overlapsAny = [&](const CRect& rect, const std::vector<CRect>& rects, bool excludeEdges = false) {
		for (const CRect& r : rects)
			if (rectsOverlap(rect, r, excludeEdges))
				return true;

		return false;
	};

	std::vector<std::tuple<int, HWND>> leftPopups;
	std::vector<std::tuple<int, HWND>> rightPopups;

	EnumThreadPopups(
	    [&](HWND hWnd) {
		    if (!::IsWindowVisible(hWnd))
			    return true;

		    for (HWND iw : ignoreWnd)
		    {
			    if (!iw || !::IsWindow(iw))
				    continue;

			    if (hWnd == iw || ::IsChild(hWnd, iw) || ::IsChild(iw, hWnd))
				    return true;
		    }

		    int mp = 0;
		    auto side = getSide(hWnd, mp);
		    if (side < 0)
			    leftPopups.emplace_back(mp, hWnd);
		    else if (side > 0)
			    rightPopups.emplace_back(mp, hWnd);
		    return true;
	    },
	    threadId);

	std::sort(leftPopups.begin(), leftPopups.end(),
	          [](const std::tuple<int, HWND>& a, const std::tuple<int, HWND>& b) -> bool {
		          return std::get<0>(a) > std::get<0>(b);
	          });

	std::sort(rightPopups.begin(), rightPopups.end(),
	          [](const std::tuple<int, HWND>& a, const std::tuple<int, HWND>& b) -> bool {
		          return std::get<0>(a) < std::get<0>(b);
	          });

	bool retval = false;

	CRect reserved = screenRect;
	reserved.right += VsUI::DpiHelper::LogicalToDeviceUnitsX(5); // append gap
	// Note: left gap is made by lightbulb, it has transparent part on it's right edge

	std::vector<CRect> movedList;
	std::vector<CRect> beforeList;
	bool haveOffset = false;
	int offset = 0;

	for (const auto& tup : leftPopups)
	{
		HWND wnd = std::get<1>(tup);
		CRect wndRect;

		if (!::GetWindowRect(wnd, &wndRect))
			continue;

		if (rectsOverlap(wndRect, reserved) ||
		    (overlapsAny(wndRect, movedList) && !overlapsAny(wndRect, beforeList, true)))
		{
			if (!haveOffset)
			{
				int newX = reserved.left - wndRect.Width();
				offset = newX - wndRect.left;
				haveOffset = true;
				retval = true;
			}

			if (::MoveWindow(wnd, wndRect.left + offset, wndRect.top, wndRect.Width(), wndRect.Height(), FALSE))
			{
				beforeList.push_back(wndRect);
				wndRect.OffsetRect(offset, 0);
				movedList.push_back(wndRect);
			}
		}
	}

	beforeList.clear();
	movedList.clear();
	offset = 0;
	haveOffset = false;
	for (const auto& tup : rightPopups)
	{
		HWND wnd = std::get<1>(tup);
		CRect wndRect;

		if (!::GetWindowRect(wnd, &wndRect))
			continue;

		if (rectsOverlap(wndRect, reserved) ||
		    (overlapsAny(wndRect, movedList) && !overlapsAny(wndRect, beforeList, true)))
		{
			if (!haveOffset)
			{
				int newX = reserved.right;
				offset = newX - wndRect.left;
				haveOffset = true;
				retval = true;
			}

			if (::MoveWindow(wnd, wndRect.left + offset, wndRect.top, wndRect.Width(), wndRect.Height(), FALSE))
			{
				beforeList.push_back(wndRect);
				wndRect.OffsetRect(offset, 0);
				movedList.push_back(wndRect);
			}
		}
	}

	return retval;
}

void VATomatoTip::Display(EdCntPtr curEd, CPoint pt)
{
	if (gVaInteropService && Psettings->mCodeInspection) // VS2010+ uses vaclang
		return;
	if (!CanDisplay())
		return;
	if (!Psettings->mDisplayRefactoringButton && !Psettings->mAutoDisplayRefactoringButton)
		return;
	if (!EditorIsAtPoint(pt))
		return; // [case: 39402] don't display over non-IDE wnds
	if (!curEd)
		return;

	auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(*curEd);

	CPoint mpt;
	GetCursorPos(&mpt);
	curEd->vScreenToClient(&mpt);
	m_dispPt = pt;
	dx = (uint)(abs(m_dispPt.x - mpt.x) + abs(m_dispPt.y - mpt.y));
	pt.y += VsUI::DpiHelper::LogicalToDeviceUnitsY(2);
	pt.x += VsUI::DpiHelper::LogicalToDeviceUnitsX(1);
	if (!m_hWnd)
	{
		m_hasFocus = false;

		CRect r(pt.x, pt.y, pt.x + VASUG_WIDTH, pt.y + VASUG_HEIGHT);
		m_hasTip = HasVsNetPopup(FALSE);

		if (m_hasTip && gShellAttr && gShellAttr->IsDevenv10OrHigher())
		{
			m_dispPt.x -= VASUG_WIDTH;
			dx = uint(abs(m_dispPt.x - mpt.x) + abs(m_dispPt.y - mpt.y));
			r.left = pt.x - VASUG_WIDTH;        // Left of sym
			r.right = pt.x + VASUG_ARROW_WIDTH; // include -^ arrow
		}
		m_dispPt = r.TopLeft();

		// Ensure w/in client rect
		CRect rc;
		curEd->vGetClientRect(&rc);
		if (!rc.PtInRect(m_dispPt))
			return;

		curEd->vClientToScreen(&r);

		m_layeredParentWnd.CreateEx(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, "STATIC", sButtonText.c_str(),
		                            WS_POPUP /*| WS_VISIBLE*/, r, GetFocus(), 0);
		// WinXP WS_EX_LAYERED fix - From VACompletionSet
		::SetClassLong(
		    m_layeredParentWnd.m_hWnd, GCL_STYLE,
		    long(::GetClassLong(m_layeredParentWnd.m_hWnd, GCL_STYLE) & ~(CS_OWNDC | CS_CLASSDC | CS_PARENTDC)));
		::SetWindowLong(m_layeredParentWnd.m_hWnd, GWL_EXSTYLE,
		                ::GetWindowLong(m_layeredParentWnd.m_hWnd, GWL_EXSTYLE) | WS_EX_LAYERED);

		// potential problem on win10 fall creators update. see:
		// https://www.unknowncheats.me/forum/1782126-post9.html
		::SetLayeredWindowAttributes(m_layeredParentWnd.m_hWnd, RGB(0, 255, 0), 100, LWA_ALPHA | LWA_COLORKEY);

		r.SetRect(0, 0, r.Width(), r.Height());
		Create(sButtonText.c_str(), WS_CHILD | BS_BITMAP | BS_FLAT | BS_OWNERDRAW, r, &m_layeredParentWnd, 0);

		LoadBitmaps(IDB_TOMATO_TIP, IDB_TOMATO_TIP, IDB_TOMATO_TIP, IDB_TOMATO_TIP);
		BITMAP bm = {0};
		m_bitmap.GetBitmap(&bm);
		CSize orig_size = {bm.bmWidth, bm.bmHeight};
		VsUI::DpiHelper::LogicalToDeviceUnits(m_bitmap);
		VsUI::DpiHelper::LogicalToDeviceUnits(m_bitmapSel);
		VsUI::DpiHelper::LogicalToDeviceUnits(m_bitmapFocus);
		VsUI::DpiHelper::LogicalToDeviceUnits(m_bitmapDisabled);
		bm = {0};
		m_bitmap.GetBitmap(&bm);
		CSize scaled_size = {bm.bmWidth, bm.bmHeight};

		if (orig_size != scaled_size)
		{
			// do special colorkey processing only if the bitmap was scaled
			std::vector<CPoint> transparent;
			{
				// make nearest neighbor scaling is ugly, but won't touch our green colour
				CBitmap bitmap;
				bitmap.LoadBitmap(MAKEINTRESOURCE(IDB_TOMATO_TIP));
				VsUI::DpiHelper::LogicalToDeviceUnits(bitmap, VsUI::ImageScalingMode::NearestNeighbor);

				// go through the bitmap and store transparent pixels
				CDC dc;
				dc.CreateCompatibleDC(nullptr);
				dc.SelectObject(&bitmap);
				CFastBitmapReader fastReader(dc.GetSafeHdc(), bitmap);
				for (int y = 0; y < scaled_size.cy; y++)
					for (int x = 0; x < scaled_size.cx; x++)
						if (fastReader.FastGetPixel(x, y) == RGB(0, 255, 0))
							transparent.emplace_back(x, y);
			}
			for (auto bitmap : std::vector<CBitmap*>{&m_bitmap, &m_bitmapSel, &m_bitmapFocus, &m_bitmapDisabled})
			{
				// put transparent green color on its place in nicely scaled bitmaps
				CDC dc;
				dc.CreateCompatibleDC(nullptr);
				dc.SelectObject(bitmap);
				for (const auto& p : transparent)
					dc.SetPixelV(p, RGB(0, 255, 0));
			}
		}

		mToolTipCtrl.Create(this);
		mToolTipCtrl.Activate(TRUE);
		::mySetProp(mToolTipCtrl.m_hWnd, "__VA_do_not_colour", (HANDLE)1);
		CToolInfo ti;
		ZeroMemory(&ti, sizeof(CToolInfo));
		ti.cbSize = sizeof(TOOLINFO);
		ti.hwnd = m_hWnd;
		ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
		ti.uId = (UINT_PTR)m_hWnd;

		static char sRefactoringMenuTooltipText[255];
		const WTString binding = ::GetBindingTip("VAssistX.RefactorContextMenu", "Alt+Shift+Q");
		sprintf(sRefactoringMenuTooltipText, "Display refactoring menu...%s", binding.c_str());
		ti.lpszText = sRefactoringMenuTooltipText;

		mToolTipCtrl.SendMessage(TTM_ADDTOOL, 0, (LPARAM)&ti);
	}

	CRect myRect;
	m_layeredParentWnd.GetWindowRect(&myRect);
	MovePopupsFromRect(myRect, {m_hWnd, m_layeredParentWnd.m_hWnd, mToolTipCtrl.m_hWnd}, g_mainThread);

	ShowWindow(SW_SHOWNOACTIVATE);
	m_layeredParentWnd.ShowWindow(SW_SHOWNOACTIVATE);
}

// returns CharHeight less line spacing fudge factor
static int GetFudgedCharHeight()
{
	if (gShellAttr->IsDevenv10OrHigher())
		return g_FontSettings->GetCharHeight() -
		       VsUI::DpiHelper::LogicalToDeviceUnitsY(1); // vs2010 line height is different than prev versions?
	if (gShellAttr->IsDevenv())
		return g_FontSettings->GetCharHeight() -
		       VsUI::DpiHelper::LogicalToDeviceUnitsY(5); // VSNet has more space between lines
	return g_FontSettings->GetCharHeight() - VsUI::DpiHelper::LogicalToDeviceUnitsY(3);
}

void VATomatoTip::Display(EdCntPtr ed, int index)
{
	if (!m_bContextMenuUp && index != m_edPos && ed)
	{
		Dismiss();
		CPoint pt = ed->GetCharPos(index);
		// Same placement logic as GetDisplayPoint()
		pt.y += ::GetFudgedCharHeight();
		if (g_ScreenAttrs.m_VATomatoTip)
			g_ScreenAttrs.m_VATomatoTip->Display(ed, pt);
		m_hasFocus = TRUE;
		m_edPos = index;
	}
}
#define MAXDELTA 10
void VATomatoTip::OnMouseMove(UINT nFlags, CPoint point)
{
	if (m_hWnd)
	{
		if (HasVsNetPopup(FALSE) && !m_hasTip)
		{
			long p = m_edPos;
			m_edPos = 0;
			Display(g_currentEdCnt, p);
			return;
		}
		ULONG dist = ULONG(abs(m_dispPt.x - point.x) + abs(m_dispPt.y - point.y));
		if (m_IsSuggestionTip)
		{
			if (dist < dx)
				dx = dist;
			else if (Psettings->mAutoDisplayRefactoringButton && Psettings->mDisplayRefactoringButton)
			{
				if (dist > (dx + VsUI::DpiHelper::LogicalToDeviceUnitsX(MAXDELTA * 4)))
					Dismiss(); // [case: 41985] dismiss when both options are enabled
			}
		}
		else if (dist < dx)
			dx = dist;
		else if (dist >
		         (dx + VsUI::DpiHelper::LogicalToDeviceUnitsX(MAXDELTA))) // Make it disappear if moving moving away.
			Dismiss();
	}
}

void VATomatoTip::OnHover(EdCntPtr ed, CPoint pt)
{
	if (!Psettings->m_enableVA || !Psettings->mDisplayRefactoringButton)
		return;
	if (gVaInteropService && Psettings->mCodeInspection)
		return; // use code inspections instead

	if (gShellAttr->IsDevenv10OrHigher() && WPF_ViewManager::Get() && !WPF_ViewManager::Get()->HasAggregateFocus())
	{
		// NOTE: not sure if HasAggregateFocus() check should be in HasFocus()? Leaving to prevent potential breakage
		// -Jerry
		return; // Don't display when not active, happens when menu's are active. case=39044
	}

	if (GetDisplayPoint(ed, pt, atCursor))
		Display(ed, pt);
}

bool VATomatoTip::GetDisplayPoint(EdCntPtr ed, CPoint& pt, PositionPlacement at, bool* notSymbol /*= nullptr*/)
{
	if (notSymbol)
		*notSymbol = false;
	if (ed->m_hasSel)
	{
		const WTString sel = ed->GetSelString();
		if (sel.GetLength())
		{
			if (!m_hasFocus && ::GetCursor() != afxData.hcurArrow)
				return FALSE;

			if (atCursor == at)
			{
				GetCursorPos(&pt);
				ed->vScreenToClient(&pt);
			}
			else
			{
				pt = ed->GetCharPos((long)ed->CurPos());
				ed->vClientToScreen(&pt);
			}

			_ASSERTE(g_FontSettings->GetCharHeight() && "$$MSD3");
			pt.y = g_FontSettings->GetCharHeight() *
			       (VsUI::DpiHelper::LogicalToDeviceUnitsY(1) + (pt.y / g_FontSettings->GetCharHeight()));
			pt.x -= VsUI::DpiHelper::LogicalToDeviceUnitsX(20);

			if (atCaret == at)
				pt.y += VASUG_HEIGHT;

			// don't call ShouldDisplay when there is a selection
			return true;
		}
		return false;
	}

	if (!m_hWnd)
	{
		const WTString buf = ed->GetBuf();
		if (buf.IsEmpty())
			return false;

		//#ifdef _DEBUG
		//		const LPCTSTR dbgBuf = buf.c_str();
		//#endif // _DEBUG
		bool resolveVc6Tabs = false;
		if (gShellAttr->IsMsdev() && at == atCaret)
			resolveVc6Tabs = true;
		uint p1 = (uint)ed->GetBufIndex(buf, ed->CharFromPos(&pt, resolveVc6Tabs));
		if (!p1)
			return false;
		if (atCaret == at)
		{
			// [case: 6820] allow menu at start of symbol
			if (!ISCSYM(buf[p1 - 1]) && !ISCSYM(buf[p1]))
			{
				if (notSymbol)
					*notSymbol = true;
				// return false; // case 80803: show the menu anyways - let the method run till the next return true; to
				// set up the coordinates for the menu
			}
		}
		else if (!ISCSYM(buf[p1 - 1]))
			return false;

		for (m_edPos = (int)p1; m_edPos && ISCSYM(buf[(uint)m_edPos - 1]);)
			m_edPos--;

		if (atCaret == at || ShouldDisplay(ed, buf))
		{
			pt = ed->GetCharPos(m_edPos);
			pt.y += ::GetFudgedCharHeight();

			if (atCaret == at)
			{
				pt.y += VASUG_HEIGHT;
				ed->vClientToScreen(&pt);
			}
			return true;
		}
	}

	return false;
}

LRESULT
VATomatoTip::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_SHOWWINDOW: {
		if (wParam)
			SetTimer(TID_MovePopups, 10, nullptr);
		else
			KillTimer(TID_MovePopups);
		break;
	}
	case WM_TIMER: {
		if (wParam == TID_MovePopups)
		{
			CRect myRect;
			m_layeredParentWnd.GetWindowRect(&myRect);
			MovePopupsFromRect(myRect, {m_hWnd, m_layeredParentWnd.m_hWnd, mToolTipCtrl.m_hWnd}, g_mainThread);
		}
		break;
	}
	case WM_DESTROY:
		KillTimer(TID_MovePopups);
		break;
	case WM_SETCURSOR:
		if (::EditorIsAtPoint(CPoint(m_dispPt.x, m_dispPt.y - 3)))
		{
			// potential problem on win10 fall creators update. see:
			// https://www.unknowncheats.me/forum/1782126-post9.html
			::SetLayeredWindowAttributes(m_layeredParentWnd.m_hWnd, RGB(0, 255, 0), 255, LWA_ALPHA | LWA_COLORKEY);
			m_hasFocus = true;
		}
		else
			Dismiss();
		break;
	case WM_MOUSEACTIVATE:
		return MA_NOACTIVATEANDEAT;
	case WM_RBUTTONUP:
	case WM_LBUTTONUP:
		DoMenu(atCursor);
		return TRUE;
	}

	return __super::WindowProc(message, wParam, lParam);
}

void VATomatoTip::Dismiss()
{
	if (m_hWnd)
	{
		DestroyWindow();
		m_layeredParentWnd.DestroyWindow();
		mToolTipCtrl.DestroyWindow();
	}
	m_IsSuggestionTip = false;
	m_hasFocus = false;
	m_hWnd = NULL;
	m_edPos = 0;
}

bool VATomatoTip::ShouldDisplayAtCursor(EdCntPtr ed)
{
	CPoint pt;
	::GetCursorPos(&pt);
	ed->vScreenToClient(&pt);
	return GetDisplayPoint(ed, pt, atCursor);
}

// This method is based on EdCnt::Scope but doesn't change caret pos.
// Returns false if scope is string or comment.
bool VATomatoTip::ShouldDisplay(EdCntPtr ed, const WTString& edBuf)
{
	//	static const EdCnt *sLastEd = NULL;
	// 	static long sLastPos = -1;
	static bool sLastResult = false;
	// Removed for now because it does not save much time and caused bug where:
	// Hovering over a symbol before all threads finished cashed lastresult = 0
	// So, we needed to hover over another symbol before being able to hover over current word again
	// 	if (sLastPos == m_edPos && sLastEd == ed)
	// 		return sLastResult;

	_ASSERTE(ed && edBuf.c_str());

	//	sLastEd = ed.get();
	// 	sLastPos = m_edPos;
	sLastResult = false;
	MultiParsePtr mp(ed->GetParseDb());

	if (GlobalProject->IsBusy() || !IsFeatureSupported(Feature_Refactoring, ed->m_ScopeLangType) ||
	    mp->GetFilename().IsEmpty() || mp->GetFilename() != ed->FileName() ||
	    (g_ParserThread && g_ParserThread->IsNormalJobActiveOrPending()))
		return sLastResult;

	uint pos = (uint)m_edPos;
	if (pos && ed->CharAt(pos - 1) == '/' && strchr("/*", ed->CharAt(pos)))
		pos++; // get the * or / of the // or /*

	if (g_currentEdCnt == ed) // make sure this is still the active window
	{
		const WTString scope = QuickScope(edBuf, (int)pos, ed->m_ftype);
		if (scope != "String" && scope != "CommentLine")
			sLastResult = true;
	}

	return sLastResult;
}

void VATomatoTip::DisplayTipContextMenu(bool atMousePos)
{
	if (gShellIsUnloading)
		return;

	if (atMousePos)
	{
		DoMenu(atCursor);
		return;
	}

	m_hasFocus = true; // To force extract method menu

	EdCntPtr ed = g_currentEdCnt;
	ScopeInfoPtr si = ed ? ed->ScopeInfoPtr() : nullptr;
	if (Psettings && Psettings->mAutoDisplayRefactoringButton && si && si->m_lastErrorPos &&
	    !(::IsWindow(m_hWnd) && IsWindowVisible()))
		Display(ed, si->m_lastErrorPos); // Redisplay in the case the binding (alt+...) made it dismiss.

	if (::IsWindow(m_hWnd) && IsWindowVisible())
	{
		ed->vClientToScreen(&m_dispPt);
		DoMenu(&m_dispPt);
	}
	else
		DoMenu(atCaret);
}

void VATomatoTip::UpdateQuickInfoState(BOOL hasQuickInfo)
{
	if (m_hWnd && hasQuickInfo)
	{
		// Update refactor tip to include --^ arrow
		long p = m_edPos;
		m_edPos = 0;
		if (g_currentEdCnt)
			Display(g_currentEdCnt, p);
	}
}

void VATomatoTip::DisplaySuggestionTip(EdCntPtr ed, int index)
{
	if (gVaInteropService && Psettings->mCodeInspection)
	{
		// vs2010+
		gVaInteropService->SuggestQuickAction(index);
	}
	else
	{
		m_IsSuggestionTip = true;
		Display(ed, index);
	}
}

void ScreenAttributes::DoAddDisplayAttribute(EdCntPtr ed, WTString expectedStr, long pos, ScreenAttributeEnum flag,
                                             bool queue)
{
	// get column of error
	if (!ed)
		return;

	if (SA_UNDERLINE == flag || SA_UNDERLINE_SPELLING == flag)
	{
		// see ExtTextOutDType in VAParseTextOut.h
		// case 15054: only underline modified files
		if (!ed->modified_ever || Psettings->m_bSupressUnderlines)
			return;

		if (SA_UNDERLINE == flag)
		{
			// see ExtTextOutDType in VAParseTextOut.h
			// Not in CS/VB, case: 509
			// Not JS or HTML, case: 15552
			// No typo underlines at all except in Src and Header
			if (!(ed->m_ftype == Src || ed->m_ftype == Header))
				return;

			if (!Psettings->m_underlineTypos)
				return;
		}
		else if (!Psettings->m_spellFlags)
			return;
	}

	WTString bb(ed->GetBuf());
	LPCSTR buf = bb.c_str();
	pos = ed->GetBufIndex(bb, pos);

	// backup pos to start of symbol
	if (ISCSYM(buf[pos]))
		while (ISCSYM(buf[pos - 1]))
			pos--;
	LPCSTR p = &buf[pos];

	// backup p to start of line
	for (; p > buf && !strchr("\r\n", p[-1]); p--)
		;
	int GetColumnOffset(LPCSTR line, int pos);
	uint xOffset = (uint)GetColumnOffset(p, ptr_sub__int(&buf[pos], p));

	// find end of symbol
	int len = 0;
	for (; ISCSYM(buf[pos + len]) || (flag == SA_UNDERLINE_SPELLING && buf[pos + len] == '\'') ||
	       (flag == SA_HASHTAG &&
	        ((len == 0 && buf[pos] == '#') || (buf[pos + len] == '-' && Psettings->mHashtagsAllowHypens)));
	     len++)
	{
	}

	// [case: 116759] prevent apostrophe(s) at the end of the symbol from being considered
	while (len && buf[pos + len - 1] == '\'')
		--len;

	WTString sym(&buf[pos], len);
	if (!len && buf[pos] && !wt_isspace(buf[pos]))
		sym = buf[pos];

	if (!sym.GetLength())
	{
		if (SA_BRACEMATCH == flag || SA_MISBRACEMATCH == flag)
			InvalidateBraces();
		return;
	}
	if (expectedStr.GetLength())
	{
		if (g_doDBCase)
		{
			if (expectedStr != sym)
				return;
		}
		else
		{
			if (expectedStr.CompareNoCase(sym))
				return;
		}
	}

	uint line = (uint)ed->CurLine((uint)pos);

	if (SA_BRACEMATCH == flag || SA_MISBRACEMATCH == flag)
	{
		// [case: 34166] don't do brace highlights during box selection
		if (gShellSvc->HasBlockModeSelection(ed.get()))
		{
			InvalidateBraces();
			return;
		}
	}

	if (queue)
	{
		AutoLockCs l(mVecLock);
		_ASSERTE(flag == SA_UNDERLINE || flag == SA_UNDERLINE_SPELLING || flag == SA_REFERENCE_AUTO ||
		         flag == SA_REFERENCE_ASSIGN_AUTO || flag == SA_HASHTAG);
		AttrClassPtr attr(new AttrClass(ed, sym, line, xOffset, flag, (uint)pos));
		attr->SetThis(attr);
		// don't display
		if (SA_REFERENCE_ASSIGN_AUTO == flag || SA_REFERENCE_AUTO == flag)
			m_queuedAutoRefVec.push_back(attr);
		else if (SA_HASHTAG == flag)
			m_queuedHashtagsVec.push_back(attr);
		else
			m_queuedUlVec.push_back(attr);
	}
	else
	{
		switch (flag)
		{
		case SA_BRACEMATCH:
		case SA_MISBRACEMATCH: {
			if (strchr("({[<", sym[0]))
			{
				m_brc1->Init(ed, sym, line, xOffset, flag, (uint)pos);
				m_brc1->Display();
			}
			else if (strchr(")}]>", sym[0]))
			{
				m_brc2->Init(ed, sym, line, xOffset, flag, (uint)pos);
				m_brc2->Display();
			}
			else
			{
				_ASSERTE(0 && "??? ScreenAttributes::DoAddDisplayAttribute");
				InvalidateBraces();
			}
		}
		break;

		case SA_UNDERLINE:
		case SA_UNDERLINE_SPELLING:
		case SA_REFERENCE_AUTO:
		case SA_REFERENCE_ASSIGN_AUTO:
		case SA_HASHTAG:
			_ASSERTE(0 && "These should always be queued");
			break;

		case SA_REFERENCE:
		case SA_REFERENCE_ASSIGN: {
			AutoLockCs l(mVecLock);
			AttrClassPtr attr(new AttrClass(ed, sym, line, xOffset, flag, (uint)pos));
			attr->SetThis(attr);
			attr->Display();
			m_refs.push_back(attr);
		}
		break;

		default:
			break;
		}
	}
}

AttrClassPtr ScreenAttributes::AttrFromPoint(EdCntPtr ed, int x, int y, LPCSTR sym)
{
	if (!ed)
		return NULL;
	if (!g_FontSettings->CharSizeOk())
		return NULL;

	ULONG ln = (ULONG)ed->m_firstVisibleLine;
	ln += y / g_FontSettings->GetCharHeight();
	extern int g_screenXOffset;
	ULONG xOffset = ULONG(x - g_screenXOffset);

	if (m_brc1->IsMatch(ed, ln, xOffset, sym))
		return m_brc1;

	if (m_brc2->IsMatch(ed, ln, xOffset, sym))
		return m_brc2;

	for (AttrVec::iterator iter = m_ulVect.begin(); iter != m_ulVect.end(); ++iter)
	{
		AttrClassPtr attr = *iter;
		if (attr->IsMatch(ed, ln, xOffset, sym))
			return attr;
	}

	for (AttrVec::iterator iter = m_AutoRefs.begin(); iter != m_AutoRefs.end(); ++iter)
	{
		AttrClassPtr attr = *iter;
		if (attr->IsMatch(ed, ln, xOffset, sym))
			return attr;
	}

	for (auto attr : m_hashtagsVec)
	{
		if (attr->IsMatch(ed, ln, xOffset, sym))
			return attr;
	}

	return NULL;
}

void ScreenAttributes::InsertText(ULONG line, ULONG pos, LPCSTR txt)
{
	if (!Psettings->mUseMarkerApi)
	{
		AutoLockCs l(mVecLock);

		if (strchr("\r\n", txt[0]))
		{
			m_brc2->mLine += 1;
			for (AttrVec::iterator iter = m_ulVect.begin(); iter != m_ulVect.end(); ++iter)
			{
				AttrClassPtr attr = *iter;
				if (attr->mPos > pos)
					attr->mLine++;
			}

			for (AttrVec::iterator iter = m_AutoRefs.begin(); iter != m_AutoRefs.end(); ++iter)
			{
				AttrClassPtr attr = *iter;
				if (attr->mPos > pos)
					attr->mLine++;
			}

			for (auto attr : m_hashtagsVec)
			{
				if (attr->mPos > pos)
					attr->mLine++;
			}
		}
		else
		{
			int width = GetStrWidth(txt);

			if (m_brc2->mLine == line)
				m_brc2->mXOffset += width;
			for (AttrVec::iterator iter = m_ulVect.begin(); iter != m_ulVect.end(); ++iter)
			{
				AttrClassPtr attr = *iter;
				if (attr->mLine == line && attr->mPos > pos)
					attr->mXOffset += width;
			}

			for (AttrVec::iterator iter = m_AutoRefs.begin(); iter != m_AutoRefs.end(); ++iter)
			{
				AttrClassPtr attr = *iter;
				if (attr->mLine == line && attr->mPos > pos)
					attr->mXOffset += width;
			}

			for (auto attr : m_hashtagsVec)
			{
				if (attr->mLine == line && attr->mPos > pos)
					attr->mXOffset += width;
			}
		}

		InvalidateVec(m_queuedUlVec);
		InvalidateVec(m_queuedAutoRefVec);
		InvalidateVec(m_queuedHashtagsVec);
	}
}

void ScreenAttributes::AddDisplayAttribute(EdCntPtr ed, WTString str, long pos, ScreenAttributeEnum flag)
{
	DoAddDisplayAttribute(ed, str, pos, flag, false);
}

void ScreenAttributes::QueueDisplayAttribute(EdCntPtr ed, WTString str, long pos, ScreenAttributeEnum flag)
{
	DoAddDisplayAttribute(ed, str, pos, flag, true);
}

void ScreenAttributes::ProcessQueue_Underlines()
{
	AutoLockCs l(mVecLock);

	// invalidate old items
	InvalidateVec(m_ulVect);

	// display queued items and move them to other list
	AttrVec tmp;
	tmp.swap(m_queuedUlVec);
	for (AttrVec::iterator iter = tmp.begin(); iter != tmp.end(); ++iter)
	{
		AttrClassPtr attr = *iter;
		attr->Display();
		m_ulVect.push_back(attr);
	}
	tmp.clear();
}

void ScreenAttributes::ProcessQueue_AutoReferences()
{
	AutoLockCs l(mVecLock);
	mProcessAutoRefQueue = true;

	// invalidate old items
	InvalidateVec(m_AutoRefs);

	// display queued items and move them to other list
	AttrVec tmp;
	tmp.swap(m_queuedAutoRefVec);
	for (AttrVec::iterator iter = tmp.begin(); iter != tmp.end(); ++iter)
	{
		AttrClassPtr attr = *iter;
		if (mProcessAutoRefQueue)
			attr->Display();
		m_AutoRefs.push_back(attr);
	}

	tmp.clear();
}

void ScreenAttributes::ProcessQueue_Hashtags()
{
	AutoLockCs l(mVecLock);

	// invalidate old items
	InvalidateVec(m_hashtagsVec);

	// display queued items and move them to other list
	AttrVec tmp;
	tmp.swap(m_queuedHashtagsVec);
	for (auto attr : tmp)
	{
		attr->Display();
		m_hashtagsVec.push_back(attr);
	}
	tmp.clear();
}

void ScreenAttributes::Invalidate(ScreenAttributeEnum flag)
{
	switch (flag)
	{
	case SA_BRACEMATCH:
	case SA_MISBRACEMATCH:
		if (m_brc1->mFlag == flag)
			m_brc1->Invalidate();
		if (m_brc2->mFlag == flag)
			m_brc2->Invalidate();
		break;

	case SA_UNDERLINE:
	case SA_UNDERLINE_SPELLING:
		InvalidateVec(m_ulVect, flag);
		break;

	case SA_REFERENCE_AUTO:
	case SA_REFERENCE_ASSIGN_AUTO:
		InvalidateVec(m_AutoRefs, flag);
		break;

	case SA_REFERENCE:
	case SA_REFERENCE_ASSIGN:
		InvalidateVec(m_refs, flag);
		break;

	case SA_HASHTAG:
		InvalidateVec(m_hashtagsVec, flag);
		break;

	case SA_NONE:
	default:
		break;
	}
}

void ScreenAttributes::Invalidate()
{
	InvalidateBraces();
	InvalidateVec(m_ulVect);
	InvalidateVec(m_AutoRefs);
	InvalidateVec(m_hashtagsVec);
	InvalidateVec(m_queuedUlVec);
	InvalidateVec(m_queuedAutoRefVec);
	InvalidateVec(m_queuedHashtagsVec);
	InvalidateVec(m_refs);

	if (m_VATomatoTip)
	{
		m_VATomatoTip->Dismiss();

		if (gShellIsUnloading)
			m_VATomatoTip = nullptr;
	}
}

void ScreenAttributes::OptionsUpdated()
{
	if (!Psettings)
		return;

	if (!Psettings->m_enableVA)
	{
		Invalidate();
		return;
	}

	if (!Psettings->boldBraceMatch)
		Invalidate(SA_BRACEMATCH);

	if (!Psettings->m_braceMismatches)
		Invalidate(SA_MISBRACEMATCH);

	if (!Psettings->mAutoHighlightRefs)
	{
		Invalidate(SA_REFERENCE_AUTO);
		Invalidate(SA_REFERENCE_ASSIGN_AUTO);
	}

	if (!Psettings->mHighlightFindReferencesByDefault)
	{
		Invalidate(SA_REFERENCE);
		Invalidate(SA_REFERENCE_ASSIGN);
	}

	if (!Psettings->m_underlineTypos || Psettings->m_bSupressUnderlines)
		Invalidate(SA_UNDERLINE);

	if (!Psettings->m_spellFlags || Psettings->m_bSupressUnderlines)
		Invalidate(SA_UNDERLINE_SPELLING);
}

void ScreenAttributes::InvalidateVec(AttrVec& vec)
{
	AttrVec tmp;

	{
		AutoLockCs l(mVecLock);
		tmp.swap(vec);
	}

	for (AttrVec::iterator iter = tmp.begin(); iter != tmp.end(); ++iter)
	{
		AttrClassPtr attr = *iter;
		attr->Invalidate();
	}
	tmp.clear();
}

void ScreenAttributes::InvalidateVec(AttrVec& vec, ScreenAttributeEnum flag)
{
	AutoLockCs l(mVecLock);
	AttrVec tmp;
	tmp.swap(vec);

	for (AttrVec::iterator iter = tmp.begin(); iter != tmp.end();)
	{
		AttrClassPtr attr = *iter;
		if (attr->mFlag == flag)
		{
			attr->Invalidate();
			tmp.erase(iter++);
		}
		else
		{
			++iter;
		}
	}

	vec.swap(tmp);
}

void ScreenAttributes::InvalidateBraces()
{
	m_brc1->Invalidate();
	m_brc2->Invalidate();
}

ScreenAttributes::~ScreenAttributes()
{
	_ASSERTE(m_brc1->mFlag == SA_NONE);
	_ASSERTE(m_brc2->mFlag == SA_NONE);
	_ASSERTE(m_queuedUlVec.empty());
	_ASSERTE(m_queuedAutoRefVec.empty());
	_ASSERTE(m_queuedHashtagsVec.empty());
	_ASSERTE(m_ulVect.empty());
	_ASSERTE(m_AutoRefs.empty());
	_ASSERTE(m_hashtagsVec.empty());
	_ASSERTE(m_refs.empty());
}

WTString ScreenAttributes::GetSummary() const
{
	WTString sum("Queued Markers:\r\n"), tmp;
	AutoLockCs l(mVecLock);
	AttrVec::const_iterator iter;

	for (iter = m_queuedUlVec.begin(); iter != m_queuedUlVec.end(); ++iter)
	{
		tmp.WTFormat("  %d  %s\r\n", (*iter)->mFlag, (*iter)->mSymUtf8.c_str());
		sum += tmp;
	}

	for (iter = m_queuedAutoRefVec.begin(); iter != m_queuedAutoRefVec.end(); ++iter)
	{
		tmp.WTFormat("  %d  %s\r\n", (*iter)->mFlag, (*iter)->mSymUtf8.c_str());
		sum += tmp;
	}

	sum += "Find References Highlights:\r\n";
	for (iter = m_refs.begin(); iter != m_refs.end(); ++iter)
	{
		tmp.WTFormat("  %d  %s\r\n", (*iter)->mFlag, (*iter)->mSymUtf8.c_str());
		sum += tmp;
	}

	sum += "Underlines:\r\n";
	for (iter = m_ulVect.begin(); iter != m_ulVect.end(); ++iter)
	{
		tmp.WTFormat("  %d  %s\r\n", (*iter)->mFlag, (*iter)->mSymUtf8.c_str());
		sum += tmp;
	}

	sum += "Auto Reference Highlights:\r\n";
	for (iter = m_AutoRefs.begin(); iter != m_AutoRefs.end(); ++iter)
	{
		tmp.WTFormat("  %d  %s\r\n", (*iter)->mFlag, (*iter)->mSymUtf8.c_str());
		sum += tmp;
	}

	return sum;
}

WTString ScreenAttributes::GetBraceSummary() const
{
	WTString sum, tmp;
	AutoLockCs l(mVecLock);
	AttrVec::const_iterator iter;

	// don't log mXOffset since it is font dependent

	sum += "Brace1:\r\n";
	if (m_brc1)
	{
		tmp.WTFormat("%d %ld %ld %s\r\n", m_brc1->mFlag, m_brc1->mLine, m_brc1->mPos, m_brc1->mSymUtf8.c_str());
		sum += tmp;
	}

	sum += "Brace2:\r\n";
	if (m_brc2)
	{
		tmp.WTFormat("%d %ld %ld %s\r\n", m_brc2->mFlag, m_brc2->mLine, m_brc2->mPos, m_brc2->mSymUtf8.c_str());
		sum += tmp;
	}

	return sum;
}

unsigned int VATooltipBlocker::blocks = 0;

VATooltipBlocker::VATooltipBlocker()
{
	InterlockedIncrement(&blocks);
}

VATooltipBlocker::~VATooltipBlocker()
{
	InterlockedDecrement(&blocks);
}
