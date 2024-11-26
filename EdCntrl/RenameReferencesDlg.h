#ifndef RenameReferencesDlg_h__
#define RenameReferencesDlg_h__

#include "ReferencesWndBase.h"
#include "CtrlBackspaceEdit.h"

class FreezeDisplay;
class CColourizedControl;
typedef std::shared_ptr<CColourizedControl> CColourizedControlPtr;
typedef std::pair<const CStringW, std::shared_ptr<std::deque<HTREEITEM>>> FileRefItemsElement;
typedef std::vector<std::shared_ptr<FileRefItemsElement>> FilesRefItemsCollection;

#if defined(RAD_STUDIO)
class RAD_DesignerReferences
{
  public:
	RAD_DesignerReferences() = default;
	virtual ~RAD_DesignerReferences() = default;
  protected:
	std::unique_ptr<FindRefsScopeInfo> RAD_DesignerRefsInfo;
	WTString RAD_passFindSym[2];
	WTString RAD_fieldPassSymScope;
	int RAD_fieldPassRefImgIdx = -1;
	std::set<UINT> RAD_designerFiles;
	std::map<UINT, HTREEITEM> RAD_fileTreeItems;
	bool RAD_skipSymbols = false;
	HTREEITEM RAD_designerItem = NULL;

	void RAD_FixReplaceString(int pass, const WTString& findText, WTString& replaceText);
};
#endif

class UpdateReferencesDlg : public ReferencesWndBase
#if defined(RAD_STUDIO)
    ,protected RAD_DesignerReferences
#endif
{
  public:
	enum UpdateResult
	{
		rrNoChange,
		rrError,
		rrSuccess
	};

	virtual void SetSymMaskedType(uint symMaskedType);
	virtual void SetIsUEMarkedType(bool isUEMarkedType);

  protected:
	UpdateReferencesDlg(const char* settingsCategory, UINT idd, CWnd* pParent, bool displayProjectNodes,
	                    bool outerUndoContext);
	virtual ~UpdateReferencesDlg() = default;

	virtual UpdateResult UpdateReference(int refIdx, FreezeDisplay& _f) = 0;

	virtual BOOL OnInitDialog();

  private:
	using ReferencesWndBase::OnInitDialog;

  protected:
	afx_msg void OnDestroy();
	virtual void OnCancel();
	virtual void UpdateStatus(BOOL done, int fileCount) = 0;
	virtual void OnSearchBegin();
	virtual void OnSearchComplete(int fileCount, bool wasCanceled);
	virtual void FindCurrentSymbol(const WTString& symScope, int typeImgIdx);
	virtual void OnDoubleClickTree();
	virtual void ShouldNotBeEmpty();

	void OnUpdate();
	void WriteCodeRedirectsForUE(CStringW oldSymName, CStringW newSymName);
	void EditUECoreRedirectsINI(const CStringW& coreRedirectsIniFileFullPath, CStringW& oldSymName, CStringW& newSymName);
	void OnToggleCommentDisplay();
	void OnCheckAllCommentsAndStrings();
	void OnUncheckAllCommentsAndStrings();
	void CheckAllCommentsAndStrings(bool check);
	void OnToggleWiderScopeDisplay();
	// 	void OnCheckAllOverridesEtc();
	// 	void OnUncheckAllOverridesEtc();
	// 	void CheckAllOverridesEtc(bool check);
	void OnCheckAllIncludes();
	void OnUncheckAllIncludes();
	void OnCheckAllUnknown();
	void OnUncheckAllUnknown();
	void OnToggleAllProjects();
	void OnToggleCoreRedirects();
	void CheckDescendants(HTREEITEM item, bool check);
	void CheckDescendants(HTREEITEM item, int refType, bool check);
	void OpenFilesForUpdate(HTREEITEM refItems);
	void UpdateReferences(HTREEITEM refItems, FreezeDisplay& _f, int& nChanged, BOOL& hasError);
	virtual void DoDataExchange(CDataExchange* pDX);
	DECLARE_MESSAGE_MAP()
	virtual void OnPopulateContextMenu(CMenu& contextMenu);
	void OnCheckAllCmd();
	void OnUncheckAllCmd();
	void OnChangeEdit();
	virtual BOOL ValidateInput()
	{
		return TRUE;
	}
	virtual BOOL OnUpdateStart()
	{
		return TRUE;
	}
	virtual void OnUpdateComplete()
	{
	}
	virtual void RegisterRenameReferencesControlMovers();
	void GetFilesAndReferenceItemFlat(HTREEITEM refItem, FilesRefItemsCollection& filesRefItemsCollection);
	bool mAutoUpdate;
	WTString mEditTxt;
	int mFirstVisibleLine;
	CtrlBackspaceEdit<CThemedEdit> mEdit_subclassed;
	bool mColourize;
	CColourizedControlPtr mColourizedEdit;
	bool mIsLongSearch;
	bool mOuterUndoContext; // do not create undo context
	// [case: 147774] store symbol type for determining if it is class, struct, function or enum
	// values are defined in SymbolTypes.h
	uint mSymMaskedType = UNDEF;
	bool mCreateCoreRedirects = false;
	bool mIsUEMarkedType = false;
};

class RenameReferencesDlg : public UpdateReferencesDlg
{
  public:
	RenameReferencesDlg(const WTString& symScope, LPCSTR newName = NULL, BOOL autoRename = FALSE,
	                    BOOL ueRenameImplicit = FALSE);

	virtual ~RenameReferencesDlg() = default;

  protected:
	DECLARE_MESSAGE_MAP()
	virtual BOOL OnInitDialog() override;
	virtual void UpdateStatus(BOOL done, int fileCount) override;
	virtual void OnSearchBegin();
	virtual void OnSearchComplete(int fileCount, bool wasCanceled) override;
	virtual UpdateResult UpdateReference(int refIdx, FreezeDisplay& _f) override;
	virtual void OnUpdateComplete() override;
	virtual BOOL ValidateInput();

	enum
	{
		IDD = IDD_RENAMEREFERENCES
	};

private:
	using ReferencesWndBase::OnInitDialog;

#if defined(RAD_STUDIO)
	LRESULT OnAddReference(WPARAM wparam, LPARAM lparam) override;
	LRESULT OnAddFileReference(WPARAM wparam, LPARAM lparam) override;
	LRESULT OnSearchProgress(WPARAM prog, LPARAM lparam) override;

	LRESULT RAD_FindOrAddFileReference(WPARAM wparam, LPARAM lparam);
	void RAD_AddDesignerItem(const TreeReferenceInfo* info);
#endif
};

BOOL ReplaceReferencesInFile(const WTString& symscope, const WTString& newname, const CStringW& file);

#endif // RenameReferencesDlg_h__
