
#include "StdAfxEd.h"
#include "GenericTreeDlg.h"
#include "VAAutomation.h"
#include "MenuXP\Tools.h"
#include "WindowUtils.h"
#include "ColorSyncManager.h"
#include "ImageListManager.h"
#include <stack>
#include "Expansion.h"
#include "TextOutDc.h"
#include "MenuXP\Draw.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#ifndef TVIS_EX_DISABLED
#define TVIS_EX_DISABLED 0x0002
#endif

GenericTreeDlgParams::GenericTreeDlgParams()
    : mImgList(ImageListManager::bgOsWindow), mListCanBeColored(true), mExtended(false), mFirstCtlExId(2000)
{
}

GenericTreeExtraCtl& ExtraCtlContainer::AddControl(UINT row, GenericTreeExtraCtl::ctl_type type)
{
	std::vector<GenericTreeExtraCtl>& extra_ctls = ExtraCtls[row];
	extra_ctls.push_back(GenericTreeExtraCtl());
	return extra_ctls.back().SetType(type);
}

GenericTreeExtraCtl& ExtraCtlContainer::AddCheckBox(UINT row, LPCSTR text,
                                                    std::function<void(CThemedCheckBox&)> on_click)
{
	GenericTreeExtraCtl& ctl = AddControl(row, GenericTreeExtraCtl::CheckBox).SetText(text);

	if (on_click)
	{
		ctl.SetOnChange([on_click](CWnd* wnd) {
			CThemedCheckBox* chb = static_cast<CThemedCheckBox*>(wnd);
			on_click(*chb);
		});
	}

	return ctl;
}

GenericTreeExtraCtl& ExtraCtlContainer::AddCheckBox(UINT row, LPCSTR text, chb_func on_click, chb_func on_create)
{
	GenericTreeExtraCtl& ctl = AddCheckBox(row, text, on_click);
	if (on_create)
	{
		ctl.SetOnCreate([on_create](CWnd* wnd) {
			CThemedCheckBox* chb = static_cast<CThemedCheckBox*>(wnd);
			on_create(*chb);
		});
	}
	return ctl;
}

GenericTreeExtraCtl& ExtraCtlContainer::AddButton(UINT row, LPCSTR text, std::function<void(CThemedButton&)> on_click)
{
	GenericTreeExtraCtl& ctl = AddControl(row, GenericTreeExtraCtl::PushBtn).SetText(text);

	if (on_click)
	{
		ctl.SetOnChange([on_click](CWnd* wnd) {
			CThemedButton* chb = static_cast<CThemedButton*>(wnd);
			on_click(*chb);
		});
	}

	return ctl;
}

GenericTreeExtraCtl& ExtraCtlContainer::AddButton(UINT row, LPCSTR text, btn_func on_click, btn_func on_create)
{
	GenericTreeExtraCtl& ctl = AddButton(row, text, on_click);
	if (on_create)
	{
		ctl.SetOnCreate([on_create](CWnd* wnd) {
			CThemedButton* chb = static_cast<CThemedButton*>(wnd);
			on_create(*chb);
		});
	}
	return ctl;
}

GenericTreeExtraCtl& ExtraCtlContainer::AddEdit(UINT row, LPCSTR text,
                                                std::function<void(CThemedEdit&)> on_text_changed)
{
	GenericTreeExtraCtl& ctl = AddControl(row, GenericTreeExtraCtl::EditCtl).SetText(text);

	if (on_text_changed)
	{
		ctl.SetOnChange([on_text_changed](CWnd* wnd) {
			CThemedEdit* chb = static_cast<CThemedEdit*>(wnd);
			on_text_changed(*chb);
		});
	}

	return ctl;
}

GenericTreeExtraCtl& ExtraCtlContainer::AddEdit(UINT row, LPCSTR text, ed_func on_text_changed, ed_func on_create)
{
	GenericTreeExtraCtl& ctl = AddEdit(row, text, on_text_changed);
	if (on_create)
	{
		ctl.SetOnCreate([on_create](CWnd* wnd) {
			CThemedEdit* chb = static_cast<CThemedEdit*>(wnd);
			on_create(*chb);
		});
	}
	return ctl;
}
GenericTreeExtraCtl& ExtraCtlContainer::AddDimEdit(UINT row, LPCSTR dimText,
                                                   std::function<void(CThemedDimEdit&)> on_text_changed)
{
	GenericTreeExtraCtl& ctl = AddControl(row, GenericTreeExtraCtl::DimEditCtl).SetText(dimText);

	if (on_text_changed)
	{
		ctl.SetOnChange([on_text_changed](CWnd* wnd) {
			CThemedDimEdit* chb = static_cast<CThemedDimEdit*>(wnd);
			on_text_changed(*chb);
		});
	}

	return ctl;
}

GenericTreeExtraCtl& ExtraCtlContainer::AddDimEdit(UINT row, LPCSTR dimText, ded_func on_text_changed,
                                                   ded_func on_create)
{
	GenericTreeExtraCtl& ctl = AddDimEdit(row, dimText, on_text_changed);
	if (on_create)
	{
		ctl.SetOnCreate([on_create](CWnd* wnd) {
			CThemedDimEdit* chb = static_cast<CThemedDimEdit*>(wnd);
			on_create(*chb);
		});
	}
	return ctl;
}

GenericTreeExtraCtl& ExtraCtlContainer::AddLabel(UINT row, LPCSTR text)
{
	return AddControl(row, GenericTreeExtraCtl::Label).SetText(text);
}

GenericTreeExtraCtl& ExtraCtlContainer::AddComboBox(UINT row)
{
	return AddControl(row, GenericTreeExtraCtl::EditComboBox);
}

IMPLEMENT_DYNAMIC(GenericTreeDlg, VADialog)

GenericTreeDlg::GenericTreeDlg(GenericTreeDlgParams& params, CWnd* parent)
    : CThemedVADlg(GenericTreeDlg::IDD, parent), mParams(params)
{
	mSavedView[0] = 0;
	mSavedView[1] = 0;
}

GenericTreeDlg::~GenericTreeDlg()
{
}

void GenericTreeDlg::DoDataExchange(CDataExchange* pDX)
{
	VADialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_DIRECTIONS_LABEL, m_tree_label);
	DDX_Control(pDX, IDC_TREE, m_tree);
}

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(GenericTreeDlg, CThemedVADlg)
ON_MENUXP_MESSAGES()
ON_WM_CONTEXTMENU()
END_MESSAGE_MAP()
#pragma warning(pop)

