#pragma once

#include "VAParse.h"
#include "CreateFileDlg.h"
#include "RefactoringActive.h"

#if defined(RAD_STUDIO)
#include "RadStudio/RadStudioUtils.h"
#endif

void CreateImplementation();
void Refactor(UINT flag, LPCSTR orgSymscope = NULL, LPCSTR newName = NULL, LPPOINT pt = NULL);
inline void Refactor(UINT flag, LPCSTR orgSymscope, const WTString& newName, LPPOINT pt = NULL)
{
	return Refactor(flag, orgSymscope, newName.c_str(), pt);
}
inline void Refactor(UINT flag, const WTString& orgSymscope, const WTString& newName, LPPOINT pt = NULL)
{
	return Refactor(flag, orgSymscope.c_str(), newName.c_str(), pt);
}

void OnRefactorErrorMsgBox(WTString customErrorString = "");

enum RefactorFlag
{
	VARef_FindUsage = 1,
	VARef_Rename,
	VARef_Rename_References,
	VARef_Rename_References_Preview,
	VARef_ExtractMethod,
	VARef_CreateMethodImpl,
	VARef_CreateMethodDecl,
	VARef_EncapsulateField,
	VARef_OverrideMethod,
	VARef_CreateMethodComment,
	VARef_FindErrorsInFile,
	VARef_FindErrorsInProject,
	VARef_MoveImplementationToSrcFile,
	VARef_AddMember,
	VARef_AddSimilarMember,
	VARef_ChangeSignature,
	VARef_ChangeVisibility,
	VARef_FindUsageInFile,
	VARef_AddInclude,
	VARef_CreateFromUsage,
	VARef_ExpandMacro,
	VARef_SmartSelect, // temporarily here
	VARef_ModifyExpression,
	VARef_PromoteLambda,

	// Graphing
	VARef_GraphFirst,
	VARef_GraphSymbol,
	VARef_GraphClassInheritance,
	VARef_GraphSolution,
	VARef_GraphIncludes,
	VARef_GraphReferences,
	VARef_GraphLast,

	VARef_ImplementInterface,
	VARef_RenameFilesFromMenuCmd,
	VARef_RenameFilesFromRefactorTip,
	VARef_RenameFilesFromRenameRefs,
	VARef_CreateFile,
	VARef_MoveSelectionToNewFile,
	VARef_IntroduceVariable,
	VARef_AddRemoveBraces,
	VARef_AddBraces,
	VARef_RemoveBraces,
	VARef_CreateMissingCases,
	VARef_MoveImplementationToHdrFile,
	VARef_ConvertBetweenPointerAndInstance,
	VARef_SimplifyInstance,
	VARef_GotoInclude,
	VARef_OpenFileLocation,
	VARef_DisplayIncludes,
	VARef_AddForwardDeclaration,
	VARef_ConvertEnum,

	VARef_InsertVASnippet,
	VARef_MoveClassToNewFile,
	VARef_SortClassMethods,

	VARef_ShareWith,

	VARef_Count // last item
};

// [case: 31484]
struct MoveClassRefactorHelper
{
	enum class ClassFindingStatus
	{
		NotFound,
		Found,
		Duplicate
	};

	static CStringW HeaderFileName;
	static CStringW HeaderPathName;
	std::pair<long, long> MoveLocationListHdrClass;
	std::vector<std::pair<long, long>> MoveLocationListHdrMethods;
	std::vector<std::pair<long, long>> MoveLocationListSrc;
	std::vector<std::pair<long, long>> MoveLocationListSrcNamespc;
	std::vector<CStringW> NamespaceList;
	std::vector<CStringW> PreprocessorList;
	bool IsSourceFile = false;
	ClassFindingStatus FoundResult = ClassFindingStatus::NotFound;
};

bool GetRefactorSym(EdCntPtr& curEd, DTypePtr& outType, WTString* outInvokingScope, bool reparseIfNeeded);

BOOL CanRefactor(RefactorFlag flag, WTString* outMenuText = NULL);

void FillVAContextMenuWithRefactoring(PopupMenuLmb& menu, size_t& find_refs_id, DTypePtr& refSym, bool isCtxMenu);
void ShowVAContextMenu(EdCntPtr cnt, CPoint point, bool mouse);

