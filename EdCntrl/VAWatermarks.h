#pragma once

#include <atlstr.h>


enum class VAWindowType
{
	OFIS, // VAOpenFile.cpp
	FSIS, // VABrowseSym.cpp
	BrowseMembers, // VABrowseMembers.cpp
	RenameReferences, // RenameReferencesDlg.cpp
	UpdateReferences, // RenameReferencesDlg.cpp
	RenameFiles, // RenameFilesDlg.cpp
	CreateFile, // CreateFileDlg.cpp
	AddClassMember, // AddClassMemberDlg.cpp
	ExtractMethod, // ExtractMethodDlg.cpp
	MoveImplementations, // VARefactor.cpp
	CreateImplementations, // VARefactor.cpp
	ImplementMethods, // ImplementMethods.cpp
	InsertPath, // InsertPathDialog.cpp
	CreateFromUsage, // CreateFromUsage.cpp
	SortSelectedLines, // SortSelectedLinesDlg.cpp
	SpellCheck, // SpellCheckDlg.cpp
	ConvertBetweenPointerAndInstance, // ConvertBetweenPointerAndInstanceDlg.cpp
};

bool VAUpdateWindowTitle(VAWindowType wintype, CString& title, int reason = 0);
bool VAUpdateWindowTitle(VAWindowType wintype, WTString& title, int reason = 0);
WTString VAUpdateWindowTitle_c(VAWindowType wintype, WTString title, int reason = 0);
bool VAUpdateWindowTitle(VAWindowType wintype, HWND hwnd, int reason = 0);
