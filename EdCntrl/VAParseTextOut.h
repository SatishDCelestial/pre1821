//////////////////////////////////////////////////////////////////////////
// Provides ExtTextOut that does enhanced syntax coloring

#include "FontSettings.h"
#include "SyntaxColoring.h"
#include "..\Common\TempAssign.h"

extern BOOL g_inPaint;
extern void UnderLine(HDC dc, int x, int y, int x2, COLORREF clr, BOOL isComment = TRUE);

#define WIDE_TO_CHAR(wc) ((wc & 0xff00) ? ('\x80') : (char)wc) // Changed to 0x80 so ISCSYM return true case:16927

class DCState2
{
  public:
	HDC dc;
	HFONT font;
	COLORREF fgColor;
	COLORREF bgColor;
	uint textalign;
	int bkmode;
	CPoint curPT;
	void SaveState(HDC _dc)
	{
		this->dc = _dc;
		font = (HFONT)::GetCurrentObject(dc, OBJ_FONT);
		bgColor = GetBkColor(dc);
		fgColor = GetTextColor(dc);
		textalign = ::GetTextAlign(dc);
		bkmode = ::GetBkMode(dc);
		curPT.SetPoint(0, 0);
		::GetCurrentPositionEx(dc, &curPT);
	}
	void RestoreState()
	{
		::SelectObject(dc, font);
		SetBkColor(dc, bgColor);
		SetTextColor(dc, fgColor);
		::SetTextAlign(dc, textalign);
		::SetBkMode(dc, bkmode);
	}
};

class FontColorCls
{
	BYTE FACTORCOLOR(BYTE c, INT factor)
	{
		int clr = c + factor /*-(255/2)*/;
		if (clr > 255)
			clr = 255;
		if (clr < 0)
			clr = 0;
		return (BYTE)clr;
	}
	COLORREF ADDCOLOR(COLORREF clr, int factor)
	{
		return RGB(FACTORCOLOR(GetRValue(clr), factor), FACTORCOLOR(GetGValue(clr), factor),
		           FACTORCOLOR(GetBValue(clr), factor));
	}

  public:
	FontColorCls() = default;
	virtual ~FontColorCls() = default;
	FontColorCls(const FontColorCls &) = default;
	FontColorCls& operator=(const FontColorCls &) = default;

	enum
	{
		STYLE_NONE = 0,
		STYLE_ITALIC = 0x1,
		STYLE_BOLD = 0x2,
		STYLE_UNDERLINED = 0x4,
		STYLE_UNDERLINED_SPELLING = 0x8
	};
	int m_FontStyle;
	virtual BOOL ShouldColor() = 0;
	void SetDTypeColor(HDC dc, uint type, uint attrs)
	{
		int style = STYLE_NONE;
		int colorIdx = C_Text;
		DType dt;
		dt.setType(type, attrs, 0);
		switch (dt.MaskedType())
		{
		case CONSTANT:
		case DEFINE:
			colorIdx = C_Macro;
			break;
		case LINQ_VAR:
		case VAR:
		case PROPERTY:
			colorIdx = C_Var;
			break;
		case FUNC:
		case GOTODEF: // not right for vars but better than having GOTODEFs for both vars and funcs be uncolored
			colorIdx = C_Function;
			break;
		case CLASS: // added for local typedefs
		case TEMPLATETYPE:
		case TYPE:
		case STRUCT:
		case C_ENUM:
		case MODULE:
		case MAP:
		case C_INTERFACE:
		case EVENT:
			colorIdx = C_Type;
			break;
		case RESWORD:
			colorIdx = C_Keyword;
			break;
		case COMMENT:
			colorIdx = C_Comment;
			break;
		case STRING:
			colorIdx = C_String;
			break;
		case OPERATOR:
			colorIdx = C_Operator;
			break;
		case NUMBER:
			colorIdx = C_Number;
			break;
		case C_ENUMITEM:
			colorIdx = C_EnumMember;
			break;
		case NAMESPACE:
			colorIdx = C_Namespace;
			break;
		}
		if (IsPaintType(PaintType::SourceWindow) || !dc)
		{
			if (dt.HasLocalFlag() && colorIdx == C_Var && Psettings->m_bLocalSymbolsInBold)
				style = STYLE_BOLD;
			if (dt.IsSysLib() && Psettings->m_bStableSymbolsInItalics)
				style = STYLE_ITALIC;
		}
		SetIdxColor(dc, colorIdx, style | m_FontStyle);
	}
	virtual void SetIdxColor(HDC dc, int clrIdx, int style = 0)
	{
		COLORREF clr = Psettings->m_colors[clrIdx].c_fg;
		m_FontStyle = style;
		if (IsPaintType(PaintType::SourceWindow))
		{
			if (style == STYLE_BOLD && clrIdx == C_Var && Psettings->m_bLocalSymbolsInBold)
			{
				COLORREF bgclr = Psettings->m_colors[clrIdx].c_bg;
				ULONG avgBgClr = ULONG((GetRValue(bgclr) + GetRValue(bgclr) + GetRValue(bgclr)) / 3);
				ULONG avgClr = ULONG((GetRValue(clr) + GetRValue(clr) + GetRValue(clr)) / 3);
				if (avgClr > avgBgClr)
					clr = ADDCOLOR(clr, 30);
				else
					clr = ADDCOLOR(clr, -30);
				SetTextColor(dc, clr); // Regardless of m_bEnhColorSourceWindows?
				return;                // No coloring, just italic and bold
			}
			if (!Psettings->m_bEnhColorSourceWindows)
				return; // No coloring, just italic and bold
		}
		else if (C_Text == clrIdx || C_Undefined == clrIdx || C_String == clrIdx)
		{
			// [case: 14268] don't output text that is same color as window background
			//			DWORD wndBgClr = ::GetSysColor(COLOR_WINDOW);
			DWORD wndBgClr = ::GetBkColor(dc);
			DWORD colorToCompare = clr;

			if (C_Text == clrIdx && gShellAttr->IsDevenv())
			{
				static DWORD sPreviousColorReverseHacked = 0xffffffff;
				static DWORD sReverseHackColorForCompare = 0;
				if (clr != sPreviousColorReverseHacked)
				{
					sReverseHackColorForCompare = sPreviousColorReverseHacked = clr;
					// need to reverse the VA color hack performed in Connect.cpp
					if ((clr & 0xff) == 0xff)
						;
					else if ((clr & 0xff) == 0xfe)
						sReverseHackColorForCompare++;
					else
						sReverseHackColorForCompare--;

					const byte bval = (byte)((sReverseHackColorForCompare >> 16) & 0xff);
					if ((bval & 0xff) == 0xff)
						;
					else if (bval == 0xfe)
						sReverseHackColorForCompare = (sReverseHackColorForCompare & 0xffff) | ((0xff) << 16);
					else
						sReverseHackColorForCompare = (sReverseHackColorForCompare & 0xffff) | ((bval - 1) << 16);
				}

				colorToCompare = sReverseHackColorForCompare;
			}

			if (colorToCompare == wndBgClr)
			{
				DWORD fg;
				if (IsPaintType(PaintType::ToolTip) || IsPaintType(PaintType::VS_ToolTip))
					fg = ::GetSysColor(COLOR_INFOTEXT);
				else
					fg = ::GetSysColor(COLOR_WINDOWTEXT);

				if (fg != wndBgClr)
					clr = fg;
			}
		}

		// Do we support reverse colors?
		//		SetBkColor(dc, Psettings->m_colors[clrIdx].c_bg);
		SetTextColor(dc, clr);
	}
};
// Use VAParse to parse the line
class VAParseExtTextOutCls : public ReadToCls, public FontColorCls
{
  private:
	static MultiParsePtr s_mp;

