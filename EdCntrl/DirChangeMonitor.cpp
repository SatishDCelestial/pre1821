#include "stdafxed.h"

#include <stdio.h>
#include <stdlib.h>
#include <conio.h>
#include <windows.h>
#include <winbase.h>

#include "WTString.h"
#include "file.h"
#include "TokenW.h"
#include "DBLock.h"
#include "parse.h"
#include "Mparse.h"
#include "Settings.h"
#include "project.h"
#include "..\common\ThreadName.h"
#include "DevShellAttributes.h"
#include "ParseThrd.h"
#include "Edcnt.h"
#include "VARefactor.h"
#include "FileTypes.h"
#include "StringUtils.h"

#define MAX_BUFFER 8192
//#define LOG_FILE_CHANGE_NOTIFICATION

WCHAR g_watchDir[MAX_PATH] = {L'\0'};

////////////////////////
// Watch External Files

static CStringW x_fileList;
static CStringW sModifiedProjectList;
static int sReadCount = 0;

void FileChangeNotification(LPCWSTR wfile, INT len, DWORD notificationAction)
{
	_ASSERTE(wfile);
	const int filenameLen = len / 2;

	if (gShellAttr->SupportsDspFiles() && filenameLen > 4 && wfile[filenameLen - 1] == L'p' &&
	    wfile[filenameLen - 2] == L's' && wfile[filenameLen - 3] == L'd' && wfile[filenameLen - 4] == L'.')
	{
		// watch for changes to .dsp files
		const CStringW file = CStringW(g_watchDir) + CStringW(wfile, filenameLen);
		if (!ContainsIW(sModifiedProjectList, file))
			sModifiedProjectList += file + L';'; // cache file to parse when we get focus
	}
	else if (!gShellAttr->SupportsDspFiles() &&
	         ((filenameLen > 6 && wfile[filenameLen - 1] == L'j' && wfile[filenameLen - 2] == L'o' &&
	           wfile[filenameLen - 3] == L'r' && wfile[filenameLen - 4] == L'p') ||
	          (filenameLen > 8 && wfile[filenameLen - 1] == L's' && wfile[filenameLen - 2] == L'p' &&
	           wfile[filenameLen - 3] == L'o' && wfile[filenameLen - 4] == L'r' && wfile[filenameLen - 5] == L'p')))
	{
		// watch for changes to project and property files
		const CStringW file = CStringW(g_watchDir) + CStringW(wfile, filenameLen) + L";";
		if (!ContainsIW(sModifiedProjectList, file))
			sModifiedProjectList += file; // cache file to parse when we get focus
	}
	else if (sReadCount < 10 && Psettings && Psettings->m_doModThread)
	{
		const CStringW file = CStringW(g_watchDir) + CStringW(wfile, filenameLen);
		const int ftype = GetFileType(file);
		if (!IsCFile(ftype))
			return;

		if (ShouldIgnoreFile(file, false))
			return;

		if (x_fileList.Find(file) == -1)
		{
#ifdef LOG_FILE_CHANGE_NOTIFICATION
			vLog("FileChangeNotification: %d %x %s", sReadCount, notificationAction, CString(file));
#endif // LOG_FILE_CHANGE_NOTIFICATION
			++sReadCount;
			x_fileList += file + L';'; // cache file to parse when we get focus
		}
		else
		{
#ifdef LOG_FILE_CHANGE_NOTIFICATION
			vLog("FileChangeNotification: skip %x %s", notificationAction, CString(file));
#endif // LOG_FILE_CHANGE_NOTIFICATION
		}
	}
}

void ProcessFileChanges()
{
	sReadCount = 0;
	HWND h = GetForegroundWindow();
	if (h != MainWndH || RefactoringActive::IsActive())
	{
#ifdef LOG_FILE_CHANGE_NOTIFICATION
		vLog("ProcessFileChanges 1");
#endif // LOG_FILE_CHANGE_NOTIFICATION
		return;
	}

	if (x_fileList.GetLength() && (g_currentEdCnt || (g_EdCntList.empty() && GlobalProject &&
	                                                  !GlobalProject->IsBusy() && GlobalProject->GetFileItemCount())))
	{
#ifdef LOG_FILE_CHANGE_NOTIFICATION
		vLog("ProcessFileChanges 2a");
#endif // LOG_FILE_CHANGE_NOTIFICATION
       // we have focus and a list of files to parse
		TokenW t = x_fileList;
		x_fileList.Empty();
		while (t.more() && g_ParserThread)
		{
			const CStringW file = t.read(L";");
			if (!file.GetLength())
				continue;

			bool queueFile = false;
			if (IsFile(file))
			{
				DB_READ_LOCK;
				if (MultiParse::IsIncluded(file, FALSE) && !MultiParse::IsIncluded(file, TRUE))
				{
					// queueFile if is a file that is known but stale
					queueFile = true;
				}
			}

			if (queueFile)
			{
#ifdef LOG_FILE_CHANGE_NOTIFICATION
				vLog("ProcessFileChanges: %s", CString(file));
#endif // LOG_FILE_CHANGE_NOTIFICATION
       // see if the current EdCnt has this file open.
       // if so, don't queue it up since we'll blow away the locals
				CStringW activeFile;
#if !defined(SEAN)
				try
#endif // !SEAN
				{
					EdCntPtr ed = g_currentEdCnt;
					if (ed)
						activeFile = ed->FileName();
				}
#if !defined(SEAN)
				catch (...)
				{
				}
#endif // !SEAN

				if (file.CompareNoCase(activeFile) && g_ParserThread)
					g_ParserThread->QueueFile(file);
			}
			else
			{
#ifdef LOG_FILE_CHANGE_NOTIFICATION
				vLog("ProcessFileChanges: skip %s", CString(file));
#endif // LOG_FILE_CHANGE_NOTIFICATION
			}
		}
	}
	else
	{
#ifdef LOG_FILE_CHANGE_NOTIFICATION
		vLog("ProcessFileChanges 2b");
#endif // LOG_FILE_CHANGE_NOTIFICATION
	}

	if (sModifiedProjectList.GetLength())
	{
		if (GlobalProject)
		{
			// we have focus and a list of files to parse
			TokenW t = sModifiedProjectList;
			sModifiedProjectList.Empty();
			while (t.more() && !StopIt)
			{
				CStringW prjFile = t.read(L";");
				if (prjFile.GetLength() && IsFile(prjFile) && GlobalProject)
					GlobalProject->CheckForChanges(prjFile);
			}
		}
		else
			sModifiedProjectList.Empty();
	}
}

