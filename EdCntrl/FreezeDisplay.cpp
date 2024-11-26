#include "StdAfxEd.h"
#include "FreezeDisplay.h"
#include "Edcnt.h"
#include "SubClassWnd.h"
#include "project.h"
#include "file.h"
#include "DevShellAttributes.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif // _DEBUG

BOOL g_DisplayFrozen = FALSE;

//#ifndef RAD_STUDIO
#define FREEZE_SUPPORTED
//#endif

FreezeDisplay::FreezeDisplay(BOOL returnToOrgPos /* = TRUE */, BOOL useStartOfSelection /*= FALSE*/)
{
	_ASSERTE(!useStartOfSelection || returnToOrgPos);
	supressRedraw = TRUE;
	EdCntPtr ed(g_currentEdCnt);
	if (ed)
	{
		NavAdd(ed->FileName(), ed->CurPos());
		if (returnToOrgPos)
		{
			orgFile = ed->FileName();
			orgPos = ed->CurPos();

			if (useStartOfSelection)
			{
				long p1, p2;
				ed->GetSel2(p1, p2);
				orgPos = uint(p1 < p2 ? p1 : p2);
			}
		}
	}

#ifdef FREEZE_SUPPORTED
	g_DisplayFrozen = TRUE;
	if (!gShellAttr->IsDevenv10OrHigher())
		::SendMessage(MainWndH, WM_SETREDRAW, FALSE, 0);
#else
	supressRedraw = FALSE;
#endif
}

FreezeDisplay::~FreezeDisplay()
{
	if (orgFile.GetLength())
	{
		DelayFileOpen(orgFile);
		if (g_currentEdCnt)
			g_currentEdCnt->SetPos(orgPos);
	}

#ifdef FREEZE_SUPPORTED
	if (!gShellAttr->IsDevenv10OrHigher())
	{
		::SendMessage(MainWndH, WM_SETREDRAW, TRUE, 0);
		::RedrawWindow(MainWndH, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
	}
	g_DisplayFrozen = FALSE;
#endif
}

void FreezeDisplay::LeaveCaretHere()
{
	orgFile.Empty();
}

void FreezeDisplay::ReadOnlyCheck()
{
#ifdef FREEZE_SUPPORTED
	if (supressRedraw && g_currentEdCnt && IsFileReadOnly(g_currentEdCnt->FileName()))
	{
		supressRedraw = FALSE;
		if (!gShellAttr->IsDevenv10OrHigher())
			::SendMessage(MainWndH, WM_SETREDRAW, TRUE, 0);
	}
#endif
}

void FreezeDisplay::OffsetLine(int lines)
{
	ULONG orgLine = TERROW(orgPos);
	int orgCol = TERCOL(orgPos);
	orgLine += lines;
	orgPos = (uint)TERRCTOLONG(orgLine, orgCol);
}
