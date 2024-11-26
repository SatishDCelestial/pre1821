#include "StdAfxEd.h"
#include "file.h"
#include "FileTypes.h"
#include "RegKeys.h"
#include "log.h"
#include "AutoTemplateConverter.h"
#include "StatusWnd.h"
#include "CFileW.h"
#include "DevShellService.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

static LPCTSTR kOldTemplateReadme1 = "Autotext and Code Templates for Visual Assist X"; // keep old name
static LPCTSTR kOldTemplateReadme2 = "Code Templates follow. All entry names contain a space.";
static LPCTSTR kOldTemplateReadme3 = "AutoText follows. None of the entry names contain a space.";

void CAutoTemplateConverter::ConvertIfRequired(const CStringW& filePath)
{
	m_sTplFilePath = filePath;
	m_sOutputBuffer.Empty();
	static int sLastRecycleResult = 0;
	if (!sLastRecycleResult && IsConversionRequired())
	{
		::SetStatus("Converting Visual Assist template file...");
		if (ConvertTemplateFile())
		{
			sLastRecycleResult = ::RecycleFile(m_sTplFilePath);
			if (0 == sLastRecycleResult)
				Save();
			else if (ERROR_ACCESS_DENIED == sLastRecycleResult)
			{
				static bool once = true;
				if (once)
				{
					once = false;
					CString msg;
					CString__FormatA(msg,
					                 "A file in the directory \"%s\" could not be modified because access was denied.\n"
					                 "\n"
					                 "Please modify the security settings of this directory.\n"
					                 "Add users of Visual Assist and grant them full control permissions.",
					                 (LPCTSTR)CString(::Path(filePath)));
					WtMessageBox(msg, IDS_APPNAME, MB_OK);
				}
			}
		}
		::SetStatus("Ready");
	}
}

bool CAutoTemplateConverter::IsConversionRequired()
{
	CFileW f;
	WTString fileTxt;
	bool isOldStyle = false;

	if (!f.Open(m_sTplFilePath, CFile::modeRead))
	{
		SILENT_ASSERT1(0, "IsConversionRequired file open error", CString(m_sTplFilePath));
		return false;
	}

	const DWORD fLen = (DWORD)f.GetLength();
	LPTSTR buf = fileTxt.GetBuffer(int(fLen + 1));
	if (buf)
	{
		f.Read(buf, fLen);
		buf[fLen] = '\0';
		fileTxt.ReleaseBuffer();
	}
	f.Close();

	if (fileTxt.GetLength() > 1)
	{
		if (0 == fileTxt.Find("a:"))
			; // first line is new style
		else
		{
			bool found = fileTxt.Find("caretPosChar:") != -1;
			if (found)
				isOldStyle = true;
			else
			{
				found = fileTxt.Find("\nseparator") != -1;
				if (found)
					isOldStyle = true;
				else
				{
					// look for new style
					found = fileTxt.Find("\na:") != -1;
					if (!found)
						isOldStyle = true;
				}
			}
		}
	}

	return isOldStyle;
}