  protected:
	uint m_dtype;
	uint mAttrs;
	uint m_refFlag;
	DCState2 m_savestate;
	int m_x;
	int m_y;
	MultiParsePtr m_overrideMp;

  public:
	VAParseExtTextOutCls(int fType) : ReadToCls(fType), m_dtype(0), mAttrs(0)
	{
		SetDType(CTEXT, 0);
	}

	static void ClearStatic()
	{
		if (!s_mp)
			return;

		s_mp->ClearCwData();
		s_mp->InitScopeInfo();
		s_mp = nullptr;
	}

	void OverrideMp(MultiParsePtr mp)
	{
		m_overrideMp = mp;
	}

  protected:
	virtual void OnChar()
	{
	}
	virtual WTString GetCStr(LPCSTR str) = 0; // { return ::GetCStr(str); };

	virtual void OnCSym()
	{
		if (Psettings->mUnrealEngineCppSupport && !Psettings->mColorInsideUnrealMarkup)
		{
			// check if inside U* macro [case: 109053]
			for (ULONG i = 1; Depth() >= i && i < 4; ++i)
			{
				const LPCSTR lastScopePos = State(Depth() - i).m_lastScopePos;

				if (lastScopePos != nullptr && lastScopePos[0] == 'U')
				{
					if (StartsWith(lastScopePos, "UINTERFACE") || StartsWith(lastScopePos, "UCLASS") ||
					    StartsWith(lastScopePos, "USTRUCT") || StartsWith(lastScopePos, "UFUNCTION") ||
					    StartsWith(lastScopePos, "UDELEGATE") || StartsWith(lastScopePos, "UENUM") ||
					    StartsWith(lastScopePos, "UPROPERTY") || StartsWith(lastScopePos, "UPARAM") ||
					    StartsWith(lastScopePos, "UMETA"))
					{
						// do not color inside U* macros
						SetDType(CTEXT);
						return;
					}
				}
			}
		}

		WTString sym = GetCStr(CurPos());
		if (sym == "of" && wt_isdigit(State().m_lastWordPos[0])) // Case 15809
		{
			// catch [1 of 3] in vsnet tooltips.
			SetDType(CTEXT);
			return;
		}
		if (sym.length() && sym[0] >= '0' && sym[0] <= '9')
		{
			bool isNum = true;
			// [case: 14570] GetCStr might rewind too far and read garbage
			// ensure really is a number before committing
			for (int idx = 0; isNum && idx < 10 && idx < sym.GetLength(); ++idx)
			{
				const char curCh = sym[idx];
				if (::wt_isxdigit(curCh) || strchr("XxnsmhinflulL.", curCh))
					; // [case: 95006] suffixes: ns ms us s h min u l L f		prefix: x X
				else if (::wt_ispunct(curCh) || ::wt_isspace(curCh))
					break; // space or other separator
				else
					isNum = false;
			}

			if (isNum)
			{
				SetDType(NUMBER);
				return;
			}
		}
		if (sym[0] == '~' || (sym[0] == '!' && ISCSYM(sym[1])))
			sym = sym.Mid(1);
		if (!sym.length() || !ISCSYM(sym[0]))
		{
			SetDType(NULL);
			return;
		}

		if (sym.GetLength() == 1 && sym[0] == 'f' && m_dtype == NUMBER && !InComment() &&
		    (!m_cp || m_buf[m_cp - 1] == '.'))
		{
			// [case: 95006]
			// 1.f
			return;
		}

		MultiParsePtr mp = m_overrideMp ? m_overrideMp : (g_currentEdCnt ? g_currentEdCnt->GetParseDb() : nullptr);
		if (!mp)
		{
			if (!s_mp)
				s_mp = MultiParse::Create(Src);
			mp = s_mp;
		}
		WTString bcl = mp->GetGlobalNameSpaceString();
		if (IsXref() && State().m_lwData)
			bcl = mp->GetBaseClassListCached(State().m_lwData->SymScope()) + "\f" + bcl;
	again:
		// for coloring, don't need to check for invalid system files, we're just coloring
		DType* cd = IsXref() ? mp->FindSym(&sym, NULL, &bcl, FDF_NoInvalidSysCheck) : NULL; // Case 15312
		if (cd)
			State().m_lwData = std::make_shared<DType>(cd);
		else
			State().m_lwData = mp->FindBestColorType(sym);

		// No longer needed since adding a check to GetCStr preventing it from reading g_GlyfBuffer.m_p2
		// Causes "Unknown_Base" to be painted italic in case 15883
		// This section can be removed once it passes testing...
		// 		// When typing in VS200x, the buffer to the right of the CurPos()is invalid garbage.
		// 		// This is a hack(from the old logic) to check just to the end of buffer len
		// 		// We should use the Glyph logic to detect the real end of valid text.
		// 		if(!againLoop && !State().m_lwData && (m_cp + sym.GetLength()) > m_maxLen)
		// 		{
		// 			// Only get to end of buf;
		// 			sym = sym.Left(m_maxLen - m_cp);
		// 			againLoop++;
		// 			goto again;
		// 		}
		if (!State().m_lwData && m_cp == 0 && sym != ::GetCStr(CurPos()))
		{
			// Only get to beginning of buf;
			sym = ::GetCStr(CurPos());
			goto again;
		}

		if (State().m_lwData)
		{
			// check for A/W // from DSFormat.cpp:GetCwdColor()
			if (State().m_lwData->MaskedType() == DEFINE && State().m_lwData->IsDbCpp())
			{
				DTypePtr cd2 = mp->FindBestColorType(sym + "W");
				if (cd2)
					State().m_lwData = cd2;
			}
			SetDType(State().m_lwData->MaskedType(), State().m_lwData->Attributes());
		}
		else
		{
			if (IsDef(m_deep) && InParen(m_deep))
				SetDType(VAR);
			else if (IsPaintType(PaintType::SourceWindow))
			{
				if (Psettings->mUnrealEngineCppSupport && sym.GetLength() > 4 && ::StrIsUpperCase(sym))
				{
					// [case: 114511]
					// in editor and if all upper, only guess macro if ue4 support is enabled
					SetDType(DEFINE);
				}
				else
					SetDType(CTEXT);
			}
			else
			{
				// guess color in non-editor windows
				if (::StrIsUpperCase(sym))
				{
					// [case: 114511]
					SetDType(DEFINE);
				}
				else if (sym.GetLength() && (m_cp + sym.GetLength()) < mBufLen && '(' == m_buf[m_cp + sym.GetLength()])
				{
					// this is a hack to fix coloring of identifiers that use non-ascii characters.
					// problems in StrFromWStr and WIDE_TO_CHAR need to be fixed to fix parse so that State().m_lwData
					// gets populated. in the meantime, check for '(' and guess FUNC instead of VAR if present. Doesn't
					// fix alt+m when parameters are filtered from list but helps outline
					SetDType(FUNC);
				}
				else
				{
					// case: 15048, case 15046, case 15036
					SetDType(VAR);
				}
			}
		}

		if (!Psettings->mUseMarkerApi && g_inPaint)
		{
			EdCntPtr ed(g_currentEdCnt);
			if (ed)
			{
				// Highlight references
				FindReferencesPtr globalRefs(g_References);
				if (globalRefs && globalRefs->m_doHighlight && globalRefs->GetFindSym() == sym)
				{
					MultiParsePtr mp2(ed->GetParseDb());
					_ASSERTE(g_FontSettings->GetCharHeight() && "$$MSD1");
					const ULONG ln = (ULONG)mp2->m_firstVisibleLine +
					                 ((m_y ? m_y : m_savestate.curPT.y) / g_FontSettings->GetCharHeight());
					const FREF_TYPE ref = globalRefs->IsReference(g_currentEdCnt->FileName(), ln, (ULONG)m_x);
					switch (ref)
					{
					case FREF_None:
						break;
					case FREF_DefinitionAssign:
					case FREF_ReferenceAssign:
						m_refFlag = C_ReferenceAssign;
						break;
					default:
						m_refFlag = C_Reference;
						break;
					}
				}
			}
		}
	}