const LPCSTR classNames[]{"", WC_STATICA, WC_BUTTONA, WC_BUTTONA, WC_BUTTONA, WC_EDITA, WC_EDITA, WC_COMBOBOXEXA};
const LPCWSTR classNamesW[]{L"", WC_STATICW, WC_BUTTONW, WC_BUTTONW, WC_BUTTONW, WC_EDITW, WC_EDITW, WC_COMBOBOXEXW};

static void HandleWndFuncs(std::vector<GenericTreeExtraCtl::CWndFnc>& fnc_list, GenericTreeExtraCtl& ctl)
{
	std::vector<GenericTreeExtraCtl::CWndFnc> fncs;

	// Function may want to modify current list of functors,
	// to allow that, we make a copy of valid functors
	// and then we invoke them from external array.
	for (auto& fnc : fnc_list)
		if (fnc)
			fncs.push_back(fnc);

	for (auto& fnc : fncs)
		fnc(ctl.Wnd);
}

template <typename TWnd>
TWnd* AddCtlWide(CThemedVADlg* dlg, GenericTreeExtraCtl& xctl, int X, int Y, int Width, int Height, UINT ID)
{
	std::shared_ptr<TWnd> wnd(new TWnd());

	bool is_edit = xctl.CtlType == GenericTreeExtraCtl::EditCtl || xctl.CtlType == GenericTreeExtraCtl::DimEditCtl;
	bool is_label = xctl.CtlType == GenericTreeExtraCtl::Label;
	bool is_dropDown = xctl.CtlType == GenericTreeExtraCtl::DropDownComboBox;
	bool is_cbox = is_dropDown || xctl.CtlType == GenericTreeExtraCtl::EditComboBox;

	UINT notify_code = UINT(is_edit ? EN_CHANGE : BN_CLICKED);
	LPCWSTR class_name = classNamesW[xctl.CtlType];
	CStringW caption = xctl.TextW ? *xctl.TextW : CStringW();

	DWORD ex_style = 0;
	DWORD style = WS_CHILD | WS_VISIBLE | (is_label ? 0u : WS_TABSTOP);
	if (xctl.CtlType == GenericTreeExtraCtl::CheckBox)
		style |= BS_AUTOCHECKBOX;
	else if (xctl.CtlType == GenericTreeExtraCtl::RadioBtn)
		style |= BS_AUTORADIOBUTTON;
	else if (is_edit)
	{
		style |= ES_AUTOHSCROLL;
		ex_style |= WS_EX_CLIENTEDGE;
	}
	else if (is_dropDown)
		style |= CBS_DROPDOWN | WS_VSCROLL;
	else if (is_cbox)
		style |= CBS_DROPDOWN | WS_VSCROLL;

	HWND hWnd = nullptr;
	// unable to test this loop -- found no UI that exercises this method
	for (int cnt = 0; cnt < 10; ++cnt)
	{
		hWnd = ::CreateWindowExW(ex_style, class_name, caption, style, X, Y, Width, Height, dlg->m_hWnd,
		                         (HMENU)(UINT_PTR)ID, 0, 0);

		if (hWnd != nullptr)
			break;

		// add retry for [case: 111864]
		DWORD err = GetLastError();
		vLog("WARN: AddCtlWide call to CreateWindowEx failed, 0x%08lx\n", err);
		Sleep(100u + (cnt * 50u));
	}

	::mySetProp(hWnd, "__VA_do_not_colour", (HANDLE)1);
	::mySetProp(hWnd, VA_GenericTreeExCtlProperty, (HANDLE)&xctl);

	if (is_cbox)
	{
		CThemedComboBox* cb = (CThemedComboBox*)wnd.get();
		cb->SubclassWindowW(hWnd);
		cb->Init();
	}
	else
	{
		wnd->SubclassWindowW(hWnd);
	}

	wnd->SetFontType(VaFontType::EnvironmentFont);

	xctl.Wnd = wnd.get();
	HandleWndFuncs(xctl.OnCreate, xctl);

	// Note: this function is always needed (even for Static),
	//		 because it holds shared ptr w/ our window.
	dlg->WndProcEvents.AddCommandHandler(notify_code, ID, [wnd, &xctl]() -> bool {
		(void)wnd;
		HandleWndFuncs(xctl.OnChange, xctl);
		return false;
	});
	return wnd.get();
}

template <typename TWnd>
TWnd* AddCtl(CThemedVADlg* dlg, GenericTreeExtraCtl& xctl, int X, int Y, int Width, int Height, UINT ID)
{
	if (xctl.TextW || xctl.CtlType == GenericTreeExtraCtl::EditComboBox)
		return AddCtlWide<TWnd>(dlg, xctl, X, Y, Width, Height, ID);

	bool is_edit = xctl.CtlType == GenericTreeExtraCtl::EditCtl || xctl.CtlType == GenericTreeExtraCtl::DimEditCtl;
	bool is_label = xctl.CtlType == GenericTreeExtraCtl::Label;
	bool is_dropDown = xctl.CtlType == GenericTreeExtraCtl::DropDownComboBox;
	bool is_cbox = is_dropDown || xctl.CtlType == GenericTreeExtraCtl::EditComboBox;

	std::shared_ptr<TWnd> wnd(new TWnd());
	LPCSTR class_name = classNames[xctl.CtlType];

	LPCSTR caption = xctl.TextA ? (*xctl.TextA).c_str() : "";
	RECT rect = {X, Y, X + Width, Y + Height};

	std::shared_ptr<CDC> dc(dlg->GetWindowDC(), [dlg](CDC* dc) { dlg->ReleaseDC(dc); });

	DWORD ex_style = 0;
	DWORD style = WS_CHILD | WS_VISIBLE | (is_label ? 0u : WS_TABSTOP);
	if (xctl.CtlType == GenericTreeExtraCtl::CheckBox)
		style |= BS_AUTOCHECKBOX;
	else if (xctl.CtlType == GenericTreeExtraCtl::RadioBtn)
		style |= BS_AUTORADIOBUTTON;
	else if (is_edit)
	{
		style |= ES_AUTOHSCROLL;
		ex_style |= WS_EX_CLIENTEDGE;
	}
	else if (is_dropDown)
		style |= CBS_DROPDOWNLIST | WS_VSCROLL;
	else if (is_cbox)
		style |= CBS_DROPDOWN | WS_VSCROLL;

	if (xctl.CtlType == GenericTreeExtraCtl::DimEditCtl)
	{
		for (int cnt = 0; cnt < 10; ++cnt)
		{
			if (wnd->CreateEx(ex_style, class_name, "", style, X, Y, Width, Height, dlg->m_hWnd, (HMENU)(UINT_PTR)ID))
				break;

			// add retry for [case: 111864]
			vLog("WARN: AddCtl call to CreateEx 1 failed\n");
			Sleep(100u + (cnt * 50u));
		}

		CThemedDimEdit* dimEdit = (CThemedDimEdit*)wnd.get();
		dimEdit->SetDimText(caption);
	}
	else
	{
		for (int cnt = 0; cnt < 10; ++cnt)
		{
			if (wnd->CreateEx(ex_style, class_name, caption, style, X, Y, Width, Height, dlg->m_hWnd,
			                  (HMENU)(UINT_PTR)ID))
				break;

			// add retry for [case: 111864]
			vLog("WARN: AddCtl call to CreateEx 2 failed\n");
			Sleep(100u + (cnt * 50u));
		}
	}

	wnd->SetFontType(VaFontType::EnvironmentFont);

	::mySetProp(wnd->m_hWnd, "__VA_do_not_colour", (HANDLE)1);
	::mySetProp(wnd->m_hWnd, VA_GenericTreeExCtlProperty, (HANDLE)&xctl);

	xctl.Wnd = wnd.get();
	HandleWndFuncs(xctl.OnCreate, xctl);

	UINT notify_code = UINT(is_edit ? EN_CHANGE : BN_CLICKED);

	// Note: this function is always needed (even for Static),
	//		 because it holds shared ptr w/ our window.
	dlg->WndProcEvents.AddCommandHandler(notify_code, ID, [wnd, &xctl]() -> bool {
		(void)wnd;
		HandleWndFuncs(xctl.OnChange, xctl);
		return false;
	});

	return wnd.get();
}