bool CAutoTemplateConverter::ConvertTemplateFile()
{
	CString sKey;   // Key for Map
	CString sValue; // Value for Map
	enum SrcType
	{
		SrcNone = -1,
		SrcAutotext = 0,
		SrcTemplate = 1
	} srcType = SrcNone;
	bool bReadmeFlag = false;
	CString sSaveForNextItemDescription;

	CStdioFile f;
	// the conversion won't happen in a unicode path, so this is ok (since an old style file couldn't have been created
	// to begin with)
	if (!f.Open(CString(m_sTplFilePath), CFile::typeText | CFile::modeRead))
	{
		//		DWORD err = ::GetLastError();
		_ASSERTE(!"ConvertTemplateFile file open error");
		return false;
	}

	CString strText;
	CString sCaretPosChar; // The caret position character
	bool bBetweenColonAndFormFeed = false, bCaretPosChar = false;

	if (!f.ReadString(strText)) // Read the first line of the file into the variable
	{
		f.Close();
		return false; // Failed, so quit out
	}

	do // Repeat while there are lines in the file left to process
	{
		if (strText.GetLength() == 0)
		{
			// If line is empty then add an empty line
			if (bReadmeFlag)
				sSaveForNextItemDescription += "\n";
			else
				sValue += "\n";
			continue;
		}

		if (bCaretPosChar == true)
		{
			bCaretPosChar = false;
			sCaretPosChar = strText.GetAt(0);
			continue;
		}

		const int nColonFind = strText.Find(":");
		// Is it the start of a definition
		if ((nColonFind >= 0) && (bBetweenColonAndFormFeed == false))
		{
			bBetweenColonAndFormFeed = true;
			if (nColonFind == 0)
			{
				// This a colon by itself on a line
				// Error condition
				continue;
			}

			const CString sLeftOfColon(strText.Left(nColonFind));
			// Is it readme:
			if (sLeftOfColon == "readme")
			{
				// Ignore until \f
				sKey = sLeftOfColon;
				srcType = SrcAutotext;
				bReadmeFlag = true;
				continue;
			}
			else if (sLeftOfColon == "caretPosChar")
			{
				// Get the first char on the next line
				bCaretPosChar = true;
				sKey.Empty();
				srcType = SrcAutotext;
				continue;
			}
			else
			{
				// It's either autotext or codetemplate
				const int nSpace = sLeftOfColon.Find(' ');
				if (nSpace >= 0)
				{
					// It has spaces so it's a code template
					// Read until the next \f
					sKey = sLeftOfColon;
					srcType = SrcTemplate;
				}
				else
				{
					// It's autotext
					// Read until the next \f
					sKey = sLeftOfColon;
					srcType = SrcAutotext;
				}
			}
		}
		else
		{
			// No Colon Found

			// How about FormFeed
			const int nFormFeed = strText.Find("\f");

			// How about a separator
			const int nSep = strText.Find("separator");
			if (nSep >= 0)
			{
				// Just a separator
				sKey = strText;
				// Ignore separators
				sKey.Empty();
				srcType = SrcAutotext;
				continue;
			}
			else if (nFormFeed > 0)
			{
				// This line has a formfeed
				const CString sLeftOfFormFeed(strText.Left(nFormFeed));
				if (bReadmeFlag)
				{
					sSaveForNextItemDescription += sLeftOfFormFeed;
					bReadmeFlag = false;
				}
				else
				{
					sValue += sLeftOfFormFeed;
				}
				bBetweenColonAndFormFeed = false;
			}
			else if (nFormFeed == 0)
			{
				// Just a formfeed by itself
				bBetweenColonAndFormFeed = false;
				// If I still have a readme flag when I find a colon make it false
				if (bReadmeFlag)
					bReadmeFlag = false;
			}
			else
			{
				if (bReadmeFlag == true)
				{
					// Store readme for later use
					if (strText == kOldTemplateReadme1 || strText == kOldTemplateReadme2 ||
					    strText == kOldTemplateReadme3)
					{
						// Reset string
						strText.Empty();
						sKey.Empty();
						sValue.Empty();
						continue;
					}
					else
					{
						sSaveForNextItemDescription += strText + "\n";
						// Reset string
						strText.Empty();
						sKey.Empty();
						sValue.Empty();
					}
				}
				else
				{
					sValue += strText + "\n";
				}
			}
		}

		if (!bBetweenColonAndFormFeed)
		{
			// Is this a formfeed only
			if (sKey == "" && sValue == "")
				continue;

			if (SrcAutotext == srcType || SrcTemplate == srcType)
			{
				CString sTitle, sShortcut;

				if (SrcAutotext == srcType)
					sTitle.Empty();
				else
					sTitle = sKey.Trim();

				// If sKey has a space in it then make it null because
				// spaces are not allowed in shortcuts
				if (sKey.Find(_T(' ')) == -1)
					sShortcut = sKey.Trim();
				else
					sShortcut.Empty();

				sValue.Replace(sCaretPosChar, "$end$");
				sValue.Replace("%0", "$selected$");
				sValue.Replace("%1", "$1$");
				sValue.Replace("%2", "$2$");
				sValue.Replace("%3", "$3$");
				sValue.Replace("%4", "$4$");
				sValue.Replace("%5", "$5$");
				sValue.Replace("%6", "$6$");
				sValue.Replace("%7", "$7$");
				sValue.Replace("%8", "$8$");
				sValue.Replace("%DATE%", "$DATE$");
				sValue.Replace("%DAY%", "$DAY$");
				sValue.Replace("%DAY_02%", "$DAY_02$");
				sValue.Replace("%DAYNAME%", "$DAYNAME$");
				sValue.Replace("%FILE%", "$FILE$");
				sValue.Replace("%FILE_BASE%", "$FILE_BASE$");
				sValue.Replace("%FILE_EXT%", "$FILE_EXT$");
				sValue.Replace("%FILE_PATH%", "$FILE_PATH$");
				sValue.Replace("%HOUR%", "$HOUR$");
				sValue.Replace("%HOUR_02%", "$HOUR_02$");
				sValue.Replace("%MINUTE%", "$MINUTE$");
				sValue.Replace("%MONTH%", "$MONTH$");
				sValue.Replace("%MONTH_02%", "$MONTH_02$");
				sValue.Replace("%MONTHNAME%", "$MONTHNAME$");
				sValue.Replace("%SCOPE_LINE%", "$SCOPE_LINE$");
				sValue.Replace("%SECOND%", "$SECOND$");
				sValue.Replace("%YEAR%", "$YEAR$");

				bReadmeFlag = false;
				if (sSaveForNextItemDescription.GetLength())
				{
					m_sOutputBuffer += "readme:\n" + sSaveForNextItemDescription + "\f\n";
					sSaveForNextItemDescription.Empty();
				}
				m_sOutputBuffer += "a:" + sTitle + ":" + sShortcut + ":\n" + sValue + "\f\n";
			}
			else
				srcType = SrcNone; // Reinitialize it to neither autotext or codetemplate

			// Reset Key & Value
			sKey.Empty();
			sValue.Empty();
		}
	} while (f.ReadString(strText));

	f.Close();
	m_sOutputBuffer.Replace("\r", "");
	return true;
}

void CAutoTemplateConverter::Save()
{
	CFileW file;

	if (!file.Open(m_sTplFilePath, CFile::modeCreate | CFile::modeWrite))
	{
		_ASSERTE(!"Save file open error");
		return;
	}

	file.Write(m_sOutputBuffer, (UINT)m_sOutputBuffer.GetLength());
	file.Close();
}