	virtual void OnComment(char c, int altOffset = UINT_MAX) override
	{
		if (!c && IsDone())
			return; // Ignore the clear comments at the end of _DoParse
		__super::OnComment(c, altOffset);
		if (c == '#')
			SetDType(RESWORD);
		else if (c == '"' || c == '\'')
			SetDType(STRING);
		else if (c)
		{
			_ASSERTE(c != 'L' && c != '@' && c != 'S');
			if (c == '[')
			{
				// [case: 112204]
				// C++ [[attributes]] are treated as comment except for coloring
				SetDType(CTEXT);
			}
			else
				SetDType(COMMENT);
		}
		else
			SetDType(CTEXT);
		m_InSym = FALSE; // case: 13373
	}

	void ReallyClearComment()
	{
		// [case: 45866]
		if (CommentType())
		{
			// don't call OnComment(NULL) - it won't, so skip to __super
			__super::OnComment(NULL);
			SetDType(CTEXT);
			m_InSym = FALSE; // case: 13373
		}
	}

	void SetDType(uint type, uint attrs = 0)
	{
		if (m_dtype != type || mAttrs != attrs)
		{
			FlushTextOut();
			m_dtype = type;
			mAttrs = attrs;
		}
		m_refFlag = NULL;
	}
	virtual void IncCP()
	{
		if (CommentType() == '#')
		{
			// color # directive text
			if (m_dtype == RESWORD && wt_isspace(*CurPos()))
			{
				// can't rely on DT_INCLUDE == m_inDirectiveType
				if (StartsWith(State().m_begLinePos, "#include"))
					SetDType(STRING); // [case: 14690] don't clear comment in #include directives in vc6
				else
					OnComment('\0'); // SetDType(CTEXT);
			}
		}
		else if (IsAtDigitSeparator())
		{
			// [case: 86379]
			SetDType(NUMBER);
		}
		else if (!InComment() && m_buf[m_cp] == '.' && m_buf[m_cp + 1] == 'f' &&
		         (!m_cp || ::wt_isdigit(m_buf[m_cp - 1])))
		{
			// [case: 95006]
			SetDType(NUMBER);
		}
		else if (!InComment() && m_buf[m_cp] != '~' &&
		         (m_buf[m_cp] != '!' || (m_buf[m_cp] == '!' && !ISCSYM(m_buf[m_cp + 1]))) &&
		         (!m_InSym || !ISCSYM(m_buf[m_cp])))
		{
			char curCh = CurChar();
			if (wt_isspace(curCh))
			{
				// [case: 14579] fix for vc6 visible whitespace causing coloring problems
				SetDType(CTEXT);
				m_InSym = false;
			}
			else if ('\"' == curCh || '\'' == curCh)
				SetDType(STRING); // [case: 14690] fix for vc6 coloring of string terminator
			else if ('s' == curCh && '\"' == PrevChar())
				SetDType(STRING); // [case: 95006][case: 65734]
			else if ('/' == curCh && m_cp && '*' == m_buf[m_cp - 1] && CTEXT == m_dtype)
			{
				// [case: 14764] fix for coloring of last /  in  /* */
				// the parser ends the comment on the terminating '*' rather than the '/'
				// so InComment is false at this point
				SetDType(COMMENT);
			}
			else
				SetDType(OPERATOR);
		}

		// If SrcWin is scrolled right, check to see that our state is correct
		if (IsPaintType(PaintType::SourceWindow) && gShellAttr && gShellAttr->IsMsdev() && InComment() &&
		    State().m_inComment != '#')
		{
			// case:16880 and case:16454 this block is only for vc6 and does not
			// apply to preproc directives per case:14690
			// watch out for strings colored the same as text
			if (m_savestate.fgColor != Psettings->m_colors[C_Comment].c_fg &&
			    m_savestate.fgColor != Psettings->m_colors[C_String].c_fg &&
			    Psettings->m_colors[C_String].c_fg != Psettings->m_colors[C_Text].c_fg)
			{
				OnComment(NULL);
				OnCSym();
			}
		}

		if (InComment())
		{
			// Handle word breaks in comments for underlining
			if (ISCSYM(CurChar()))
			{
				if (!m_InSym)
					FlushTextOut(); // flush before word
				m_InSym = TRUE;
			}
			else if (m_InSym)
			{
				FlushTextOut(); // flush after word
				m_InSym = FALSE;
			}
		}

		if (CurChar() == '_' && gShellAttr->IsMsdev())
		{
			// Split _'s for to fix missing italic _'s in vc6
			FlushTextOut();
			__super::IncCP();
			FlushTextOut();
		}
		else if (!InComment() && strchr("()[]{}<>", m_buf[m_cp]))
		{
			// [case: 14690 and case: 15050] fix for italic or bold first brace and incorrect bg color of first quote in
			// #define foo _T("fff")
			// int arg[];	// if locals in bold is enabled
			m_FontStyle = NULL;

			if (']' == CurChar() && ']' == PrevChar())
			{
				// [case: 112204] [[attributes]] treated as comment but not for coloring
				SetDType(CTEXT);
			}
			else
			{
				// Split ((('s for bold bracing
				SetDType(OPERATOR);
			}
			FlushTextOut();
			__super::IncCP();
			FlushTextOut();
		}
		else
			__super::IncCP();
		if (!!m_InSym != ISCSYM(CurChar()))
		{
			// Flush at beginning and end of all csyms
			// This should make some of the other Flushes unneeded, but leaving in for now.
			FlushTextOut();
			m_FontStyle = NULL; // Reset any underline if any // Case 15026
			m_InSym =
			    FALSE; // Case 15438, _DoParse doesn't clear this after first ':', (should clear after every non-csym)
		}

		if (!m_InSym && m_FontStyle)
			m_FontStyle = NULL;

		if (!InComment() && !ISCSYM(CurChar()) && !wt_isspace(CurChar()))
		{
			// [case: 43695] we are in IncCp and IsDone can decrement cp, so watch out for it
			const int curPos = m_cp;
			const BOOL isDone = IsDone();
			if (m_cp < curPos)
				m_cp = curPos; // restore change made by IsDone

			if (!isDone)
				SetDType(OPERATOR);
		}
	}

  protected:
	virtual void ExtTextOutDType(LPCSTR str, int l, uint dtype, uint attrs)
	{
	}
	virtual void FlushTextOut()
	{
	}
};

class ExtTextOutCls : public VAParseExtTextOutCls
{
  protected:
	HDC m_dc;
	UINT m_style;
	LPCSTR m_str;
	const USHORT* m_wPos;
	LPCWSTR m_wstr;
	int m_len;
	LPCRECT m_rc;
	CONST INT* m_w;
	int m_rval;
	int m_LastPrintPos;
	int m_wasItalic;
	BOOL m_canColor;
	HDC mSkipDc;
	CPoint mSkipPos;