int XPixelsFromDLU(HWND hWnd, int val)
{
	RECT r = {};
	r.right = val;
	::MapDialogRect(hWnd, &r);
	return r.right;
}

int YPixelsFromDLU(HWND hWnd, int val)
{
	RECT r = {};
	r.bottom = val;
	::MapDialogRect(hWnd, &r);
	return r.bottom;
}

int DLUSpacing::XToPix(HWND hWnd, int value)
{
	return XPixelsFromDLU(hWnd, value);
}

int DLUSpacing::YToPix(HWND hWnd, int value)
{
	return YPixelsFromDLU(hWnd, value);
}

int DefaultDLUHeight(GenericTreeExtraCtl::ctl_type type)
{
	switch (type)
	{
	case GenericTreeExtraCtl::Spacer: // fall-through
	case GenericTreeExtraCtl::Label:
		return DLUHeight::LabelSpace;
	case GenericTreeExtraCtl::CheckBox: // fall-through
	case GenericTreeExtraCtl::RadioBtn:
		return DLUHeight::CheckRadio;
	case GenericTreeExtraCtl::PushBtn:    // fall-through
	case GenericTreeExtraCtl::EditCtl:    // fall-through
	case GenericTreeExtraCtl::DimEditCtl: // fall-through
	case GenericTreeExtraCtl::EditComboBox:
		return DLUHeight::EditButton;
	default:
		return DLUHeight::LabelSpace;
	}
}

