#pragma once

#define DEFINE_VA_MESSAGE(name) __declspec(selectany) extern const UINT name = ::RegisterWindowMessageA(#name);

#define VAM_SORTSELECTION 0x317   /* WM_COMMAND message */
#define VAM_ADDBRACE 0x318        /* WM_COMMAND message */
#define VAM_IFDEFBLOCK 0x319      /* WM_COMMAND message */
#define VAM_UNCOMMENTBLOCK 0x320  /* WM_COMMAND message */
#define VAM_COMMENTBLOCK 0x321    /* WM_COMMAND message */
#define VAM_COMMENTBLOCK2 0x322   /* WM_COMMAND message */
#define VAM_UNCOMMENTBLOCK2 0x323 /* WM_COMMAND message */
#define VAM_PARENBLOCK 0x324      /* WM_COMMAND message */
DEFINE_VA_MESSAGE(VAM_DSM_COMMAND)
#define VAM_REGIONBLOCK 0x326 /* WM_COMMAND message */
#define VAM_NAMESPACEBLOCK 0x327

#define VAM_GETBUF 0x351      /* WM_COMMAND and VAM_DSM_COMMAND message */
#define VAM_SHOWGUESSES 0x352 /* WM_COMMAND message */

#define DSM_VA_LISTMEMBERS (WM_USER + 166) // used as both a message and a timer id
#define VAM_SETSEL (WM_USER + 802)         // point(r,c),point(r,c)
#define VAM_GETSEL (WM_USER + 803)         // &point(r,c),&point(r,c)
// don't use VAM_GETTEXT in any new places - use VAM_GETTEXTW instead
#define VAM_GETTEXT (WM_USER + 804)          // returns a temporary lpcstr
#define VAM_SWAPANCHOR (WM_USER + 805)       // swap anchor on current selection
#define VAM_GETSELECTIONMODE (WM_USER + 806) // return 10 for standard mode or 11 for block selection

// VA.net
#define VAM_EXECUTECOMMAND (WM_USER + 807) // run a nsvet command
#define VAM_ReleaseDocDte (WM_USER + 808)  // release vassistNet dte doc references (independent of VAM_DeleteDoc)
#define VAM_INDENT (WM_USER + 809)
#define VAM_GETSELSTRINGW (WM_USER + 810) // returns LPCWSTR of selection
#define VAM_GETTEXTW (WM_USER + 811)      // returns a temporary lpcwstr
#define VAM_REPLACESELW (WM_USER + 812)
#define VAM_CMDWithCURDOCOBJ                                                                                           \
	(WM_USER + 813)                 // Sends a VAM_command, with wparam as the docObj, and lParam as the MSG struct
#define VAM_GETSEL2 (WM_USER + 814) // &point(r,c),&point(r,c)
#define VAM_CARETPOSITION (WM_USER + 815)
#define VAM_QUERYSTATUS (WM_USER + 816)
#define VAM_UPDATE_VS_COLORS (WM_USER + 818)
#define VAM_SET_IVsTextView (WM_USER + 819)
#define VAM_FILEPATHNAMEW (WM_USER + 820)
#define VAM_GETOPENWINDOWLISTW (WM_USER + 821)
#define VAM_CREATELINEMARKER (WM_USER + 822)
#define VAM_OnChangeScrollInfo (WM_USER + 823)
#define WM_VA_READ_RTF_FILE (WM_USER + 824)
#define WM_VA_NEW_UNDO (WM_USER + 825)
#define WM_VA_SET_DB (WM_USER + 826)
#define VAM_CLOSE_DTE_DOC (WM_USER + 827)
#define VAM_ISSUBCLASSED (WM_USER + 828)
#define VAM_ISSUBCLASSED2 (WM_USER + 829)
#define VAM_UPDATE_SOLUTION_LOAD (WM_USER + 830)
#define VAM_DeleteDoc (WM_USER + 831) // delete the vassistNet WTDoc object
#define VAM_UpdateDocDte (WM_USER + 832)
#define VAM_Ping (WM_USER + 833)

// constant shared with ViEmu developer to ID windows that we have attached to ( j@ngedit.com )
#define WM_VA_GET_EDCNTRL 0x0545