  public:
	ExtTextOutCls(int ftype) : VAParseExtTextOutCls(ftype)
	{
		m_LastPrintPos = 0;
		m_wasItalic = -1;
		m_wstr = NULL;
		m_wPos = NULL;
		mSkipDc = NULL;
		mSkipPos.SetPoint(0, 0);
	}

	LPCSTR StrFromWStr(LPCSTR str, int& len, LPCWSTR wstr, ULONG& wholeLen)
	{
		if (wstr)
		{
			// Allow displaying of wide strings
			// Get lower bits and store them in a static buffer for parser
#define BLEN 4096 // case:118872 - increased 4 times so we can hold 4 bytes per Unicode glyph
			static CHAR buf[BLEN + 1];
			static USHORT wPos[BLEN + 1];
			uint lp = (uint)m_cp;
			if (wstr != m_wstr + lp)
			{
				m_wstr = wstr;
				m_wPos = wPos;
				lp = 0;
			}
			str = &buf[lp];
			int wp = 0;
			try
			{
				int u8lenSum = 0;
				int u8len = 1;
				for (;
				     wstr[wp] &&
				     (wp <= len || wstr[wp] == '~' || (wstr[wp] == '!' && ISCSYM(wstr[wp + 1])) || ISCSYM(wstr[wp])) &&
				     lp + 4 < BLEN;
				     lp += u8len, wp++)
				{
					// case:118872 - instead of WIDE_TO_CHAR, we translate Unicode parts into UTF8
					// note: WIDE_TO_CHAR just replaces all Unicode chars with a 0x80

					if (wstr[wp] >= 0x80) // Unicode as UTF8
					{
						if (IsPaintType(PaintType::SourceWindow))
						{
							buf[lp] = WIDE_TO_CHAR(
							    wstr[wp]); // default behavior in source windows (applies to pre-vs2010 IDEs)
							u8len = 1;
						}
						else
						{
							if (IS_SURROGATE_PAIR(wstr[wp], wstr[wp + 1]))
							{
								// Max 4 bytes per code point
								// Max valid UTF-32 code point is U+10FFFF, which is maximum for representation with
								// surrogate pair
								u8len = ::WideCharToMultiByte(CP_UTF8, 0, &wstr[wp], 2, &buf[lp], 4, nullptr, nullptr);
								wp++;
							}
							else
							{
								u8len = ::WideCharToMultiByte(CP_UTF8, 0, &wstr[wp], 1, &buf[lp], 4, nullptr, nullptr);
							}

							if (u8len <= 0)
							{
								buf[lp] = WIDE_TO_CHAR(wstr[wp]); // default behavior if conversion failed
								u8len = 1;
							}
						}
					}
					else // ASCII as is
					{
						buf[lp] = (CHAR)wstr[wp];
						u8len = 1;
					}

					if (wp < len)
						u8lenSum += u8len;

					for (int x = 0; x < u8len; x++)
						wPos[lp + x] = (USHORT)wp;
				}

				len = u8lenSum; // case:118872 - length needs to be in utf8
			}
			catch (...)
			{
				// [case: 108622] ????
				// the for loop was originally added in change 10325
				// it has had problems since it was first created
				// read access violations of wstr appear to be frequent in vs2008 on win10
				// the entire glyf* mechanism seems awfully fragile
			}

			buf[lp] = '\0';
			wPos[lp] = (USHORT)wp;
			wholeLen = lp;
		}
		else
		{
			m_wstr = NULL;
			m_wPos = NULL;
			wholeLen = 0;
		}
		return str;
	}

	int ColorTextOut(HDC dc, int x, int y, UINT style, LPCSTR str, int len, LPCRECT rc, CONST INT* w, LPCWSTR wstr)
	{
		m_dc = dc;
		m_x = x;
		m_y = y;
		m_style = style;
		m_rc = rc;
		ULONG wholeLen;
		// Handle wide strings
		str = StrFromWStr(str, len, wstr, wholeLen);
		if (!wholeLen)
		{
			// [case: 58538]
			wholeLen = (ULONG)len;
		}
		m_str = str;
		m_len = len;
		m_w = w;
		m_rval = 0;

		if (IsPaintType(PaintType::SourceWindow) && rc && ShouldColor())
		{
			extern void GetFontInfo(HDC dc);
			GetFontInfo(m_dc);
			g_FontSettings->SetLineHeight(rc->bottom - rc->top);
		}

		// Set widths for vc6, so bold/italic fonts fit into same size
		INT* new_w = NULL;
		if (!w && IsPaintType(PaintType::SourceWindow))
		{
			new_w = new INT[(uint)len];
			int cwidth = g_vaCharWidth[' '];
			for (int i = 0; i < len; i++)
			{
				if (str[i] < 255)
					new_w[i] = g_vaCharWidth[(UINT)(str[i]) & 0xff];
				else
					new_w[i] = cwidth;
			}
			m_w = new_w;
		}
		ParseTextOut(wholeLen);
		if (new_w)
			delete[] new_w;
		return m_rval;
	}
	virtual void OnInitLine()
	{
	}
	void ParseTextOut(ULONG wholeBufLen)
	{
		m_savestate.SaveState(m_dc);
		if (m_savestate.bkmode != TRANSPARENT && !IsPaintType(PaintType::SourceWindow) &&
		    m_savestate.bgColor != Psettings->m_colors[C_Reference].c_bg // highlighting in Find References case 15973
		    && m_savestate.bgColor != Psettings->m_colors[C_ReferenceAssign].c_bg)
			SetBkMode(m_dc, TRANSPARENT); // Fixes clipped text: case 15562

		if (CurPos() == m_str)
		{
			// Resume from previous string
			mBufLen = m_maxLen = m_cp + m_len;
			if (InComment() && gShellAttr && gShellAttr->IsDevenv())
			{
				// If SrcWin is scrolled right, check to see that our state is correct
				// case:16880 and case:16454 this block is only for VS
				if (m_savestate.fgColor == Psettings->m_colors[C_Text].c_fg &&
				    m_savestate.bgColor == Psettings->m_colors[C_Text].c_bg)
				{
					OnComment(NULL);
				}
			}
			DoParse();
			m_cp = m_maxLen;
		}
		else if (IsPaintType(PaintType::ToolTip) && CommentType() == '*')
		{
			char inComment = State().m_inComment;
			Init(m_str, m_len);
			State().m_inComment = inComment;
			m_maxLen = m_len;
			m_LastPrintPos = 0;
			DoParse();
			m_cp = m_maxLen;
		}
		else
		{
			// Reset state for new line
			m_wasItalic = -1;
			m_LastPrintPos = 0;
			m_cp = 0;
			m_FontStyle = NULL;
			OnInitLine();
			ReadTo(m_str, (int)wholeBufLen, "", m_len);
		}
		FlushTextOut();
		m_savestate.RestoreState();
	}