void GenericTreeDlg::ExtendedInit()
{
	{
		// This block is here to resize directions
		// of label to fit only really needed space.

		// Counting of lines in text
		WTString& str = mParams.mDirectionsText;
		int str_len = str.GetLength();
		int numlines = 1;
		for (int i = 0; i < str_len; i++)
		{
			if (str[i] == '\r' && i + 1 < str_len && str[i + 1] == '\n')
			{
				numlines++;
				i++;
			}
		}

		// Calculating of new rectangle and moving label
		CRect rect;
		GetClientRect(&rect);
		rect.DeflateRect(XPixelsFromDLU(m_hWnd, DLUSpacing::DlgMargins),
		                 YPixelsFromDLU(m_hWnd, DLUSpacing::DlgMargins));
		rect.bottom = rect.top + numlines * YPixelsFromDLU(m_hWnd, DLUHeight::LabelSpace);
		m_tree_label.SetWindowPos(NULL, rect.left, rect.top, rect.Width(), rect.Height(), SWP_NOZORDER);
	}

	// gaps between rows of related and unrelated controls
	int related_gap = YPixelsFromDLU(m_hWnd, DLUSpacing::RelatedGap);
	int unrelated_gap = YPixelsFromDLU(m_hWnd, DLUSpacing::UnrelatedGap);

	// find top start position
	int TopStart;
	{
		CRect exts_Rect;
		m_tree_label.GetWindowRect(&exts_Rect);
		ScreenToClient(&exts_Rect);
		TopStart = exts_Rect.bottom + related_gap;
	}

	// rectangle of tree control is used as reference for new rows
	CRect rect;
	m_tree.GetWindowRect(&rect);
	ScreenToClient(&rect);
	CRect rect_orig = rect;

	rect.top = TopStart;
	rect.bottom -= unrelated_gap - related_gap; // OK and Cancel are unrelated to tree.

	UINT id = mParams.mFirstCtlExId;

	// preprocessing of control definitions
	for (auto& row : mParams.ExtraCtls)
	{
		// Translate dialog units of
		// controls not placed to any row.
		if (row.first >= RowOrder_None)
		{
			for (auto& ctl : row.second)
			{
				_ASSERTE(ctl.PlacementRelative == false);

				if (ctl.Placement.Height() == 0)
					ctl.Placement.bottom += DefaultDLUHeight(ctl.CtlType);

				CRect pl_rect = ctl.Placement;
				MapDialogRect(&pl_rect);

				if (rect.top < TopStart + pl_rect.bottom + related_gap)
					rect.top = TopStart + pl_rect.bottom + related_gap;
			}
		}

		// assign CtlID to controls with CtlIDAuto
		for (auto& ctl : row.second)
			if (ctl.CtlID == GenericTreeExtraCtl::CtlIDAuto)
				ctl.CtlID = id++;
	}

	// create extra controls
	for (auto& row : mParams.ExtraCtls)
	{

		// this adds all controls with no row order
		// are placed directly to specific location
		if (row.first >= RowOrder_None)
		{
			for (auto& ctl : row.second)
			{
				_ASSERTE(ctl.PlacementRelative == false);

				CRect rect2 = ctl.Placement;
				MapDialogRect(&rect2);
				rect2.OffsetRect(0, TopStart);

				switch (ctl.CtlType)
				{
				case GenericTreeExtraCtl::Label:
					AddCtl<CThemedStaticNormal>(this, ctl, rect2.left, rect2.top, rect2.Width(), rect2.Height(),
					                            ctl.CtlID);
					break;
				case GenericTreeExtraCtl::CheckBox:
					AddCtl<CThemedCheckBox>(this, ctl, rect2.left, rect2.top, rect2.Width(), rect2.Height(), ctl.CtlID);
					break;
				case GenericTreeExtraCtl::RadioBtn:
					AddCtl<CThemedRadioButton>(this, ctl, rect2.left, rect2.top, rect2.Width(), rect2.Height(),
					                           ctl.CtlID);
					break;
				case GenericTreeExtraCtl::PushBtn:
					AddCtl<CThemedButton>(this, ctl, rect2.left, rect2.top, rect2.Width(), rect2.Height(), ctl.CtlID);
					break;
				case GenericTreeExtraCtl::EditCtl:
					AddCtl<CThemedEdit>(this, ctl, rect2.left, rect2.top, rect2.Width(), rect2.Height(), ctl.CtlID);
					break;
				case GenericTreeExtraCtl::DimEditCtl:
					AddCtl<CThemedDimEdit>(this, ctl, rect2.left, rect2.top, rect2.Width(), rect2.Height(), ctl.CtlID);
					break;
				case GenericTreeExtraCtl::EditComboBox:
					AddCtlWide<CThemedComboBox>(this, ctl, rect2.left, rect2.top, rect2.Width(), rect2.Height(),
					                            ctl.CtlID);
					break;
				default:
					break;
				}
			}

			continue;
		}

		int row_height = 0;

		// calculation of row height
		for (const auto& ctl : row.second)
		{
			int curr_height = ctl.Placement.Height() ? ctl.Placement.Height() : DefaultDLUHeight(ctl.CtlType);

			if (curr_height > row_height)
				row_height = curr_height;
		}

		row_height = YPixelsFromDLU(m_hWnd, row_height);

		// creating controls for current row
		for (auto& ctl : row.second)
		{
			CRect wr;

			// relative placement is a percentage representation
			// of space between label from top, OK/Cancel buttons
			// from bottom and standard dialog left and right margins.
			if (!ctl.PlacementRelative)
			{
				wr.left = XPixelsFromDLU(m_hWnd, ctl.Placement.left);
				wr.right = XPixelsFromDLU(m_hWnd, ctl.Placement.right);

				if (ctl.Placement.Height())
				{
					wr.top = YPixelsFromDLU(m_hWnd, ctl.Placement.top);
					wr.bottom = YPixelsFromDLU(m_hWnd, ctl.Placement.bottom);
				}
				else
				{
					wr.top = 0;
					wr.bottom = YPixelsFromDLU(m_hWnd, DefaultDLUHeight(ctl.CtlType));
				}
			}
			else
			{
				// placement is defined by direct DLU values
				CRect init_rect(CPoint(), m_szInitial);

				init_rect.DeflateRect(XPixelsFromDLU(m_hWnd, DLUSpacing::DlgMargins),
				                      YPixelsFromDLU(m_hWnd, DLUSpacing::DlgMargins));

				wr.left = init_rect.left + (int)(((double)ctl.Placement.left / 100.0) * (double)init_rect.Width());
				wr.right = init_rect.left + (int)(((double)ctl.Placement.right / 100.0) * (double)init_rect.Width());

				if (ctl.Placement.Height())
				{
					wr.top = (int)(((double)ctl.Placement.top / 100.0) * (double)init_rect.Height());
					wr.bottom = (int)(((double)ctl.Placement.bottom / 100.0) * (double)init_rect.Height());
				}
				else
				{
					wr.top = 0;
					wr.bottom = YPixelsFromDLU(m_hWnd, DefaultDLUHeight(ctl.CtlType));
				}
			}

			// bottom to top = between buttons and tree
			// top to bottom = between label and tree
			if (row.first >= RowOrder_BottomToTop)
				wr.OffsetRect(0, rect.bottom - wr.Height());
			else
				wr.OffsetRect(0, rect.top);

			// control creation
			switch (ctl.CtlType)
			{
			case GenericTreeExtraCtl::Label: {
				int top_offset = YPixelsFromDLU(m_hWnd, DLUSpacing::LabelOffset);

				if (row_height - (wr.bottom - wr.top) < top_offset)
					top_offset = (row_height - (wr.bottom - wr.top)) / 2;

				wr.top += top_offset;
				wr.bottom += top_offset;

				ctl.Wnd = AddCtl<CThemedStaticNormal>(this, ctl, wr.left, wr.top, wr.right - wr.left,
				                                      wr.bottom - wr.top, ctl.CtlID);
				break;
			}
			case GenericTreeExtraCtl::CheckBox:
				ctl.Wnd = AddCtl<CThemedCheckBox>(this, ctl, wr.left, wr.top, wr.right - wr.left, wr.bottom - wr.top,
				                                  ctl.CtlID);
				break;
			case GenericTreeExtraCtl::RadioBtn:
				ctl.Wnd = AddCtl<CThemedRadioButton>(this, ctl, wr.left, wr.top, wr.right - wr.left, wr.bottom - wr.top,
				                                     ctl.CtlID);
				break;
			case GenericTreeExtraCtl::PushBtn:
				ctl.Wnd = AddCtl<CThemedButton>(this, ctl, wr.left, wr.top, wr.right - wr.left, wr.bottom - wr.top,
				                                ctl.CtlID);
				break;
			case GenericTreeExtraCtl::EditCtl:
				ctl.Wnd =
				    AddCtl<CThemedEdit>(this, ctl, wr.left, wr.top, wr.right - wr.left, wr.bottom - wr.top, ctl.CtlID);
				break;
			case GenericTreeExtraCtl::DimEditCtl:
				ctl.Wnd = AddCtl<CThemedDimEdit>(this, ctl, wr.left, wr.top, wr.right - wr.left, wr.bottom - wr.top,
				                                 ctl.CtlID);
				break;
			case GenericTreeExtraCtl::EditComboBox:
				ctl.Wnd = AddCtlWide<CThemedComboBox>(this, ctl, wr.left, wr.top, wr.right - wr.left,
				                                      wr.bottom - wr.top, ctl.CtlID);
				break;
			default:
				break;
			}
		}

		if (row.first >= RowOrder_BottomToTop)
			rect.bottom -= row_height + related_gap;
		else
			rect.top += row_height + related_gap;
	}

	// setting new position of tree
	m_tree.MoveWindow(rect);

	// fix tab stop order
	ThemeUtils::ReorderChildren(m_hWnd);

	// AFTER controls reordering, we can add dynamics.
	// If we would add it during creating of controls,
	// Z order would be changed back during layout.
	for (auto& row : mParams.ExtraCtls)
	{
		for (auto& ctl : row.second)
		{
			// set dynamics to controls
			if (ctl.Wnd && (ctl.Dynamics.left || ctl.Dynamics.top || ctl.Dynamics.right || ctl.Dynamics.bottom))
			{
				AddSzControl(ctl.Wnd->m_hWnd, (SBYTE)ctl.Dynamics.left, (SBYTE)ctl.Dynamics.top,
				             (SBYTE)ctl.Dynamics.right, (SBYTE)ctl.Dynamics.bottom);
			}
		}
	}
}