// call this to see if a disabled command should be visible
// (call CanRefactor first, then if it returns false, see if the command
// should be displayed at all)
BOOL DisplayDisabledRefactorCommand(RefactorFlag flag);

__inline void Refactor(RefactorFlag flag)
{
	Refactor((UINT)flag, NULL, NULL, NULL);
}

class VARefactorCls
{
  public:
	VARefactorCls(LPCSTR symscope = NULL, int curLine = -1);
	VARefactorCls(int ftype)
	    : mInfo(ftype)
	{
	}

	BOOL CanRefactor(RefactorFlag flag, WTString* outMenuText = NULL) const;
	static BOOL CanCreateDeclaration(DType* sym, const WTString& invokingScope);
	BOOL CreateDeclaration(DType* sym, const WTString& invokingScope);
	void FindClassScopePositionViaNamespaceUsings(WTString& scopeDeclPosToGoto, CStringW& file);
	static BOOL CanCreateMemberFromUsage();
	static BOOL CanImplementInterface(WTString* outMenuText);
	static BOOL ImplementInterface();
	BOOL CanCreateMethodComment() const;
	BOOL CreateMethodComment();
	static BOOL CanMoveImplementationToSrcFile(const DType* sym, const WTString& invokingScope, BOOL force = FALSE,
	                                           WTString* outMenuText = NULL);
	BOOL MoveImplementationToSrcFile(const DType* sym, const WTString& invokingScope, BOOL force = FALSE);
	static BOOL CanMoveImplementationToHdrFile(const DType* sym, const WTString& invokingScope, BOOL force = FALSE,
	                                           WTString* outMenuText = nullptr, int* nrOfOverloads = nullptr,
	                                           WTString* defPath = nullptr);
	BOOL CanConvertInstance(WTString* outMenuText = nullptr) const;
	BOOL CanSimplifyInstanceDeclaration() const;
	static BOOL CanConvertEnum();
	BOOL MoveImplementationToHdrFile(const DType* sym, const WTString& invokingScope, BOOL force = FALSE);
	BOOL CutImplementation(const BOOL isExternalInline, const BOOL isExplicitlyDefaulted, WTString* comment = nullptr, int cutSelectionOffset = 0);
	static BOOL CanCreateImplementation(DType* sym, const WTString& invokingScope, WTString* outMenuText = NULL);
	BOOL CreateImplementation(DType* sym, const WTString& invokingScope, BOOL displayErrorMessage = TRUE);
	static BOOL CanEncapsulateField(DType* sym);
	BOOL Encapsulate(DType* sym);
	BOOL CanExtractMethod() const;
	BOOL ExtractMethod();
	BOOL PromoteLambda();
	BOOL ConvertInstance(DType* sym);
	BOOL SimplifyInstanceDeclaration(DType* sym);
	static BOOL CanFindReferences(DType* sym);
	static BOOL CanRename(DType* sym);
	static BOOL CanPromoteLambda();
	BOOL CanRenameReferences() const;
	static BOOL CanAddMember(DType* sym);
	static BOOL AddMember(DType* cls, DType* similarMember = NULL);
	static BOOL CanAddSimilarMember(DType* sym);
	static BOOL CanChangeSignature(DType* sym);
	static BOOL ChangeSignature(DType* sym);
	static BOOL CanChangeVisibility(DType* sym);
	static BOOL CanOverrideMethod(DType* sym);
	static BOOL AddUsingStatement(const WTString& statement);
	BOOL GetAddIncludeInfo(int& outAtLine, CStringW& outFile, BOOL* sysOverride = nullptr) const;
	BOOL CanAddInclude() const;
	BOOL AddInclude();
	static BOOL CanAddForwardDeclaration();
	BOOL AddForwardDecl() const;
	static BOOL CanGotoInclude();
	BOOL GotoInclude();
	static BOOL CanOpenFileLocation();
	BOOL OpenFileLocation();
	static BOOL CanDisplayIncludes();
	BOOL DisplayIncludes();