	virtual BOOL ShouldColor()
	{
		if (IsPaintType(PaintType::SourceWindow) || IsPaintType(PaintType::FindInFiles))
		{
// VS200x, color marker foreground only if it is the default black.
#define DEFAULT_MARKER_CLR 0
			if (m_savestate.fgColor == DEFAULT_MARKER_CLR &&
			    (m_savestate.bgColor == Psettings->m_colors[C_FindResultsHighlight].c_bg ||
			     m_savestate.bgColor == Psettings->m_colors[C_Reference].c_bg ||
			     m_savestate.bgColor == Psettings->m_colors[C_ReferenceAssign].c_bg))
				return TRUE;

			return m_savestate.fgColor == Psettings->m_colors[C_Text].c_fg &&
			       m_savestate.bgColor == Psettings->m_colors[C_Text].c_bg;
		}
		if (IsPaintType(PaintType::VS_ToolTip))
		{
			if (m_y > 3)
				return FALSE; // only color first line of tips

			const WTString ctxt(m_str, m_len);
			if (ctxt.Find(": ") != -1)
			{
				// [case: 13887]
				// horrible hack to prevent coloring of hornworm fogbugz tooltips
				// ideally, we would have some sort of IPC in place for hornworm to
				// tell us when a tooltip came from it
				return FALSE;
			}

			return TRUE; // don't check COLOR_HIGHLIGHT since it is not applicable to tooltips
		}

		if (IsPaintType(PaintType::ObjectBrowser) && m_str && *m_str)
		{
			WTString ctxt(m_str, m_len);
			static const LPCSTR kObjectViewerGroupNodes[] = {"Maps",
			                                                 "Global Functions and Variables",
			                                                 "Global Using Aliases and Typedefs",
			                                                 "Global Typedefs",
			                                                 "Base Types",
			                                                 "Derived Types",
			                                                 "Macros and Constants",
			                                                 "`anonymous-namespace'",
			                                                 ""};

			for (int idx = 0; *kObjectViewerGroupNodes[idx]; ++idx)
			{
				if (StartsWith(ctxt, kObjectViewerGroupNodes[idx], FALSE))
					return FALSE;
			}
		}

		// Color everything but highlighted items?
		return m_savestate.bgColor != ::GetSysColor(COLOR_HIGHLIGHT);
	}

	// case:118872 - retrieves the wide index and length from multibyte alternatives
	bool GetWideTextInfo(LPCSTR in_utf8_str, int in_utf8_len, LPCWSTR& out_wide_str, int& out_wide_len)
	{
		if (m_wstr && m_wPos && !IsPaintType(PaintType::SourceWindow))
		{
			int s_idx = ptr_sub__int(in_utf8_str, m_buf);
			int e_idx = s_idx + in_utf8_len;

			out_wide_str = m_wstr + m_wPos[s_idx];
			out_wide_len = m_wPos[e_idx] - m_wPos[s_idx];
			return true;
		}
		return false;
	}

	int GetTextWidth(LPCSTR mb_str, int mb_len)
	{
		if (!m_w)
		{
			SIZE size;
			LPCWSTR wBuf;
			int wLen;

			if (m_wstr && IsPaintType(PaintType::SourceWindow) && mb_str >= m_buf &&
			    mb_str < (m_buf + m_len)) // Get the width of the mbstring. case=50337
				GetTextExtentPoint32W(m_dc, m_wstr + (mb_str - m_buf), mb_len, &size);
			else if (mb_str >= m_buf && mb_str < (m_buf + m_len) && GetWideTextInfo(mb_str, mb_len, wBuf, wLen))
				GetTextExtentPoint32W(m_dc, wBuf, wLen, &size); // case:118872 - calculate from correct positions
			else
				GetTextExtentPoint32(m_dc, mb_str, mb_len, &size);

			return size.cx;
		}

		int w = 0;
		for (int i = 0; i < mb_len; i++)
			w += m_w[i];
		return w;
	}
	virtual void ExtTextOutDType(LPCSTR str, int l, uint dtype, uint attrs)
	{
		// ExtTextOut the string with the color and format of dtype
		CPoint curPT(0, 0);
		int size = GetTextWidth(str, l);
		BOOL shouldColor = ShouldColor();
		if (shouldColor)
			SetDTypeColor(m_dc, dtype, attrs);
		else if (m_savestate.bgColor != Psettings->m_colors[C_Text].c_bg)
			m_wasItalic = -1; // not if selected

		if (m_refFlag && m_savestate.bgColor == Psettings->m_colors[C_Text].c_bg)
			SetBkColor(m_dc, Psettings->m_colors[m_refFlag].c_bg);

		//////////////////////////////////////////////////////////////////////////
		// Funky logic for Bold Braces and italic fonts,
		// Only used if g_inPaint from EdCnt::OnPaint()
		// Else we just paint out
		int BraceHighlight = 0;
		if (IsPaintType(PaintType::SourceWindow))
		{
			// Get Current pos for italic and bolding
			::GetCurrentPositionEx(m_dc, &curPT);
			if (m_rc && !curPT.x)
				curPT.x += m_rc->left;
			curPT.y = m_rc ? m_rc->top : curPT.y;
			if (gShellAttr->IsDevenv())
				curPT.x = m_x;

			if (!Psettings->mUseMarkerApi)
			{
				// use paint for bold braces/underlines/find refs
				EdCntPtr ed = g_currentEdCnt;
				AttrClassPtr attr = g_ScreenAttrs.AttrFromPoint(ed, curPT.x, curPT.y, str);
				if (attr)
				{
					// Bold Braces
					if (Psettings->boldBraceMatch && (m_savestate.fgColor == Psettings->m_colors[C_Operator].c_fg ||
					                                  m_savestate.bgColor == Psettings->m_colors[C_Text].c_bg))
					{
						if (attr->mFlag == SA_BRACEMATCH && Psettings->boldBraceMatch)
							SetIdxColor(m_dc, BraceHighlight = C_MatchedBrace, STYLE_BOLD);
						if (attr->mFlag == SA_MISBRACEMATCH && Psettings->m_braceMismatches)
							SetIdxColor(m_dc, BraceHighlight = C_MismatchedBrace, STYLE_BOLD);
					}

					// Auto Highlight References Case=21926
					if (!m_refFlag && m_savestate.bgColor == Psettings->m_colors[C_Text].c_bg &&
					    (attr->mFlag == SA_REFERENCE || attr->mFlag == SA_REFERENCE_AUTO))
					{
						m_refFlag = C_Reference;
						SetBkColor(m_dc, Psettings->m_colors[m_refFlag].c_bg);
					}
					if (!m_refFlag && m_savestate.bgColor == Psettings->m_colors[C_Text].c_bg &&
					    (attr->mFlag == SA_REFERENCE_ASSIGN || attr->mFlag == SA_REFERENCE_ASSIGN_AUTO))
					{
						m_refFlag = C_ReferenceAssign;
						SetBkColor(m_dc, Psettings->m_colors[m_refFlag].c_bg);
					}

					// see also ScreenAttributes::AddDisplayAttribute in ScreenAttributes.cpp
					// case 15054: only underline modified files
					if (g_currentEdCnt && g_currentEdCnt->modified_ever && !Psettings->m_bSupressUnderlines)
					{
						if (attr->mFlag == SA_UNDERLINE_SPELLING && Psettings->m_spellFlags)
							m_FontStyle = STYLE_UNDERLINED_SPELLING;
						if (attr->mFlag == SA_UNDERLINE && Psettings->m_underlineTypos)
						{
							// Not in CS/VB, case: 509
							// Not JS or HTML, case: 15552
							// No typo underlines at all except in Src and Header
							if (g_currentEdCnt && (g_currentEdCnt->m_ftype == Src || g_currentEdCnt->m_ftype == Header))
								m_FontStyle = STYLE_UNDERLINED;
						}
					}
				}
				else
				{
					if (InComment())
					{
						SetBkColor(m_dc, m_savestate.bgColor);
					}
				}
			}
		}
		if (m_FontStyle & STYLE_BOLD)
			SelectObject(m_dc, ::GetBoldFont((HFONT)::GetCurrentObject(m_dc, OBJ_FONT)));
		if (m_FontStyle & STYLE_ITALIC && shouldColor)
			if (!(str[0] == '_' && gShellAttr->IsMsdev())) // No italic _'s in vc6
				SelectObject(m_dc, ::GetItalicFont((HFONT)::GetCurrentObject(m_dc, OBJ_FONT)));

		BOOL wasItalic = (m_wasItalic == (m_y ? m_y : curPT.y));
		if (((m_FontStyle & STYLE_ITALIC) && shouldColor) || wasItalic)
		{
			// Blank rect+1 so we can print that char[s] transparently to avoid clipping
			// Simplified logic for case 15084, case 14820
			CPoint pt(0, 0);
			::GetCurrentPositionEx(m_dc, &pt);
			int overhang = 5; // g_vaCharWidth[' '];
			CRect r = m_rc ? *m_rc : CRect(pt.x, pt.y, pt.x + size, pt.y + g_FontSettings->GetCharHeight() + 2);

			if (wasItalic)
			{
				// don't overwrite previous overhang, Text out first char with transparency
				CRect overhangRC(r.left, r.top, r.left + overhang, r.bottom);
				int lastMode = SetBkMode(m_dc, TRANSPARENT);
				RealExtTextOut(m_dc, m_x, m_y, (m_style & ~ETO_OPAQUE) | ETO_CLIPPED, overhangRC, str, 1, NULL);
				SetBkMode(m_dc, lastMode);

				::MoveToEx(m_dc, pt.x, pt.y, NULL); // move back
				// do the text out only with the masked rc, protecting previous overhang
				r.left += overhang; // mask overhang we just painted
			}
			if (m_FontStyle & STYLE_ITALIC) // (gShellAttr->IsMsdev())
			{
				// Paint italic with overhang
				r.right += overhang; // add a space for overhang
				ExtTextOut(m_dc, m_x, m_y, m_style | ETO_CLIPPED | ETO_OPAQUE, r, str, l /*+1*/, NULL);
				::MoveToEx(m_dc, pt.x + size, pt.y, NULL); // Move back before the overhang space
			}
			else
				ExtTextOut(m_dc, m_x, m_y, m_style | ETO_CLIPPED | ETO_OPAQUE, r, str, l, m_w);

			// Restore old font
			m_wasItalic = (m_FontStyle & STYLE_ITALIC) ? (m_y ? m_y : curPT.y) : -1;
		}
		else
		{
			if (IsPaintType(PaintType::SourceWindow))
			{
				// Fixes clipping issues in source files. case:16038
				// Since we are splitting some strings, we need to add an rc to prevent clipping of last char
				// Note r is different then m_rc, left is from our current position, protecting last char.
				CRect r(curPT.x, curPT.y, curPT.x + size, curPT.y + g_FontSettings->GetCharHeight());
				if (gShellAttr->IsMsdev())
				{
					if (m_rc) // Don't draw in left margin. case:16303
						ExtTextOut(m_dc, m_x, m_y, m_style | ETO_CLIPPED, m_rc, str, l, m_w);
					else
						ExtTextOut(m_dc, m_x, m_y, m_style | ETO_CLIPPED, &r, str, l, m_w);
				}
				else
					ExtTextOut(m_dc, m_x, m_y, m_style | ETO_CLIPPED | ETO_OPAQUE, &r, str, l, m_w);
			}
			else // Standard ExtTextOut
				ExtTextOut(m_dc, m_x, m_y, m_style, m_rc, str, l, m_w);
		}
		// Clear OPAQUE so the next paint does not overwrite this one.
		if (m_rc && (m_style & ETO_OPAQUE))
			m_style = (m_style & ~ETO_OPAQUE);
		m_x += size;
		if (BraceHighlight)
		{
			SetDTypeColor(m_dc, dtype, attrs);       // Is this needed?
			SetTextColor(m_dc, m_savestate.fgColor); // Restore normal color
		}

		if (m_FontStyle & (STYLE_ITALIC | STYLE_BOLD))
			::SelectObject(m_dc, m_savestate.font); // Restore normal font

		if (!Psettings->mUseMarkerApi)
		{
			if (m_FontStyle == STYLE_UNDERLINED_SPELLING)
				UnderLine(m_dc, curPT.x, curPT.y + g_FontSettings->GetCharAscent() - 1, curPT.x + size,
				          Psettings->m_colors[C_TypoError].c_fg);
			if (m_FontStyle == STYLE_UNDERLINED)
				UnderLine(m_dc, curPT.x, curPT.y + g_FontSettings->GetCharAscent() - 1, curPT.x + size,
				          Psettings->m_colors[C_TypoError].c_bg, FALSE);
		}
	}

