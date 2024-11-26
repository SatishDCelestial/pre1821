#include "stdafxed.h"
#include "EdCnt.h"
#include "VATree.h"
#include "Settings.h"
#include "fdictionary.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

void SetClipText(const WTString& txt, UINT type /*= CF_UNICODETEXT*/)
{
	HWND h = ::GetFocus(); // ::GetDesktopWindow();
	if (::OpenClipboard(h))
	{
		const CStringW txtW(txt.Wide());
		const uint len = (txtW.GetLength() + 1) * sizeof(WCHAR);
		HGLOBAL hData = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, len);
		if (hData)
		{
			LPVOID pTxt = GlobalLock(hData);
			if (pTxt)
			{
				memcpy(pTxt, (LPCWSTR)txtW, len);
				EmptyClipboard();
				SetClipboardData(type, hData);
			}

			CloseClipboard();
			GlobalUnlock(hData);
		}
		else
			CloseClipboard();
	}
}

WTString RTFColor(COLORREF clr)
{
	WTString clrStr;
	clrStr.WTFormat("\\red%ld\\green%ld\\blue%ld", (clr)&0xff, (clr >> 8) & 0xff, (clr >> 16) & 0xff);
	return clrStr;
}

WTString RTFCopy2(EdCnt* ed, WTString* txtToFormat)
{
	WTString txt;
	if (txtToFormat && txtToFormat->GetLength())
		txt = *txtToFormat;
	else
	{
		txt = g_VATabTree ? g_VATabTree->GetClipboardTxt() : ""; // GetClipTxt();
		if (txt.IsEmpty())
			return "";
	}
	const int len = txt.GetLength();
	if (len)
	{
		LPCSTR p = txt.c_str();
		int i = 0;
		WTString rtf;
		//		rtf.AllocBuffer(txt.GetLength()*2);
		// rtf = "{\\rtf1 {\\colortbl ;\\red0\\green0\\blue0;\\red255\\green0\\blue0;}";
		rtf = WTString("{\\rtf1 {\\colortbl ;") + RTFColor(Psettings->m_colors[C_Text].c_fg) + ";" +
		      RTFColor(Psettings->m_colors[C_Type].c_fg) + ";" + RTFColor(Psettings->m_colors[C_Comment].c_fg) + ";" +
		      RTFColor(Psettings->m_colors[C_Var].c_fg) + ";" + RTFColor(Psettings->m_colors[C_Function].c_fg) + ";" +
		      RTFColor(Psettings->m_colors[C_Macro].c_fg) + ";" + RTFColor(Psettings->m_colors[C_String].c_fg) + ";" +
		      RTFColor(Psettings->m_colors[C_Number].c_fg) + ";" + RTFColor(Psettings->m_colors[C_Operator].c_fg) +
		      ";" + RTFColor(Psettings->m_colors[C_Keyword].c_fg) + ";}\\pard\\cf1 ";
		(void)rtf.GetBuffer(10 * len);
		WTString cwd, tmpRtf;
		(void)cwd.GetBuffer(256);
		(void)tmpRtf.GetBuffer(512);
		int lst_i = -1;
		while (i < len)
		{
			if (i == lst_i)
			{
				ASSERT(FALSE); // someone did not increment i
				i++;
			}
			lst_i = i;

			tmpRtf = "";
			if (ISCSYM(p[i]))
			{
				cwd = "";
				for (; i < len && ISCSYM(p[i]); i++)
					cwd += p[i];
				if (wt_isdigit(cwd[0]))
				{
					tmpRtf += "\\cf8 ";
				}
				else
				{
					MSG msg;
					if (PeekMessage(&msg, *ed, WM_KEYDOWN, WM_KEYDOWN, PM_NOREMOVE) ||
					    PeekMessage(&msg, *ed, WM_LBUTTONDOWN, WM_MBUTTONDBLCLK, PM_NOREMOVE))
					{
						return "";
					}
					MultiParsePtr mp(ed->GetParseDb());
					uint type = mp->LDictionary()->FindAny(cwd);
					if (!type)
						type = g_pGlobDic->FindAny(cwd);
					if (!type)
						type = GetSysDic()->FindAny(cwd);
					switch (type)
					{
					case DEFINE:
						if (Psettings->m_ActiveSyntaxColoring)
							tmpRtf += "\\cf6 ";
						else
							tmpRtf += "\\cf1 ";
						break;
					case VAR:
					case FUNC:
						if (Psettings->m_ActiveSyntaxColoring)
						{
							int x;
							for (x = i; x < len && (ISCSYM(p[x]) || wt_isspace(p[x])); x++)
								;
							if (p[x] == '(')
								tmpRtf += "\\cf5 ";
							else
								tmpRtf += "\\cf4 ";
						}
						else
						{
							tmpRtf += "\\cf1 ";
						}
						break;
					case CLASS: // added for local typedefs
						if (!Psettings->m_ActiveSyntaxColoring)
							tmpRtf += "\\cf1 "; // text
						else
							tmpRtf += "\\cf2 "; // classnames
						break;
					case TYPE:
					case RESWORD:
						tmpRtf += "\\cf10 "; // keyword
						break;
					default:
						tmpRtf += "\\cf1 ";
					}
				}
				tmpRtf += cwd;
				tmpRtf += "\\cf1 ";
			}
			else
			{
				switch (p[i])
				{
				case '/':
					if (p[i + 1] == '/')
					{ // comment line
						tmpRtf += "\\cf3 ";
						for (; i < len && p[i] != '\n'; i++)
						{
							if (strchr("\\{}", p[i]))
								tmpRtf += '\\';
							if (p[i] != '\r') // fix unix comments
								tmpRtf += p[i];
						}
						tmpRtf += "\\cf1 ";
					}
					else if (p[i + 1] == '*')
					{
						tmpRtf += "\\cf3 ";
						for (; i < len && strncmp("*/", &p[i], 2); i++)
						{
							if (strchr("\\{}", p[i]))
								tmpRtf += '\\';
							else if (p[i] == '\n')
								tmpRtf += "\\par";
							tmpRtf += p[i];
						}
						if (strncmp("*/", &p[i], 2) == 0)
						{
							tmpRtf += "*/";
							i += 1;
						}
						tmpRtf += "\\cf1 ";
						i++;
					}
					else
					{
						tmpRtf += "/";
						i++;
					}
					break;
				case '"':
				case '\'': {
					char c = p[i];
					tmpRtf += "\\cf7 ";
					if (i < len)
					{
						tmpRtf += c;
						i++;
					}
					for (; i < len && p[i] != c; i++)
					{
						if (p[i] == '\\')
						{ // ignore next char
							tmpRtf += "\\\\";
							i++;
						}
						if (strchr("\\{}", p[i]))
							tmpRtf += '\\';
						else if (p[i] == '\n')
							tmpRtf += "\\par";
						tmpRtf += p[i];
					}
					if (i < len)
					{ // get closing quote
						tmpRtf += c;
						i++;
					}
				}
				break;
				default: {
					tmpRtf += "\\cf9 ";
					if (p[i] == '#')
					{
						tmpRtf += "\\cf2 ";
						tmpRtf += p[i++];
						for (; i < len && (p[i] == ' ' || p[i] == '\t'); i++) // not past cr/lf
							tmpRtf += p[i];
						for (; i < len && ISCSYM(p[i]); i++)
							tmpRtf += p[i];
					}
					else if (p[i] == '\n')
						tmpRtf += "\\par";
					else if (strchr("\\{}", p[i]))
						tmpRtf += '\\';

					if (p[i] == '\t')
						tmpRtf += "\\tab ";
					else
						tmpRtf += p[i];
					i++;
				}
				}
			}
			rtf += tmpRtf;
		}
		rtf += "}";
		return rtf;
	}
	return "";
}
void RTFCopy(EdCnt* ed, WTString* txtToFormat)
{
	WTString rtf = RTFCopy2(ed, txtToFormat);
	if(!rtf.IsEmpty())
	{
		static const CLIPFORMAT cfRTF = (CLIPFORMAT)::RegisterClipboardFormat(CF_RTF);
		SetClipText(rtf, cfRTF);
	}
}