typedef struct _DIRECTORY_INFO
{
	WCHAR lpBuffer[MAX_BUFFER];
	DWORD dwBufLength;
	OVERLAPPED Overlapped;
} DIRECTORY_INFO;

static DIRECTORY_INFO g_di;
VOID CALLBACK FileIOCompletionRoutine(DWORD dwErrorCode, DWORD dwNumberOfBytesTransfered, LPOVERLAPPED lpOverlapped)
{
	DWORD cbOffset;
	PFILE_NOTIFY_INFORMATION fni = (PFILE_NOTIFY_INFORMATION)g_di.lpBuffer;
	if (gShellIsUnloading)
		return;

	// do we miss change notifications because of this Sleep?
	Sleep(500); // give time for file to flush
	do
	{
		cbOffset = fni->NextEntryOffset;

		FileChangeNotification(fni->FileName, (int)fni->FileNameLength, fni->Action);

		fni = (PFILE_NOTIFY_INFORMATION)((LPBYTE)fni + cbOffset);
	} while (cbOffset && !gShellIsUnloading);
}

UINT WatchDirLoop(LPVOID dir)
{
	DEBUG_THREAD_NAME("VAX:DirChangeMonitor");
	ZeroMemory(g_watchDir, sizeof(g_watchDir));
#if !defined(SEAN)
	try
#endif // !SEAN
	{
		CStringW oldDir;
		HANDLE h = NULL;
		OVERLAPPED OverLapping;
		memset(&OverLapping, 0, sizeof(OverLapping));

		while (!gShellIsUnloading)
		{
			if (g_watchDir[0])
			{
				// Are changes that occur during ProcessFileChanges missed?
				// If so, ProcessFileChanges should be run on a consumer thread.
				ProcessFileChanges();

				if (oldDir != g_watchDir)
				{
					oldDir = g_watchDir;
					if (h)
						CloseHandle(h);
					h = CreateFileW(g_watchDir, FILE_LIST_DIRECTORY,
					                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
					                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL);
				}

				DWORD Written = 0;
				if (GetOverlappedResult(h, &g_di.Overlapped, &Written, false))
				{
					ReadDirectoryChangesW(
					    h, g_di.lpBuffer, MAX_BUFFER, TRUE,
					    // [case: 73883]
					    // FILE_ACTION_MODIFIED|FILE_ACTION_RENAMED_NEW_NAME is what
					    // we have used since the beginning so far as I can tell.
					    // According to the documentation, these flags are not supposed
					    // to be passed in.  They are for the return notifications.
					    // But it works.
					    // Except it doesn't work on Win 8.
					    // FILE_NOTIFY_CHANGE_LAST_WRITE works on Win7 and Win8.
					    //
					    // FILE_ACTION_MODIFIED|FILE_ACTION_RENAMED_NEW_NAME overlap
					    // since they aren't masks; they're independent statuses.
					    //
					    // FILE_ACTION_MODIFIED is equivalent to:
					    // FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME
					    // FILE_ACTION_RENAMED_NEW_NAME is equivalent to:
					    // FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_ATTRIBUTES
					    //
					    // keeping FILE_NOTIFY_CHANGE_ATTRIBUTES in case XP/Vista require it.
					    //
					    FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_LAST_WRITE, &g_di.dwBufLength,
					    &g_di.Overlapped, &FileIOCompletionRoutine);
				}
				else
				{
					auto Error = GetLastError();
					if (Error != ERROR_IO_PENDING && Error != ERROR_IO_INCOMPLETE)
					{
						vLog("ERROR: WatchDir GetOverlappedResult: 0x%lx", Error);
					}
				}

				for (int n = 4; n && !gShellIsUnloading; n--)
				{
					// IOCompletionRoutine will not be called during non-alertable sleep
					Sleep(250);
					// alertable sleep means the IOCompletionRoutine can be called
					SleepEx(250, TRUE);
				}
			}
			else
			{
				if (h)
				{
					CloseHandle(h);
					h = NULL;
					oldDir.Empty();
				}

				while (!g_watchDir[0] && !gShellIsUnloading)
					Sleep(1000);

				x_fileList.Empty();
				sReadCount = 0;
			}
		}

		if (h)
			CloseHandle(h);
	}
#if !defined(SEAN)
	catch (...)
	{
	}
#endif // !SEAN

	return 1;
}