void GenericTreeDlg::OnInitMenuPopup(CMenu* pPopupMenu, UINT nIndex, BOOL bSysMenu)
{
	__super::OnInitMenuPopup(pPopupMenu, nIndex, bSysMenu);
	CMenuXP::SetXPLookNFeel(this, pPopupMenu->m_hMenu, CMenuXP::GetXPLookNFeel(this));
}

void GenericTreeDlg::OnMeasureItem(int, LPMEASUREITEMSTRUCT lpMeasureItemStruct)
{
	CMenuXP::OnMeasureItem(lpMeasureItemStruct);
}

void GenericTreeDlg::OnDrawItem(int id, LPDRAWITEMSTRUCT dis)
{
	auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(m_hWnd);

	if (CMenuXP::OnDrawItem(dis, m_hWnd))
		return;

	if (id != IDC_CLOSE)
	{
		__super::OnDrawItem(id, dis);
		return;
	}
}

LRESULT
GenericTreeDlg::OnMenuChar(UINT nChar, UINT nFlags, CMenu* pMenu)
{
	if (CMenuXP::IsOwnerDrawn(pMenu->m_hMenu))
	{
		return CMenuXP::OnMenuChar(pMenu->m_hMenu, nChar, nFlags);
	}
	return __super::OnMenuChar(nChar, nFlags, pMenu);
}


BOOL GenericTreeDlg::OnInitDialog()
{
	__super::OnInitDialog();

	::SetWindowTextW(m_tree_label.GetSafeHwnd(), mParams.mDirectionsText.Wide());

	if (mParams.mExtended || !mParams.ExtraCtls.empty())
		ExtendedInit();

	AddSzControl(m_tree_label, mdResize, mdNone);
	AddSzControl(m_tree, mdResize, mdResize);
	AddSzControl(IDOK, mdRepos, mdRepos);
	AddSzControl(IDCANCEL, mdRepos, mdRepos);

	m_tree.on_check_toggle = std::bind(&GenericTreeDlg::OnCheckToggle, this, std::placeholders::_1);

	if (mParams.mImgList != ImageListManager::bgNone)
	{
		gImgListMgr->SetImgListForDPI(m_tree, mParams.mImgList, TVSIL_NORMAL);
	}
	else
	{
		// http://stackoverflow.com/questions/1289519/why-arent-my-ctreectrl-checkboxes-checking
		m_tree.ModifyStyle(TVS_CHECKBOXES, 0, 0);
		m_tree.ModifyStyle(0, TVS_CHECKBOXES, 0);
	}

	if (mParams.mNodeItems.size() > 1)
	{
		// [case: 81760]
		m_tree.ModifyStyle(0, TVS_HASBUTTONS | TVS_LINESATROOT, 0);
	}

	if (CVS2010Colours::IsExtendedThemeActive())
	{
		Theme.AddDlgItemForDefaultTheming(IDC_DIRECTIONS_LABEL);
		Theme.AddThemedSubclasserForDlgItem<CThemedButton>(IDOK, this);
		Theme.AddThemedSubclasserForDlgItem<CThemedButton>(IDCANCEL, this);
		ThemeUtils::ApplyThemeInWindows(TRUE, m_hWnd);
	}

	_ASSERTE(!mParams.mCaption.IsEmpty());
	SetWindowText(mParams.mCaption.c_str());

	if (mParams.mHelpTopic.IsEmpty())
		ModifyStyleEx(WS_EX_CONTEXTHELP, 0, 0);
	else
	{
		ModifyStyleEx(0, WS_EX_CONTEXTHELP, 0);
		SetHelpTopic(mParams.mHelpTopic);
	}

	::mySetProp(m_tree_label.m_hWnd, "__VA_do_not_colour", (HANDLE)1);
	if (!mParams.mListCanBeColored)
		::mySetProp(m_tree.m_hWnd, "__VA_do_not_colour", (HANDLE)1);

	if (gTestLogger)
	{
		WTString msg;
		msg.WTFormat("GenericTreeDlg:\r\n  Caption: %s\r\n  label: %s", mParams.mCaption.c_str(),
		             mParams.mDirectionsText.c_str());
		gTestLogger->LogStr(msg);
	}

	if (mParams.mOnInitialised)
		mParams.mOnInitialised();

	InsertNodes(mParams.mNodeItems, TVI_ROOT);

	return true;
}

HTREEITEM
GenericTreeDlg::InsertTreeItem(LPCWSTR lpszItem, HTREEITEM hParent, HTREEITEM hInsertAfter)
{
	ASSERT(::IsWindow(m_tree.m_hWnd));
	TVINSERTSTRUCTW tvis;
	tvis.hParent = hParent;
	tvis.hInsertAfter = hInsertAfter;
	tvis.item.mask = TVIF_TEXT;
	tvis.item.pszText = (LPWSTR)lpszItem;
	return (HTREEITEM)::SendMessageW(m_tree.m_hWnd, TVM_INSERTITEMW, 0, (LPARAM)&tvis);
}

HTREEITEM
GenericTreeDlg::InsertTreeItem(LPCWSTR lpszItem, int nImage, int nSelectedImage, HTREEITEM hParent,
                               HTREEITEM hInsertAfter)
{
	ASSERT(::IsWindow(m_tree.m_hWnd));
	TVINSERTSTRUCTW tvis;
	tvis.hParent = hParent;
	tvis.hInsertAfter = hInsertAfter;
	tvis.item.mask = TVIF_TEXT | TVIF_SELECTEDIMAGE | TVIF_IMAGE;
	tvis.item.pszText = (LPWSTR)lpszItem;
	tvis.item.iImage = nImage;
	tvis.item.iSelectedImage = nSelectedImage;
	return (HTREEITEM)::SendMessageW(m_tree.m_hWnd, TVM_INSERTITEMW, 0, (LPARAM)&tvis);
}