	virtual void FlushTextOut()
	{
		if (m_LastPrintPos > m_cp)
			m_LastPrintPos = m_cp;
		if (m_LastPrintPos != m_cp)
		{
			int lp = min(m_cp, m_maxLen);
			ExtTextOutDType(&m_buf[m_LastPrintPos], lp - m_LastPrintPos, m_dtype, mAttrs);
			m_LastPrintPos = lp;
			if (m_LastPrintPos > m_cp)
				ASSERT(FALSE);
		}
	}
	virtual WTString GetCStr2(LPCSTR str)
	{
		if (m_wstr)
		{
			// Prevent reading past g_GlyfBuffer.m_p2
			int i;
			for (i = 0; str[i] && (m_wstr + m_cp + i) != g_GlyfBuffer.m_p2 &&
			            (str[i] == '~' || (str[i] == '!' && ISCSYM(str[i + 1])) || ISCSYM(str[i]));
			     i++)
				;
			return WTString(str, i);
		}
		return ::GetCStr(str);
	}

	virtual WTString GetCStr(LPCSTR str)
	{
		if (m_wstr && m_cp == 0 && ISCSYM(m_buf[0]))
		{
			// [case: 14570] GetCStr might rewind too far and read garbage - needs to be addressed
			// [case:118872] Note: following WIDE_TO_CHAR usage is not Unicode aware, but does the job

			// Search before str
			WTString cwd;
			for (int i = 1; i < 255 && !(m_wstr[-i] & 0xff00) && ISCSYM(WIDE_TO_CHAR(m_wstr[-i])); i++)
				cwd = WTString(WIDE_TO_CHAR(m_wstr[-i])) + cwd;
			return cwd + GetCStr2(str);
		}
		return GetCStr2(str);
	}

	void ExtTextOut(HDC dc, int x, int y, UINT style, LPCRECT rc, LPCSTR str, int len, CONST INT* w)
	{
		LPCWSTR wBuf;
		int wLen;

		if (m_wstr && IsPaintType(PaintType::SourceWindow))
		{
			_ASSERTE(str == (m_buf + m_LastPrintPos));
			// Display original wide string
			m_rval = ::RealExtTextOutW(dc, x, y, style, rc, m_wstr + m_LastPrintPos, (uint)len, w);
		}
		else if (GetWideTextInfo(str, len, wBuf, wLen))
		{
			_ASSERTE(str == (m_buf + m_LastPrintPos));
			// Display original wide string
			m_rval = ::RealExtTextOutW(dc, x, y, style, rc, wBuf, (uint)wLen,
			                           w); // case:118872 - draw with correct positions
		}
		else
			m_rval = ::RealExtTextOut(dc, x, y, style, rc, str, (uint)len, w);
		m_LastPrintPos += len;
		if (m_w)
			m_w += len;
	}
};

class ExtTextOutCacheCls : public ExtTextOutCls
{
	static ExtTextOutCacheCls s_cache;

  public:
	ExtTextOutCacheCls(int ftype) : ExtTextOutCls(ftype)
	{
	}

