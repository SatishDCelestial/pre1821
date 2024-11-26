#ifndef ReferencesTreeSubclass_h__
#define ReferencesTreeSubclass_h__

#include "mctree/ColumnTreeWnd.h"

class ReferencesWndBase;

class ReferencesTreeSubclass : public CColumnTreeWnd
{
  public:
	ReferencesTreeSubclass(ReferencesWndBase& parent) : m_parent(parent)
	{
	}
	~ReferencesTreeSubclass()
	{
		DestroyWindow();
	}

  protected:
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);
	afx_msg void OnSelect(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnClickTree(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnDblClickTree(NMHDR* pNMHDR, LRESULT* pResult);

	DECLARE_MESSAGE_MAP()

  private:
	ReferencesWndBase& m_parent;
};

#endif // ReferencesTreeSubclass_h__