void GenericTreeDlg::InsertNodes(GenericTreeNodeItem::NodeItems& nodes, HTREEITEM hParent)
{
	const WinVer wv = ::GetWinVersion();
	bool logNode = gTestLogger != nullptr;

	if (logNode && nodes.size() > 25)
	{
		if (gTestLogger)
			gTestLogger->LogStr("  too many nodes, node contents not logged");
		logNode = false;
	}

	for (GenericTreeNodeItem& curNode : nodes)
	{
		struct pre_post
		{
			GenericTreeDlgParams& mPars;
			GenericTreeNodeItem& mItem;
			pre_post(GenericTreeDlgParams& pars, GenericTreeNodeItem& item) : mPars(pars), mItem(item)
			{
				if (mPars.mPreprocessItem)
					mPars.mPreprocessItem(mItem);
			}
			~pre_post()
			{
				if (mPars.mPostprocessItem)
					mPars.mPostprocessItem(mItem);
			}
		} mPrePost(mParams, curNode);

		HTREEITEM thisItem = NULL;
		if ((!mParams.mApproveItem || mParams.mApproveItem(curNode)) &&
		    // [case: 55520] TVIS_EX_DISABLED is only supported in Vista or higher,
		    // so don't display disabled items in OS < Vista
		    (curNode.mEnabled || wv >= wvVista))
		{
			if (-1 == curNode.mIconIndex)
				thisItem = InsertTreeItem(curNode.mNodeText.Wide(), hParent, TVI_LAST);
			else
				thisItem =
				    InsertTreeItem(curNode.mNodeText.Wide(), curNode.mIconIndex, curNode.mIconIndex, hParent, TVI_LAST);

			if (curNode.mChecked)
				m_tree.SetCheck(thisItem, true);
			if (!curNode.mEnabled)
				m_tree.SetItemEx(thisItem, TVIF_STATEEX, 0, 0, 0, 0, 0, 0, TVIS_EX_DISABLED, 0, 0);
			m_tree.SetItemData(thisItem, (DWORD_PTR)&curNode);

			if (logNode)
			{
				_ASSERTE(gTestLogger);
				WTString msg;
				msg.WTFormat("  node: %d %d %d %s %s", curNode.mEnabled, curNode.mChecked, curNode.mIconIndex,
				             curNode.mNodeText.c_str(), curNode.mChildren.size() ? "\r\n +" : "");
				gTestLogger->LogStr(msg);
			}
		}

		if (thisItem && curNode.mChildren.size())
		{
			m_tree.SetItemState(thisItem, TVIS_EXPANDED, TVIS_EXPANDED);
			InsertNodes(curNode.mChildren, thisItem);
		}
	}

	if (mParams.mOnItemsUpdated)
		mParams.mOnItemsUpdated();

	m_tree.UpdateWindow();
}

void GenericTreeDlg::OnCheckToggle(HTREEITEM item)
{
	GenericTreeNodeItem* node = (GenericTreeNodeItem*)m_tree.GetItemData(item);
	if (!node)
		return;
	PropagateCheck(item, !node->mChecked);
}

void GenericTreeDlg::OnDpiChanged(DpiChange change, bool& handled)
{
	if (change != CDpiAware::DpiChange::AfterParent)
	{
		gImgListMgr->UpdateImageListsForDPI(m_tree);
		WindowScaler::RedrawWindow(m_tree);
	}
}

void GenericTreeDlg::CheckAllNodes(HTREEITEM hItem, BOOL val)
{
	if (hItem != NULL)
	{
		GenericTreeNodeItem* node = (GenericTreeNodeItem*)m_tree.GetItemData(hItem);
		if (node && node->mChecked != !!val)
			m_tree.on_check_toggle(hItem);

		CheckAllNodes(m_tree.GetChildItem(hItem), val);
		CheckAllNodes(m_tree.GetNextSiblingItem(hItem), val);
	}
}

void GenericTreeDlg::OnContextMenu(CWnd* pWnd, CPoint pos)
{
	HTREEITEM selItem = m_tree.GetSelectedItem();
	CRect rc;
	GetClientRect(&rc);
	ClientToScreen(&rc);
	if (!rc.PtInRect(pos) && selItem)
	{
		// place menu below selected item instead of at cursor when using
		// the context menu command
		if (m_tree.GetItemRect(selItem, &rc, TRUE))
		{
			m_tree.ClientToScreen(&rc);
			pos.x = rc.left + (rc.Width() / 2);
			pos.y = rc.bottom;
		}
	}

	WTString txt;
	CMenu contextMenu;
	contextMenu.CreatePopupMenu();

	bool preDev14 = !gShellAttr->IsDevenv14OrHigher();
	UINT_PTR menuItem1 = preDev14 ? 1 : MAKEWPARAM(1, ICONIDX_CHECKALL);
	UINT_PTR menuItem2 = preDev14 ? 2 : MAKEWPARAM(2, ICONIDX_UNCHECKALL);

	contextMenu.AppendMenu(0u, menuItem1, "&Check all");
	contextMenu.AppendMenu(0u, menuItem2, "&Uncheck all");

	CMenuXP::SetXPLookNFeel(this, contextMenu);
    MenuXpHook hk(this);

	BOOL res = contextMenu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD, pos.x, pos.y, this);
	if (LOWORD(res) == 1)
	{
		HTREEITEM hRoot = m_tree.GetRootItem();
		CheckAllNodes(hRoot, TRUE);
	}
	else if (LOWORD(res) == 2)
	{
		HTREEITEM hRoot = m_tree.GetRootItem();
		CheckAllNodes(hRoot, FALSE);
	}
}

void GenericTreeDlg::PropagateCheck(HTREEITEM item, bool checked)
{
	GenericTreeNodeItem* node = (GenericTreeNodeItem*)m_tree.GetItemData(item);
	if (!node)
		return;
	if (!node->mEnabled)
		return;
	node->mChecked = checked;
	m_tree.SetCheck(item, checked);

	HTREEITEM child = m_tree.GetNextItem(item, TVGN_CHILD);
	while (child)
	{
		PropagateCheck(child, checked);
		child = m_tree.GetNextItem(child, TVGN_NEXT);
	}
}