	static void ClearStatic()
	{
		s_cache.Init(nullptr, 0);
		if (gShellIsUnloading)
		{
			// must release DTypePtrs before the DType heap is closed
			for (ParserState& ps : s_cache.pState)
				ps.m_lwData.reset();

			s_cache.m_mp = nullptr;
			s_cache.m_overrideMp = nullptr;
		}
	}

	int ColorTextOut(HDC dc, int x, int y, UINT style, LPCSTR str, int len, LPCRECT rc, CONST INT* w, LPCWSTR wstr)
	{
		*this = s_cache;
		int r = __super::ColorTextOut(dc, x, y, style, str, len, rc, w, wstr);
		s_cache = *this;
		return r;
	}

	static void ClearComment()
	{
		s_cache.ReallyClearComment();
	}
};

ExtTextOutCacheCls ExtTextOutCacheCls::s_cache(Src);

void ClearTextOutCacheComment()
{
	ExtTextOutCacheCls::ClearComment();
}

CPoint GetWindowPosFromDc(HDC dc)
{
	HWND hWnd = WindowFromDC(dc);
	if (hWnd)
	{
		CRect rc;
		GetWindowRect(hWnd, &rc);
		return rc.TopLeft();
	}
	return CPoint(-1, -1);
}

CREATE_MLC(ExtTextOutCacheCls, ExtTextOutCacheCls_MLC);
class VAOutputExtTextOut : public ExtTextOutCacheCls
{
  public:
	VAOutputExtTextOut(int ftype) : ExtTextOutCacheCls(ftype)
	{
	}
	virtual void OnInitLine()
	{
		SetDType(CTEXT);
		m_canColor = TRUE;
		if (mSkipDc)
		{
			if (IsPaintType(PaintType::ToolTip) && mSkipDc == m_dc)
			{
				const CPoint pos(::GetWindowPosFromDc(m_dc));
				if (pos == mSkipPos)
					m_canColor = FALSE;
			}

			if (m_canColor)
				mSkipDc = NULL;
		}

		if (IsPaintType(PaintType::ToolTip) && strcmp("File:", m_str) == 0)
		{
			// [case:15547] don't color word wrapped file paths at the end of tooltips.
			// can't use ::WindowFromDC(m_dc) since tooltip window may be reused
			m_canColor = FALSE;
			mSkipDc = m_dc;
			mSkipPos = ::GetWindowPosFromDc(m_dc);
		}
		else if (m_str[0] == '[' && (IsPaintType(PaintType::ListBox) || IsPaintType(PaintType::ToolTip)))
		{
			// [case:81970] [case:82122]
			// don't color:
			// "[Files/Symbols in Solution]"
			// "[1 of 3]" in tooltips
			m_canColor = FALSE;
		}
		else if (IsPaintType(PaintType::FindInFiles))
		{
			// don't color until after the (n):
			m_canColor = FALSE;
		}
		else
		{
			// [case: 14568]
			// Checks from ExtTextOutWHook
			WTString ctxt(m_str, m_len);
			if (!strstr(m_str, "//") && !strstr(m_str, "/*"))
			{
				if (!strstr(ctxt.c_str(), "\"") && (strstr(ctxt.c_str(), ". ") || strstr(ctxt.c_str(), " and ") ||
				                                    strstr(ctxt.c_str(), "in Workspace")))
					m_canColor =
					    FALSE; // looks like a phrase "Globals and Functions" and is not in quotes [case: 140343]
				else if (strstr(ctxt.c_str(), ":\\") || strstr(ctxt.c_str(), ":/"))
				{
					m_canColor = FALSE;
					if (IsPaintType(PaintType::ToolTip))
					{
						// [case: 15862] coloring of filepaths in tooltips on vista
						mSkipDc = m_dc;
						mSkipPos = ::GetWindowPosFromDc(m_dc);
					}
				}
			}

			if (strstr(ctxt.c_str(), " references found") ||
			    strstr(ctxt.c_str(), " reference found")) // don't colour one specific findref tooltip
				m_canColor = FALSE;
			else if (IsPaintType(PaintType::View))
			{
				// [case: 14569] don't color the names of these outline groups
				if (ctxt == "Forward declarations" || ctxt == "File scope variables")
					m_canColor = FALSE;
			}

			if (m_canColor)
			{
				int i;
				for (i = 0; i < m_len && !m_str[i]; i++)
					;
				const WTString checkForFile(&m_str[i]);
				if (checkForFile.GetLength() > 3 && Other != GetFileType(checkForFile.Wide())) // is this a file?
					m_canColor = FALSE;
			}
		}
	}

	virtual BOOL ShouldColor()
	{
		if (!m_canColor)
			return FALSE; // Case 15090 file extension in recent files ...
		if (IsPaintType(PaintType::ToolTip))
			return TRUE; // [case: 15124] don't check COLOR_WINDOWTEXT for tooltips

		if (IsPaintType(PaintType::View) && CVS2010Colours::IsExtendedThemeActive())
		{
			// fix coloring in outline and find refs in dev11 dark theme
			if (CVS2010Colours::GetVS2010Colour(VSCOLOR_COMMANDBAR_TEXT_ACTIVE) == m_savestate.fgColor)
				return TRUE;
		}

		// Color only fg text?
		return m_savestate.fgColor == ::GetSysColor(COLOR_WINDOWTEXT) ||
		       m_savestate.bgColor == Psettings->m_colors[C_Text].c_bg;
	}

	virtual void IncCP()
	{
		if (!m_canColor && CurChar() == ':' && IsPaintType(PaintType::FindInFiles) && m_cp && m_buf[m_cp - 1] == ')')
			m_canColor = TRUE;
		__super::IncCP();
	}

	virtual void OnCSym()
	{
		if (IsPaintType(PaintType::AssemblyView))
		{
			// [case:118872] Note: this is not Unicode aware, PaintType::AssemblyView is unused, debug only value!

			WTString sym = GetCStr(CurPos());
			// only color lines of code of after the 21'st col in asmbly
			if (!wt_isdigit(m_buf[0]) /*|| m_cp > 21*/)
				__super::OnCSym(); // Color code
			else if (sym == "ret" || sym == "call" || sym == "lea" || sym == "jmp" || sym == "jne" || sym == "je" ||
			         sym == "int")
				SetDType(RESWORD);
			return;
		}
		if (m_canColor)
			__super::OnCSym();
	}
};
CREATE_MLC(VAOutputExtTextOut, VAOutputExtTextOut_MLC);