	static BOOL CanExpandMacro(DType* sym, const WTString& invokingScope);
	static BOOL ExpandMacro(DType* sym, const WTString& invokingScope);

	static BOOL CanRenameFiles(CStringW filePath, DType* sym, const WTString& invokingScope, bool fromRefactorTip);
	static BOOL RenameFiles(CStringW filePath, DType* sym, const WTString& invokingScope, bool fromRefactorTip,
	                        CStringW newName = "");

	static BOOL CanCreateFile(EdCntPtr ed);
	static BOOL CreateFile(EdCntPtr ed);
	static BOOL CanMoveSelectionToNewFile(EdCntPtr ed);
	static BOOL MoveSelectionToNewFile(EdCntPtr ed);
	static BOOL CanMoveClassToNewFile(EdCntPtr ed, const DType* sym, const WTString& invokingScope);
	BOOL MoveClassToNewFile(EdCntPtr ed, const DType* sym, const WTString& invokingScope);

	static BOOL CanSortClassMethods(EdCntPtr ed, const DType* sym);
	static BOOL SortClassMethods(EdCntPtr ed, const DType* sym);

	BOOL CanIntroduceVariable(bool select) const;
	BOOL IntroduceVar();
	static BOOL CanAddRemoveBraces(EdCntPtr ed, WTString* outMenuText);
	static BOOL CanAddBraces();
	static BOOL CanRemoveBraces();
	BOOL AddRemoveBraces(EdCntPtr ed);
	BOOL AddBraces();
	BOOL RemoveBraces();
	static BOOL CanCreateCases();
	static BOOL CreateCases();
	const VAScopeInfo_MLC& GetScopeInfo() const
	{
		return mInfo;
	}

	void InsertVASnippet();
	void OverrideCurSymType(const WTString& symType);

  private:
#if defined(RAD_STUDIO)
	RadDisableOptimalFill mRadDisOptFill;
#endif

	RefactoringActive mActive;
	VAScopeInfo_MLC mInfo;
	WTString mDecLine;
	WTString mMethodScope;
	WTString mMethod;
	WTString mScope;
	CStringW mFilePath;
	WTString mSelection;

	bool GetSelBoundaries(LineMarkers::Node& node, ULONG ln, const WTString& name, std::pair<ULONG, ULONG>& boundaries);
};

BOOL CanSmartSelect(DWORD cmdId = 0);
BOOL SmartSelect(DWORD cmdId = 0);

CStringW GetFileLocationFromInclude(EdCntPtr ed);
bool GetAddIncludeLineNumber(const uint curFileId, const uint SymFileId, const int userAtLine, int& outInsertAtLine,
                             int searchForDuplicates = 0);
void RefineAddIncludeLineNumber(int& insertAtLine);
BOOL DoAddInclude(int insertAtLine, CStringW headerfile, BOOL sysOverride = FALSE);
CStringW FileFromDef(DType* sym);
CStringW FileFromScope(WTString symScope, DType* outSym = NULL);
BOOL RefactorDoCreateFile(CreateFileDlg::DlgType dlgType, RefactorFlag refFlag, EdCntPtr ed, bool useSelection,
                          CStringW optionalFileName = CStringW(), WTString optionalContents = WTString(),
                          MoveClassRefactorHelper moveClassRefactorHelper = MoveClassRefactorHelper());
bool DoCreateImpForUFunction(const CStringW& parameters);
bool DoCreateValForUFunction(const CStringW& parameters);

LPCSTR FindHeaderInTable(const WTString& sym_scope);

bool IsPreferredStlHeader(const CStringW& filename);

bool FindCppIncludeFile(
	CStringW& filenameRef, bool isBase = false);

bool FindPreferredInclude(
    CStringW& filenameRef, 
	const WTString& symScope, 
	MultiParsePtr mp, 
	const std::initializer_list<std::wstring>& base_names, /* base names of files in same path as filename */ 
	bool isCpp,                                            /* whether editor is a CPP file */
	uint includeDepth = 5, /* how deep nested includes should be checked */ 
	bool samePathIncludesOnly = true, /* if true, only includes in same path as filename are allowed */ 
	DTypeDbScope dbScope = DTypeDbScope::dbSystem);