#define WM_VA_FIRST WM_APP + 500 // 500 so we don't interfere with DevStudio msgs
#define WM_VA_MINIHELPW WM_VA_FIRST
#define WM_VA_SINKME WM_APP + 502
#define WM_VA_MINIHELP_SYNC WM_APP + 503
#define WM_VA_SETTEXT WM_APP + 504
#define WM_VA_MINIHELP_WPFRESIZE WM_APP + 505
#define WM_VA_MINIHELP_GETHEIGHT WM_APP + 506
//#define WM_VA_			WM_APP+509
#define WM_VA_GETVASERVICE2 WM_APP + 510
#define WM_VA_FILEOPENED WM_APP + 511
#define WM_VA_FILEOPENW WM_APP + 512
#define WM_VA_FLUSHFILE WM_APP + 513
#define WM_VA_DEVEDITCMD WM_APP + 514
#define WM_VA_PASTE WM_APP + 517
#define WM_VA_CODETEMPLATEMENU WM_APP + 518
#define WM_VA_CONTEXTMENU WM_APP + 519
#define WM_VA_DEFINITIONLIST WM_APP + 520
#define WM_VA_OPENOPPOSITEFILE WM_APP + 521
#define WM_VA_FLASHMARGIN WM_APP + 523
#define WM_VA_HISTORYFWD WM_APP + 524
#define WM_VA_HISTORYBWD WM_APP + 527
#define WM_VA_REPARSEFILE WM_APP + 528
#define WM_VA_COMMENTBLOCK WM_APP + 529
#define WM_VA_COMMENTLINE WM_APP + 530
#define WM_VA_GOTOCLASSVIEWITEM WM_APP + 536
#define WM_VA_SPELLDOC WM_APP + 539
#define WM_VA_OPTREBUILD WM_APP + 542 // constant shared w/ Atmel?
#define WM_VA_SCOPENEXT WM_APP + 545
#define WM_VA_SCOPEPREVIOUS WM_APP + 546
#define WM_VA_SMARTCOMMENT WM_APP + 548
#define WM_VA_CheckMinihelp WM_APP + 549

// Thread safe commands for refactoring threads
#define WM_VA_THREAD_DELAYOPENFILE WM_APP + 550
#define WM_VA_THREAD_SETSELECTION WM_APP + 551
#define WM_VA_THREAD_GETBUFFER WM_APP + 552
#define WM_VA_THREAD_GETBUFINDEX WM_APP + 553

#define WM_VA_WPF_GETFOCUS WM_APP + 554
#define WM_VA_THREAD_GETCHARPOS WM_APP + 555
#define WM_VA_THREAD_SET_VS_OPTION WM_APP + 556
#define WM_VA_THREAD_AST_GETLIST WM_APP + 557
#define WM_VA_THREAD_PROCESS_REF_QUEUE WM_APP + 558
#define WM_VA_MAIN_THREAD_CB WM_APP + 559
#define WM_VA_THREAD_AUTOMATION_PEEK WM_APP + 560
#define WM_VA_EXEC_UI_THREAD_TASKS WM_APP + 561
#define WM_VA_POST_EXEC_UI_THREAD_TASKS WM_APP + 562

// [case: 109205]
#define WM_VA_UE_ENABLE_UMACRO_INDENT_FIX_IF_RELEVANT WM_APP + 563
#define WM_VA_UE_DISABLE_UMACRO_INDENT_FIX_IF_ENABLED WM_APP + 564

#define WM_VA_PRENETCMD WM_APP + 601
#define WM_VA_HANDLEKEY (WM_APP + 601)
#define WM_VA_POSTNETCMD WM_APP + 602
#define WM_VA_LAST WM_VA_POSTNETCMD

// used as arg to WM_VA_HANDLEKEY
#define VAK_INVALIDATE_BUF 0xFE // VK_OEM_CLEAR

#define VA_WND_DATA 0x40000000

struct MarkerIds
{
	long mBraceMatchMarkerId;
	long mBraceMismatchMarkerId;
	long mRefMarkerId;
	long mRefAssignmentMarkerId;
	long mErrorMarkerId;
	long mSpellingErrorMarkerId;
	long mHashtagMarkerId;
};

struct IVsTextLines;
struct IVsTextMarkerClient;
struct IVsTextLineMarker;
struct CreateMarkerMsgStruct
{
	/* [in] */ IVsTextLines* iLines;
	//	/* [in] */ long iMarkerType;
	/* [in] */ long iStartLine;
	/* [in] */ long iStartIndex;
	/* [in] */ long iEndLine;
	/* [in] */ long iEndIndex;
	/* [in] */ IVsTextMarkerClient* pClient;
	/* [out] */ IVsTextLineMarker** ppMarker;
};