int ExtTextOutColorCache(HDC dc, int x, int y, UINT style, LPCSTR str, int len, LPCRECT rc, CONST INT* w, LPCWSTR wstr)
{
	// Cache lines that are split into multiple TextOuts so we color with context
	TempTrue _t(g_PaintLock);
	try
	{
		if (str)
		{
			// Filter out unicode strings so they don't choke the parser. case:16737
			for (int i = 0; i < len; i++)
				if (str[i] & 0x80)
					return RealExtTextOut(dc, x, y, style, rc, str, (uint)len, w);
		}
		// Case: 17006, Allow underlining in comments and strings that contain unicode strings
		// 		if(wstr)
		// 		{
		// 			// Filter out wide strings so they don't choke the parser. case:16927
		// 			for(int i = 0; i < len; i++)
		// 				if(wstr[i]&0xff80)
		// 					return RealExtTextOutW(dc, x, y, style, rc, wstr,len, w );
		// 		}

		int r;
		int type = g_currentEdCnt ? g_currentEdCnt->m_ftype : Src;
		if (Is_Tag_Based(type))
			type = Src; // HTML expects <script>..., resort coloring as Src?  Case: 15381, Case 15566
		if (IsPaintType(PaintType::FindInFiles) || IsPaintType(PaintType::View) ||
		    IsPaintType(PaintType::AssemblyView) || IsPaintType(PaintType::ToolTip))
		{
			VAOutputExtTextOut_MLC rtc(type);
			r = rtc->ColorTextOut(dc, x, y, style, str, len, rc, w, wstr);
		}
		else
		{
			ExtTextOutCacheCls_MLC rtc(type);
			r = rtc->ColorTextOut(dc, x, y, style, str, len, rc, w, wstr);
		}
		return r;
	}
	catch (...)
	{
		vLog("ERROR: ExtTextOutColorCache %x %x", PaintType::in_WM_PAINT, PaintType::inPaintType);
#ifdef _DEBUG
		if (ErrorBox("ExtTextOutCacheColor\r\n\r\nPress OK to debug...", MB_OKCANCEL) != IDOK)
			throw;
#endif // _DEBUG
	}
	return 1;
}
int ExtTextOutColor(HDC dc, int x, int y, UINT style, LPCSTR str, int len, LPCRECT rc, CONST INT* w)
{
	return ExtTextOutColorCache(dc, x, y, style, str, len, rc, w, NULL);
}
int ExtTextOutWColor(HDC dc, int x, int y, UINT style, LPCWSTR wstr, int len, LPCRECT rc, CONST INT* w)
{
	return ExtTextOutColorCache(dc, x, y, style, NULL, len, rc, w, wstr);
}

// Keep in sync with VaClassifier.cs
enum ClassificationContext
{
	ClassificationContext_Undefined = 0,
	ClassificationContext_Editor,
	ClassificationContext_QuickInfo,
	ClassificationContext_ParamInfo,
	ClassificationContext_FindResults
};

#if !defined(RAD_STUDIO)
//////////////////////////////////////////////////////////////////////////
//  Wrapper class to color WPF the with the old syntax coloring logic
class WPF_Colorizer : public VAParseExtTextOutCls
{
	// Note we could use g_currentEdCnt->m_buf and bufPos for full scope coloring
	static WPF_Colorizer s_cache;

	WTString m_lineCache;
	int m_MEF_ColorAttribute;

  public:
	WPF_Colorizer(int ftype) : VAParseExtTextOutCls(ftype)
	{
	}

	static void ClearStatic()
	{
		s_cache.Init(nullptr, 0);
		if (gShellIsUnloading)
		{
			// must release DTypePtrs before the DType heap is closed
			for (ParserState& ps : s_cache.pState)
				ps.m_lwData.reset();

			s_cache.m_mp = nullptr;
			s_cache.m_overrideMp = nullptr;
		}
	}

	int GetSymbolColor(LPCWSTR lineText, int linePos, int bufPos, ClassificationContext context)
	{
		static bool sStartingUp = true;
		if (!GlobalProject || GlobalProject->IsBusy() || sStartingUp)
		{
			if (g_pMFCDic && !g_pMFCDic->m_loaded && IsCFile(gTypingDevLang))
				return 0;
			if (g_pCSDic && !g_pCSDic->m_loaded && Defaults_to_Net_Symbols(gTypingDevLang))
				return 0;
			if (!g_pCSDic && !g_pMFCDic)
				return 0;

			sStartingUp = false;
		}

		WTString mbline = ::WideToMbcs(lineText, wcslen_i(lineText));
		const int mbPos = ::AdjustPosForMbChars(mbline, linePos);

		int prevPaintType = PaintType::inPaintType;

		switch (context)
		{
		case ClassificationContext_Editor:
			PaintType::SetPaintType(PaintType::SourceWindow);
			break;
		case ClassificationContext_QuickInfo:
		case ClassificationContext_ParamInfo:
			PaintType::SetPaintType(PaintType::VS_ToolTip);
			break;
		case ClassificationContext_FindResults:
			PaintType::SetPaintType(PaintType::FindInFiles);
			break;
		default:
			_ASSERTE(!"GetSymbolColor: Invalid ClassificationContext");
		}

		auto overrideMp = this->m_overrideMp;
		*this = s_cache;
		this->m_overrideMp = overrideMp;

		if (m_lineCache.GetLength() != mbline.GetLength() || m_lineCache != mbline || (int)m_cp > mbPos)
		{
			m_lineCache = mbline;
			Init(m_lineCache);
		}

		if (mbPos > m_lineCache.GetLength())
		{
			// load utf16.cpp in ast project for old assert to fire
			WTString padding;
			while (mbPos > (m_lineCache.GetLength() + padding.GetLength()))
				padding += " ";
			m_lineCache += padding;
			Init(m_lineCache);
		}
		m_maxLen = mbPos + 1;

		m_MEF_ColorAttribute = 0;
		DoParse();
		SetDTypeColor(NULL, m_dtype, mAttrs);

		PaintType::SetPaintType(prevPaintType);

		s_cache = *this;

		if (ClassificationContext_Editor == context && C_Keyword == m_MEF_ColorAttribute &&
		    (!g_currentEdCnt || (UC != g_currentEdCnt->m_ftype && RC != g_currentEdCnt->m_ftype)))
		{
			// in editor context, only UC and RC consume C_Keyword
			m_MEF_ColorAttribute = C_Undefined;
		}
		else if (ClassificationContext_QuickInfo == context || ClassificationContext_ParamInfo == context)
		{
			const int kBoldBit = STYLE_BOLD << 16;
			if (m_MEF_ColorAttribute & kBoldBit)
			{
				// don't use locals bold attribute for tooltips.
				// may need to use it for CurrentParam in paramInfo though...
				m_MEF_ColorAttribute &= ~kBoldBit;
			}
		}

		return m_MEF_ColorAttribute;
	}
	virtual void SetIdxColor(HDC dc, int clrIdx, int style = 0)
	{
		// Map to VAMEF_ColorAttribute on mef side
		m_MEF_ColorAttribute = clrIdx | (style << 16);
	}
	virtual BOOL ShouldColor()
	{
		return TRUE;
	}
	virtual WTString GetCStr(LPCSTR str)
	{
		return ::GetCStr(str);
	};
};

WPF_Colorizer WPF_Colorizer::s_cache(Src);

CREATE_MLC(WPF_Colorizer, WPF_Colorizer_MLC);

EdCntPtr WPFGetSymbolColor_LastEd = NULL;

int WPFGetSymbolColor(IVsTextBuffer* vsTextBuffer, LPCWSTR lineText, int linePos, int bufPos, int context)
{
	static IVsTextBuffer* lastVsTextBuffer;

	if (lastVsTextBuffer != vsTextBuffer)
	{
		lastVsTextBuffer = vsTextBuffer;
		WPFGetSymbolColor_LastEd = GetOpenEditWnd(vsTextBuffer);
	}
	else if (!WPFGetSymbolColor_LastEd)
		WPFGetSymbolColor_LastEd = GetOpenEditWnd(vsTextBuffer);

	WPF_Colorizer_MLC rtc(gTypingDevLang);

	if (WPFGetSymbolColor_LastEd)
		rtc->OverrideMp(WPFGetSymbolColor_LastEd->GetParseDb());

	return rtc->GetSymbolColor(lineText, linePos, bufPos, (ClassificationContext)context);
}
#endif

void ClearTextOutGlobals()
{
#if !defined(RAD_STUDIO)
	WPFGetSymbolColor_LastEd = NULL;
	WPF_Colorizer::ClearStatic();
#endif
	ExtTextOutCacheCls::ClearStatic();
	VAParseExtTextOutCls::ClearStatic();
}
