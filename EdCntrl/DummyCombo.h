#pragma once

#include "FastComboBox.h"
#include "ImageListManager.h"

// control used above hcb in va view
class DummyCombo : public CFastComboBox
{
  public:
	DummyCombo() : CFastComboBox(true), mIconIdx(-1)
	{
	}
	virtual ~DummyCombo()
	{
	}

  public:
	void Init(LPCSTR tipText)
	{
		SetEditControlReadOnly();
		SetEditControlTipText(tipText);
		// FastComboBox does not display icons to left of text, so override here
		gImgListMgr->SetImgListForDPI(*(CComboBoxEx*)this, ImageListManager::bgCombo);
	}

  private:
	using CFastComboBox::Init;

  public:
	int InsertItem(_In_ const COMBOBOXEXITEMA* pCBItem)
	{
		_ASSERTE(pCBItem->iItem == 0);
		mIconIdx = pCBItem->iImage;
		mText = pCBItem->pszText;
		return CFastComboBox::InsertItem(pCBItem);
	}

	int InsertItem(_In_ const COMBOBOXEXITEMW* pCBItem)
	{
		_ASSERTE(pCBItem->iItem == 0);
		mIconIdx = pCBItem->iImage;
		mText = pCBItem->pszText;
		ASSERT(::IsWindow(m_hWnd));
		ASSERT(pCBItem != NULL);
		ASSERT(AfxIsValidAddress(pCBItem, sizeof(COMBOBOXEXITEMW), FALSE));
		return (int)::SendMessageW(m_hWnd, CBEM_INSERTITEMW, 0, (LPARAM)pCBItem);
	}

	void SettingsChanged()
	{
		CFastComboBox::SettingsChanged();

		if (!IsVS2010ColouringActive())
			return;

		// FastComboBox does not display icons to left of text, so override here
		gImgListMgr->SetImgListForDPI(*(CComboBoxEx*)this, ImageListManager::bgCombo);
	}

	virtual void OnDropdown()
	{
	}

	//{{AFX_VIRTUAL(DummyCombo)
	//}}AFX_VIRTUAL

  protected:
	virtual bool AllowPopup() const
	{
		return false;
	}
	virtual void OnSelect()
	{
	}
	virtual WTString GetItemTip(int nRow) const
	{
		return WTString();
	}
	virtual CStringW GetItemTipW(int nRow) const
	{
		return CStringW();
	}
	virtual void OnEditChange()
	{
	}

	virtual void GetDefaultTitleAndIconIdx(WTString& title, int& iconIdx) const
	{
		title = mText;
		iconIdx = mIconIdx;
	}

	virtual void OnGetdispinfo(NMHDR* pNMHDR, LRESULT* pResult)
	{
		*pResult = 0;
	}

	virtual void OnGetdispinfoW(NMHDR* pNMHDR, LRESULT* pResult)
	{
		*pResult = 0;
	}

	virtual bool IsVS2010ColouringActive() const
	{
		return CVS2010Colours::IsVS2010VAViewColouringActive();
	}

  private:
	//{{AFX_MSG(DummyCombo)
	//}}AFX_MSG

	int mIconIdx;
	WTString mText;
};
