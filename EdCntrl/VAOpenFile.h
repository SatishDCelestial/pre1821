#if !defined(AFX_VAOPENFILE_H__6AF2A31F_6B24_40D6_8B47_B11B7B110370__INCLUDED_)
#define AFX_VAOPENFILE_H__6AF2A31F_6B24_40D6_8B47_B11B7B110370__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

// VAOpenFile.h : header file
//

#include "FindInWkspcDlg.h"
#include "Menuxp\MenuXP.h"
#include "PooledThreadBase.h"

class FileDataList;
struct FileInfo;

/////////////////////////////////////////////////////////////////////////////
// VAOpenFile dialog

class VAOpenFile : public FindInWkspcDlg
{
	DECLARE_MENUXP()

	friend void VAOpenFileDlg();
	// Construction
  private:
	VAOpenFile(CWnd* pParent = NULL); // standard constructor
	~VAOpenFile();

  protected:
	virtual void PopulateList();
	virtual void UpdateFilter(bool force = false);

  private:
	void AddFile(const CStringW& file, bool checkUnique, const CStringW& projectName);
	void AddFile(const FileInfo& fInfo, const CStringW& projectName);
	void UpdateTitle();

	// Dialog Data
	//{{AFX_DATA(VAOpenFile)
	enum
	{
		IDD = IDD_VAOPENFILEDLG
	};
	//}}AFX_DATA

	// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(VAOpenFile)
  public:
  protected:
	//}}AFX_VIRTUAL

	// Implementation
  protected:
	static void OfisLoader(LPVOID pVaOpenFile);
	void PopulateListThreadFunc();
	void StopThreadAndWait();
	virtual int GetFilterTimerPeriod() const;
	virtual void GetTooltipText(int itemRow, WTString& txt);
	virtual void GetTooltipTextW(int itemRow, CStringW& txt);
	void AugmentSolutionFiles();
	void RearrangeForPersistentFilterEditControl();

	void SetEditHelpText();

	// Generated message map functions
	//{{AFX_MSG(VAOpenFile)
	virtual void OnOK();
	virtual void OnCancel();
	virtual BOOL OnInitDialog();
	virtual BOOL PreTranslateMessage(MSG* pMsg);

	afx_msg void OnDestroy();
	afx_msg void OnClickHeaderItem(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnGetdispinfo(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnGetdispinfoW(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
	afx_msg void OnToggleDupeDisplay();
	afx_msg void OnToggleCaseSensitivity();
	afx_msg void OnToggleTooltips();
	afx_msg void OnToggleIncludeSolution();
	afx_msg void OnToggleIncludePrivateSystem();
	afx_msg void OnToggleIncludeWindowList();
	afx_msg void OnToggleIncludeSystem();
	afx_msg void OnToggleIncludeExternal();
	afx_msg void OnToggleAugmentSolution();
	afx_msg void OnToggleIncludeHiddenExtensions();
	afx_msg void OnToggleDisplayPersistentFilter();
	afx_msg void OnToggleApplyPersistentFilter();
	afx_msg void OnSelectAll();
	afx_msg void OnCopyFilename();
	afx_msg void OnCopyFullPath();
	afx_msg void OnOpenContainingFolder();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()

  private:
	FilterEdit<CtrlBackspaceEdit<CThemedDimEdit>> mEdit2;
	FileDataList* mFileData;
	FunctionThread mLoader;
	enum class ListLoadState
	{
		LoadNormal = 0,
		InterruptLoad,
		QuitLoad
	};
	ListLoadState mLoadControl = ListLoadState::LoadNormal;
	bool mIsLongLoad = false;
	enum class ButtonTextId
	{
		txtCancel,
		txtStop
	};
	ButtonTextId mButtonTxtId = ButtonTextId::txtCancel;
};

class FileData : public FileInfo
{
  public:
	FileData(const FileInfo& fInfo, const CStringW& projectBaseName)
	    : FileInfo(fInfo), mProjectBaseName(projectBaseName)
	{
	}
	FileData(const CStringW& filename, const CStringW& projectBaseName)
	    : FileInfo(filename), mProjectBaseName(projectBaseName)
	{
	}

	CStringW mProjectBaseName;
	CTime mFiletime;

  private:
	FileData();
	FileData(const FileData&);
};

void BrowseToFile(const CStringW& filename);

extern void VAOpenFileDlg();

// AddFile is called for every file found
// if generation parameter is used, AddFile will be called only if modifications are done since the last time
extern bool EnumerateProjectFiles(std::function<void(const FileInfo&, const CStringW&)> AddFile,
                                  LONG* generation = NULL);

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_VAOPENFILE_H__6AF2A31F_6B24_40D6_8B47_B11B7B110370__INCLUDED_)
