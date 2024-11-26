#pragma once

#include "Foo.h"
#include "ReferencesWndBase.h"

class RenameFilesDlg : public ReferencesWndBase
{
  public:
	RenameFilesDlg(CStringW currentFileName, DType* sym);
	~RenameFilesDlg();

	static CStringW BuildNewFilename(CStringW fileName, CStringW theNewName);
	void SetNewName(CStringW newName);

  protected:
	virtual BOOL OnInitDialog();

  private:
	using ReferencesWndBase::OnInitDialog;

  protected:
	void PopulateTree();

	virtual void OnCancel();
	virtual void UpdateStatus(BOOL done, int fileCount);
	virtual void OnSearchComplete(int fileCount, bool wasCanceled);
	virtual void FindCurrentSymbol(const WTString& symScope, int typeImgIdx);
	virtual void OnDoubleClickTree();
	void OnRename();
	void DoRename();
	void OnChangeNameEdit();
	void CheckName();
	void UpdateMap(HTREEITEM item);

	enum
	{
		IDD = IDD_RENAMEFILES
	};
	virtual void DoDataExchange(CDataExchange* pDX);
	DECLARE_MESSAGE_MAP()

  private:
	bool mAutoRename;
	bool mDoRename;
	bool mCanceledManualRename;
	CStringW m_originalFileName;
	CStringW m_originalName;
	CStringW m_newName;

	int mFirstVisibleLine;
	CThemedEdit mEdit_subclassed;

	typedef std::map<CStringW, CStringW> RenameMap;
	RenameMap mRenameMap;

	void AddFileToIncludesMap(CStringW fileName);
	int UpdateIncludes();
	int BuildRenameMap(RenameMap& rm);
	int RenameDocuments(const RenameMap& rm);
	int SanityCheckMap(const RenameMap& rm);
	int WalkProject(CComPtr<EnvDTE::Project> pProject, const RenameMap& rm);
	int WalkProjectItems(const CStringW& projFilepath, CComPtr<EnvDTE::ProjectItems> pItems, const RenameMap& rm);
	void LogTree(HTREEITEM item);

	virtual void GoToItem(HTREEITEM item);

	struct CheckedFileName
	{
		CheckedFileName(CStringW oldName) : mOldName(oldName), mChecked(true)
		{
		}
		CStringW mOldName;
		CStringW mNewName;
		bool mChecked;
	};

	struct CheckedDType
	{
		CheckedDType(DType& sym) : mSym(sym), mChecked(true)
		{
		}
		DType mSym;
		bool mChecked;
	};

	typedef std::list<CheckedDType> CheckedDTypeList;
	typedef std::pair<CheckedFileName /* fileName */, CheckedDTypeList /* includesToUpdate */> MapEntry;
	std::list<MapEntry> mMap;
};
