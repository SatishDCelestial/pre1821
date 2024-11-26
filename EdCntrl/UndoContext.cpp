#include "StdAfxEd.h"
#include "UndoContext.h"
#include "Edcnt.h"
#include "SubClassWnd.h"
#include "project.h"
#include "VaMessages.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif // _DEBUG

UndoContext::UndoContext(LPCSTR name)
{
	if (g_currentEdCnt)
		NavAdd(g_currentEdCnt->FileName(), g_currentEdCnt->CurPos());
	::SendMessage(gVaMainWnd->GetSafeHwnd(), WM_VA_NEW_UNDO, (WPARAM)name, 0);
}

UndoContext::~UndoContext()
{
	if (g_currentEdCnt)
		NavAdd(g_currentEdCnt->FileName(), g_currentEdCnt->CurPos());
	::SendMessage(gVaMainWnd->GetSafeHwnd(), WM_VA_NEW_UNDO, 0, 0);
}