void GenericTreeDlgParams::FilterNodes(GenericTreeNodeItem::NodeItems& nodes, WTString& filter,
                                       GenericTreeNodeItem::State state,
                                       std::function<LPCSTR(GenericTreeNodeItem&)> get_text)
{
	for (GenericTreeNodeItem& item : nodes)
		item.SetState(state, true);

	if (filter.IsEmpty())
		return;

	StrVectorA inclusivePatterns, exclusivePatterns;
	const bool kIsMultiPattern = filter.Find(' ') != -1;
	if (kIsMultiPattern)
	{
		StrVectorA allPatterns;
		WtStrSplitA(filter, allPatterns, " ");

		// sort into inclusive or exclusive
		for (StrVectorA::iterator it = allPatterns.begin(); it != allPatterns.end(); ++it)
		{
			WTString cur(*it);
			cur = StrMatchOptions::TweakFilter(cur, true);
			if (cur.IsEmpty())
				continue;

			if (cur.Find('-') == -1)
				inclusivePatterns.push_back(cur);
			else
				exclusivePatterns.push_back(cur);
		}
	}
	else
	{
		const WTString cur = StrMatchOptions::TweakFilter(filter, true);
		if (!cur.IsEmpty())
			inclusivePatterns.push_back(cur);
	}

	for (int patternsGroupIdx = 0; patternsGroupIdx < 3; ++patternsGroupIdx)
	{
		StrVectorA* patterns;
		switch (patternsGroupIdx)
		{
		case 0:
			patterns = &inclusivePatterns;
			break;
		case 1:
			if (0 == exclusivePatterns.size())
				return;

			patterns = &exclusivePatterns;
			break;
		case 2:
			// reset mSuggestedItemIdx to first inclusive pattern match
			patterns = &inclusivePatterns;
			break;
		default:
			_ASSERTE(!"bad loop");
			return;
		}

		for (StrVectorA::iterator it = patterns->begin(); it != patterns->end(); ++it)
		{
			const WTString curPattern(*it);
			StrMatchOptions opts(curPattern, true, true);

			for (GenericTreeNodeItem& item : nodes)
			{
				if (item.GetState(state))
				{
					LPCSTR node_text = get_text ? get_text(item) : item.mNodeText.c_str();
					bool is_match = 0 != ::StrMatchRankedA(node_text, opts);
					item.SetState(state, is_match);
				}
			}
		}
	}
}

void GenericTreeDlgParams::FilterNodes(GenericTreeNodeItem::NodeItems& nodes, int min_level, int max_level,
                                       WTString& filter, GenericTreeNodeItem::State state,
                                       GenericTreeNodeItem::TextFnc get_text /*= GenericTreeNodeItem::TextFnc()*/)
{
	if (min_level == 0)
		GenericTreeDlgParams::FilterNodes(nodes, filter, state, get_text);

	if (min_level > 0 && max_level != 0)
	{
		min_level -= 1;

		if (max_level > 0 && max_level < INT_MAX)
			max_level -= 1;

		GenericTreeDlgParams::ForEach(nodes, min_level, max_level, [state, &filter, &get_text](GenericTreeNodeItem& n) {
			GenericTreeDlgParams::FilterNodes(n.mChildren, filter, state, get_text);
		});
	}
}

void GenericTreeDlgParams::SortNodes(GenericTreeNodeItem::NodeItems& nodes,
                                     GenericTreeNodeItem::SortFnc sortFnc /*= GenericTreeNodeItem::SortFnc()*/)
{
	if (sortFnc)
		std::sort(nodes.begin(), nodes.end(), sortFnc);
	else
	{
		std::sort(nodes.begin(), nodes.end(), [](const GenericTreeNodeItem& n1, const GenericTreeNodeItem& n2) -> bool {
			return n1.mNodeText < n2.mNodeText;
		});
	}
}

void GenericTreeDlgParams::SortNodes(GenericTreeNodeItem::NodeItems& nodes, int min_level, int max_level,
                                     GenericTreeNodeItem::SortFnc sortFnc /*= GenericTreeNodeItem::SortFnc()*/)
{
	if (min_level == 0)
		GenericTreeDlgParams::SortNodes(nodes, sortFnc);

	if (min_level > 0 && max_level != 0)
	{
		min_level -= 1;

		if (max_level > 0 && max_level < INT_MAX)
			max_level -= 1;

		GenericTreeDlgParams::ForEach(nodes, min_level, max_level, [&sortFnc](GenericTreeNodeItem& n) {
			GenericTreeDlgParams::SortNodes(n.mChildren, sortFnc);
		});
	}
}

void GenericTreeDlgParams::FilterNodesRegex(GenericTreeNodeItem::NodeItems& nodes, WTString& filter,
                                            GenericTreeNodeItem::State state,
                                            GenericTreeNodeItem::TextFnc get_text /*= GenericTreeNodeItem::TextFnc()*/)
{
	bool is_empty_filter = filter.IsEmpty();

	std::regex rgx(filter.c_str());

	std::stack<GenericTreeNodeItem::NodeItems*> stack;

	stack.push(&nodes);

	while (!stack.empty())
	{
		GenericTreeNodeItem::NodeItems* curr_nodes = stack.top();
		stack.pop();

		for (GenericTreeNodeItem& item : *curr_nodes)
		{
			if (is_empty_filter)
				item.SetState(state, true);
			else if (get_text)
				item.SetState(state, std::regex_search(get_text(item), rgx));
			else
				item.SetState(state, std::regex_search(item.mNodeText.c_str(), rgx));

			if (item.GetState(state) && !item.mChildren.empty())
				stack.push(&item.mChildren);
		}
	}
}

void GenericTreeDlgParams::FilterNodesRegex(GenericTreeNodeItem::NodeItems& nodes, int min_level, int max_level,
                                            WTString& filter, GenericTreeNodeItem::State state,
                                            GenericTreeNodeItem::TextFnc get_text /*= GenericTreeNodeItem::TextFnc()*/)
{
	if (min_level == 0)
		GenericTreeDlgParams::FilterNodesRegex(nodes, filter, state, get_text);

	if (min_level > 0 && max_level != 0)
	{
		min_level -= 1;

		if (max_level > 0 && max_level < INT_MAX)
			max_level -= 1;

		GenericTreeDlgParams::ForEach(nodes, min_level, max_level, [state, &filter, &get_text](GenericTreeNodeItem& n) {
			GenericTreeDlgParams::FilterNodesRegex(n.mChildren, filter, state, get_text);
		});
	}
}

void GenericTreeDlgParams::ForEach(GenericTreeNodeItem::NodeItems& nodes, int min_level, int max_level,
                                   GenericTreeNodeItem::ModifyFnc modifyfnc)
{
	struct lvl_nodes
	{
		GenericTreeNodeItem::NodeItems* nodes;
		int level;

		lvl_nodes(GenericTreeNodeItem::NodeItems* n, int l) : nodes(n), level(l)
		{
		}
	};

	std::stack<lvl_nodes> stack;
	stack.push(lvl_nodes(&nodes, 0));

	if (max_level < 0)
		max_level = INT_MAX;

	while (!stack.empty())
	{
		lvl_nodes curr = stack.top();
		stack.pop();

		for (GenericTreeNodeItem& item : *curr.nodes)
		{
			if (curr.level >= min_level && curr.level <= max_level)
				modifyfnc(item);

			if (curr.level < max_level && !item.mChildren.empty())
				stack.push(lvl_nodes(&item.mChildren, curr.level + 1));
		}
	}
}

GenericTreeNodeItem* GenericTreeDlgParams::FindItem(GenericTreeNodeItem::NodeItems& nodes, int min_level, int max_level,
                                                    GenericTreeNodeItem::SearchFnc searchfnc)
{
	struct lvl_nodes
	{
		GenericTreeNodeItem::NodeItems* nodes;
		int level;

		lvl_nodes(GenericTreeNodeItem::NodeItems* n, int l) : nodes(n), level(l)
		{
		}
	};

	std::stack<lvl_nodes> stack;
	stack.push(lvl_nodes(&nodes, 0));

	if (max_level < 0)
		max_level = INT_MAX;

	while (!stack.empty())
	{
		lvl_nodes curr = stack.top();
		stack.pop();

		for (GenericTreeNodeItem& item : *curr.nodes)
		{
			if (curr.level >= min_level && curr.level <= max_level)
				if (searchfnc(item))
					return &item;

			if (curr.level < max_level && !item.mChildren.empty())
				stack.push(lvl_nodes(&item.mChildren, curr.level + 1));
		}
	}

	return nullptr;
}

void GenericTreeDlg::UpdateNodes(bool applySavedView /*= false*/, bool clearSavedView /*= true*/)
{
	{
		ThemeUtils::AutoLockUpdate lock_upd(m_tree);
		m_tree.DeleteAllItems();
		InsertNodes(mParams.mNodeItems, TVI_ROOT);
	}

	if (applySavedView)
		ApplySavedView(clearSavedView);

	m_tree.UpdateWindow();
}

void GenericTreeDlg::SaveView()
{
	mSavedView[0] = m_tree.LastUsedID;
	mSavedView[1] = m_tree.SelectedID;
}

void GenericTreeDlg::ApplySavedView(bool clear /*= true*/)
{
	if (mSavedView[0])
	{
		for (HTREEITEM item = CTreeIter::GetFirstItem(m_tree); item; item = CTreeIter::GetNextItem(m_tree, item))
		{
			GenericTreeNodeItem* item_data = (GenericTreeNodeItem*)m_tree.GetItemData(item);

			if (item_data)
			{
				if (mSavedView[0] == item_data->mUniqueID)
				{
					if (mSavedView[0] == mSavedView[1])
						m_tree.SetItemState(item, TVIS_SELECTED, TVIS_SELECTED);

					m_tree.SelectSetFirstVisible(item);
					break;
				}
			}
		}
	}

	if (clear)
	{
		mSavedView[0] = 0;
		mSavedView[1] = 0;
	}
}

// CTreeCtrlCB

IMPLEMENT_DYNAMIC(CTreeCtrlCB, CTreeCtrl)

CTreeCtrlCB::CTreeCtrlCB()
{
}

CTreeCtrlCB::~CTreeCtrlCB()
{
}

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(CTreeCtrlCB, CThemedTree)
ON_WM_KEYDOWN()
ON_WM_CHAR()
ON_WM_KEYUP()
ON_NOTIFY_REFLECT(NM_CLICK, &CTreeCtrlCB::OnNMClick)
ON_NOTIFY_REFLECT(TVN_SELCHANGED, &CTreeCtrlCB::OnSelChanged)
END_MESSAGE_MAP()
#pragma warning(pop)

// CTreeCtrlCB message handlers

void CTreeCtrlCB::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	if (nChar == VK_SPACE)
	{
		if (on_check_toggle)
		{
			HTREEITEM item = GetSelectedItem();
			if (item)
				on_check_toggle(item);
		}
		return;
	}

	CTreeCtrl::OnKeyDown(nChar, nRepCnt, nFlags);
}

void CTreeCtrlCB::OnChar(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	if (nChar == ' ')
		return;

	CTreeCtrl::OnChar(nChar, nRepCnt, nFlags);
}

void CTreeCtrlCB::OnKeyUp(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	if (nChar == VK_SPACE)
		return;

	CTreeCtrl::OnKeyUp(nChar, nRepCnt, nFlags);
}

void CTreeCtrlCB::OnNMClick(NMHDR* pNMHDR, LRESULT* pResult)
{
	*pResult = 0;

	// TVN_ITEMCHANGED is Vista+ only, so we'll check checkboxes on each click!
	CPoint mouse((LPARAM)::GetMessagePos());
	ScreenToClient(&mouse);
	UINT flags = 0;
	HTREEITEM item = HitTest(mouse, &flags);
	if (!item || !(flags & TVHT_ONITEM))
	{
		LastClickedID = 0;
		return;
	}

	GenericTreeNodeItem* item_node = (GenericTreeNodeItem*)GetItemData(item);
	if (item_node)
		LastUsedID = LastClickedID = item_node->mUniqueID;

	// at this point, checkbox state is not yet updated, so we'll need to invert ourselves
	*pResult = 1;
	if (on_check_toggle)
		on_check_toggle(item);
}

void CTreeCtrlCB::OnSelChanged(NMHDR* pNMHDR, LRESULT* pResult)
{
	*pResult = 0;

	NMTREEVIEW* tw = (NMTREEVIEW*)pNMHDR;

	if (tw && tw->itemNew.hItem)
	{
		GenericTreeNodeItem* item = (GenericTreeNodeItem*)GetItemData(tw->itemNew.hItem);
		if (item)
		{
			// after sorting of vector with nodes,
			// any saved pointer from vector become invalid,
			// or on its position may be a different item
			LastUsedID = SelectedID = item->mUniqueID;
		}
	}
	else
		SelectedID = 0;
}
