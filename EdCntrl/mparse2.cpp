#include "stdafxed.h"
#include "edcnt.h"
#include "mparse.h"
#include "foo.h"
#include "resource.h"
#include "project.h"
#include "import.h"
#include "timer.h"
#include "macroList.h"
#include "DBLock.h"
#include "wtcsym.h"
#include "wt_stdlib.h"
#include "FileTypes.h"
#include "StatusWnd.h"
#include "Settings.h"
#include "SpellBlockInfo.h"
#include "Import.h"
#include "myspell\WTHashList.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define cwdEQ(str) (_tcsicmp(cwd.c_str(), str) == 0)
static WTString GetStringTableLine(LPCSTR p, int i);

WTString NxtCSym(LPCSTR p)
{
	for (; *p && strchr(" \t", *p); p++)
		;
	int i;
	for (i = 0; p[i] && ISCSYM(p[i]); i++)
		;
	return WTString(p, i);
}
WTString GetLine(LPCSTR p, int i)
{
	for (; i > 0 && !strchr(">(),\n", p[i - 1]); i--)
		; // get bol, or (, or to '>' as in "<System.Diag> sub Foo"
	for (; p[i] && strchr(" \t", p[i]); i++)
		; // eat indent
	LPCSTR endChars = "),}\r\n";
	p = &p[i];
	for (i = 0; p[i] && !strchr(endChars, p[i]); i++)
	{
		if (strchr("\"({", p[i]))
			endChars = "\r\n";
	}
	return WTString(p, i);
}
char NxtChar(LPCSTR p)
{
	for (; *p && strchr(" \t", *p); p++)
		;
	return *p;
}
void preScope(WTString& scope)
{
	int i = scope.ReverseFind(':');
	if (i >= 0)
		scope = scope.Mid(0, i);
}

int MultiParse::ParseBlockHTML()
{
	// BOOL inTag = FALSE;
	//#ifdef _DEBUG
	//	int testLine = m_line;
	//	int testLinePos = 0;
	//#endif // _DEBUG
	WTString scope;
	BOOL isVB = (FileType() != JS); // FileType() == VB;
	BOOL commentLine = FALSE;
	BOOL commentBlock = FALSE;
	BOOL isRC = FileType() == RC;
	INT inStringTable = 0;
	char inString = '\0';
	int begWord = -1;
	int begLine = -1;
	bool jsARG = false;
	BOOL isDeclare = FALSE;
	int inParen = 0;
	bool inenum = false;
	uint attribute = 0;
	WTString ldef;
	BOOL isScript = FALSE;
	m_HTML_Scope = HTML_inText;
	m_inParenCount = 0;
#define STACKSZ 10
	int agrCount[STACKSZ + 1], agrOffset[STACKSZ + 1];
	WTString lscope[STACKSZ + 1], lwd[STACKSZ + 1], lwScope[STACKSZ + 1];
	BOOL inAssignment[STACKSZ + 1];
	BOOL inMethod[STACKSZ + 1];
	agrCount[0] = agrOffset[0] = inMethod[0] = 0;

	char spellBuf[255] = "";
	int spellBufPos = 0;
	BOOL ignoreWord = FALSE;

	for (int i = 0; i < m_len; i++)
	{
		//#ifdef _DEBUG
		//		if(testLine != m_line){
		//			int i = 123;
		//		}
		//		for(;testLinePos < i;testLinePos++){
		//			if(m_p[testLinePos] == '\n')
		//				testLine++;
		//		}
		//		if(testLine != m_line){
		//			int i = 123;
		//		}
		//
		//#endif // _DEBUG
		BOOL canSpell = FALSE;
		if (m_ed && ((m_spellFromPos <= i || m_showRed)))
		{
			if (Is_Tag_Based(FileType()))
			{
				if (m_HTML_Scope == HTML_inText ||
				    // don't spell in tags, too many underlines
				    ((m_HTML_Scope != HTML_inTag) && (commentBlock || commentLine || inString)))
				{
					m_cp = i;
					canSpell = TRUE;
				}
			}
			else if (commentBlock || commentLine || inString)
			{
				m_cp = i;
				canSpell = TRUE;
			}
		}
		if (canSpell)
		{
			BOOL doSpell = (m_cp == (m_len - 1));
			if (!ignoreWord && wt_isalpha(m_p[m_cp]))
			{
				if (spellBufPos && (m_p[m_cp] >= 'A' && m_p[m_cp] <= 'Z')) // Contains CAPS/mixed case
					ignoreWord = TRUE;
				if (spellBufPos < 254)
				{
					spellBuf[spellBufPos++] = m_p[m_cp];
					spellBuf[spellBufPos] = '\0';
				}
			}
			else
			{
				spellBuf[spellBufPos++] = '\0';
				spellBufPos = 0;
				doSpell = TRUE;
				if (m_p[m_cp] == '.' && ISCSYM(m_p[m_cp + 1])) // www.something
					ignoreWord = TRUE;
				else if (m_p[m_cp] == '#' && ISCSYM(m_p[m_cp + 1])) // [case: 91351] hashtag
					ignoreWord = TRUE;
				else if (strchr("@\\/-_", m_p[m_cp]) || wt_isdigit(m_p[m_cp]))
					ignoreWord = TRUE;
				else if (m_p[m_cp] == '<' && !Is_Tag_Based(FileType()))
					ignoreWord = TRUE;
				else if (strchr(" \t\r\n,\"", m_p[m_cp]))
					ignoreWord = FALSE;

				if (!ignoreWord)
				{
					// see if it looks like code, thiss * willl = nt+beee/Spelled;
					LPCSTR nc;
					for (nc = &m_p[m_cp]; *nc && strchr(" \t", *nc); nc++)
						;
					if (strchr("<>", *nc))
					{
						if (!Is_Tag_Based(FileType()))
							ignoreWord = TRUE;
					}
					else if (strchr("=*+;[]", *nc))
						ignoreWord = TRUE;
				}
			}

			if (!ignoreWord && doSpell && spellBuf[0])
			{
				const WTString cwd(spellBuf);
				spellBuf[0] = '\0';
				spellBufPos = 0;
				if (!m_pLocalDic->FindAny(cwd) && !g_pGlobDic->FindAny(cwd) && !SDictionary()->FindAny(cwd))
				{
					if (!FPSSpell(cwd.c_str()) &&
					    FPSSpellSurroundingAmpersandWord(m_p, m_cp - cwd.GetLength(), m_cp, cwd))
					{
						if (m_spellFromPos != -1)
						{
							s_SpellBlockInfo.spellWord = cwd;
							s_SpellBlockInfo.m_p2 = m_cp;
							s_SpellBlockInfo.m_p1 = m_cp - cwd.GetLength();
							m_len = m_cp - cwd.GetLength();
							return true;
						}
						m_pLocalDic->add(WTString(":VAunderline:") + cwd, itos(m_line), LVAR,
						                 V_PRIVATE | V_HIDEFROMUSER);
					}
				}
			}
		}

		// handle baclslashslash
		if (m_p[i] == '\\')
		{
			if (m_p[i] == '\n')
				m_line++;
			i++;
			continue;
		}
		if (m_p[i] == '\n')
		{
			inString = '\0';
			commentLine = 0;
		}

		if (ISCSYM(m_p[i]))
		{
			if (begWord < 0)
				begWord = i;
			if (begLine < 0)
				begLine = i;
		}
		else
		{
			// non csym
			if (inString)
			{
				begWord = -1;
				if (inString == m_p[i] || m_p[i] == '\n')
					inString = '\0';
				continue;
			}
			if (commentBlock)
			{
				begWord = -1;
				if (m_p[i] == '*' && m_p[i + 1] == '/')
					commentBlock = FALSE;
				continue;
			}
			// get strings and comments
			if (m_p[i] == '\'')
			{
				if (isVB)
					commentLine = TRUE;
				else
					inString = '\'';
			}
			if (m_p[i] == '/' && m_p[i + 1] == '/')
			{
				commentLine = TRUE;
			}
			if (m_p[i] == '#' && (FileType() == PERL || Is_VB_VBS_File(FileType())))
			{
				commentLine = TRUE;
			}
			if (m_p[i] == '/' && m_p[i + 1] == '*')
			{
				commentBlock = TRUE;
			}
			if (m_p[i] == '"')
				inString = '"';
			if (m_p[i] == '\n')
			{
				m_line++;
				inString = '\0';
				commentLine = FALSE;
				isDeclare = FALSE;
				ldef.Empty();
				begWord = -1;
				continue;
			}
			if (commentLine || commentBlock || inString)
			{
				begWord = -1;
				continue;
			}

			if (m_p[i] == '.')
				m_xref = true;
			else
				m_xref = false;
			if (m_p[i] == ',')
			{
				agrCount[inParen]++;
			}
			if (m_p[i] == '=')
				inAssignment[inParen] = TRUE;
			if (m_p[i] == ',')
				inAssignment[inParen] = TRUE;

			// end of statement?
			if (isVB && m_p[i] == '\n')
				begLine = -1;
			if (!isVB && strchr(";}", m_p[i]))
				begLine = -1;
			// was text string?
			if (begWord >= 0 && !commentBlock && !commentLine && ISCSYM(m_p[begWord]))
			{
				WTString cwd(&m_p[begWord], i - begWord);
				if (m_showRed && !(isRC || commentBlock || commentLine || inString) &&
				    (m_p[begWord] == '_' || wt_isalpha(m_p[begWord])))
				{
					DType* dat = FindAnySym(cwd);
					if (!dat)
					{
						if (m_p[i] == '(')
						{
							add(WTString(":VAUnknown:") + cwd, cwd);
						}
						else
							add(WTString(":VAUnknown:") + cwd, cwd);
					}
				}
				if (m_writeToDFile)
				{
					if (jsARG && (m_parseAll || !inMethod[inParen]))
					{
						DBOut(scope + DB_SEP_STR + cwd, WTString("var ") + cwd, FUNC, 0, m_line);
					}
					if (isRC && _tcsicmp("STRINGTABLE", cwd.c_str()) == 0)
						inStringTable = 1;
					if (isRC && inStringTable == 1 && _tcsicmp("BEGIN", cwd.c_str()) == 0)
						inStringTable = 2;
					if (m_writeToDFile && Is_VB_VBS_File(FileType()) && _tcsicmp("Imports", cwd.c_str()) == 0)
					{
						token t = SubStr(i, i + 150);
						token2 fullname = t.read(" \t\r\n;");
						WTString partname;
						while (fullname.length())
						{
							partname += fullname.read('.');
							//							NetImport(partname);
							partname += '.';
						}

						WTString ns = ':';
						LPCSTR p = &m_p[i];
						for (; *p && (*p == ' ' || *p == '\t'); p++)
							;
						for (; *p && (*p == '.' || ISCSYM(*p)); p++)
						{
							if (*p == '.')
								ns += ':';
							else
								ns += *p;
						}
						ns += '\f';
						if (m_parseAll || !inMethod[inParen])
							DBOut(":wtUsingNamespace", ns, CLASS, V_HIDEFROMUSER, m_line);
					}
					if (m_writeToDFile && Is_VB_VBS_File(FileType()) && _tcsicmp("As", cwd.c_str()) == 0 &&
					    lwd[inParen].length())
					{
						WTString def = GetLine(m_p, i + 1); // foo(ByVal bar() as String)
						if (def.GetLength() && (m_parseAll || !inMethod[inParen]))
							DBOut(scope + DB_SEP_STR + lwd[inParen], def, VAR, 0, m_line);
					}
					if (m_writeToDFile && _tcsicmp("include", cwd.c_str()) == 0)
					{
						WTString ns = ':';
						LPCSTR p = &m_p[i];
						for (; *p && !strchr("\"<\r\n", *p); p++)
							;
						if (*p == '"' || *p == '<')
						{
							WTString file = *p++;
							for (; *p && !strchr("\">\r\n", *p); p++)
								file += *p;
							if (*p == '"' || *p == '<')
							{
								file = "#include " + file + *p;
								if (m_parseAll || !inMethod[inParen])
									ImmediateDBOut("include", file, DEFINE, 0, m_line);
							}
						}
					}
					// if(FileType() != VB)
					if (m_writeToDFile && inenum)
					{
						WTString meth = NxtCSym(&m_p[i]);
						WTString def = GetLine(m_p, i);
						DBOut(scope + DB_SEP_STR + cwd, WTString("Enum ") + scope, VAR, 0, m_line);
					}
					if (m_writeToDFile && !inAssignment[inParen] && ldef.GetLength() && !lwd[inParen].GetLength() &&
					    (m_parseAll || !inMethod[inParen]))
					{
						WTString def = ldef + " " + GetLine(m_p, i);
						DBOut(scope + DB_SEP_STR + cwd, def, VAR, 0, m_line);
					}
					// if(FileType() != VB)
					if (_tcsicmp("Var", cwd.c_str()) == 0 || _tcsicmp("Const", cwd.c_str()) == 0 ||
					    _tcsicmp("Dim", cwd.c_str()) == 0 || _tcsicmp("Static", cwd.c_str()) == 0 ||
					    _tcsicmp("Attribute", cwd.c_str()) == 0)
					{
						WTString meth = NxtCSym(&m_p[i]);
						WTString def = GetLine(m_p, i);
						ldef = cwd;
						if (m_writeToDFile && def.GetLength() && (m_parseAll || !inMethod[inParen]))
							DBOut(scope + DB_SEP_STR + meth, def, VAR, 0, m_line);
					}
					else if (Is_VB_VBS_File(FileType()) && m_writeToDFile && _tcsicmp("Inherits", cwd.c_str()) == 0)
					{
						token t = SubStr(i, i + 150);
						token2 fullname = t.read(" \t\r\n;");
						WTString partname;
						while (fullname.length())
						{
							partname += fullname.read('.');
							//							NetImport(partname);
							partname += '.';
						}

						WTString base = GetLine(m_p, i);
						//						extern void ReplaceAll (WTString& scope, TCHAR from, TCHAR to);
						//						ReplaceAll(base, '.', ':');
						if (base.GetLength() && (m_parseAll || !inMethod[inParen]))
						{
							WTString def = GetLine(m_p, i);
							DBOut(scope + DB_SEP_STR + "MyBase", base, VAR, 0, m_line);
							DBOut(scope, WTString("Class ") + &scope.c_str()[1] + " : " + base, CLASS, 0,
							      m_line); // add to this
							// args in js are declared w/o var
							if (FileType() == JS)
								jsARG = true;
						}
					}
				}
				if (_tcsicmp("Public", cwd.c_str()) == 0)
				{
					attribute = 0;
				}
				if (_tcsicmp("Private", cwd.c_str()) == 0)
				{
					attribute = V_PRIVATE;
				}
				if (_tcsicmp("Protected", cwd.c_str()) == 0)
				{
					attribute = V_PROTECTED;
				}
				if (!isVB && !isDeclare &&
				    (_tcsicmp("Sub", cwd.c_str()) == 0 || _tcsicmp("Function", cwd.c_str()) == 0))
				{
					WTString meth = NxtCSym(&m_p[i]);
					if (meth.length())
					{
						if (m_writeToDFile && (m_parseAll || !inMethod[inParen]))
						{
							WTString def = GetLine(m_p, i);
							DBOut(DB_SEP_STR + meth, def, FUNC, attribute, m_line);
							_ASSERTE(!"when does this happen - tell sean what command was invoked in what file (DType "
							          "TYPEMASK issue)");
							// DBOut(meth, itos(m_line+1) + "|" + itos(FUNC|attribute), METHODDEFINITION, 0, m_line);
						}
						scope = scope + DB_SEP_STR + meth;
					}
				}
				if (isRC)
				{
					if (m_writeToDFile && (_tcsicmp("DIALOG", cwd.c_str()) == 0 ||
					                       _tcsicmp("TOOLBAR", cwd.c_str()) == 0 || _tcsicmp("MENU", cwd.c_str()) == 0))
					{
						if (m_parseAll || !inMethod[inParen])
							DBOut(DB_SEP_STR + lwd[inParen], cwd + " " + lwd[inParen], DEFINE,
							      /*V_USERDEFINEDKEYWORD|*/ attribute, m_line);
					}
					if (_tcsicmp("End", cwd.c_str()) == 0) // not for end Select
						inStringTable = FALSE;
					if (inStringTable == 2 && _tcsicmp("BEGIN", cwd.c_str()) != 0)
					{
						const WTString def = ::GetStringTableLine(m_p, i);
						if (m_writeToDFile && begWord && m_p[begWord - 1] != '"' && (m_parseAll || !inMethod[inParen]))
							DBOut(DB_SEP_STR + cwd, def, DEFINE, /*V_USERDEFINEDKEYWORD|*/ attribute, m_line);
						for (; i < m_len && m_p[i] != '\n'; i++)
							;
						m_line++;
					}
				}

				if (isVB)
				{
					if (_tcsicmp("End", lwd[inParen].c_str()) != 0) // not for end Select
						if (_tcsicmp("If", cwd.c_str()) == 0 || _tcsicmp("Select", cwd.c_str()) == 0 ||
						    _tcsicmp("Try", cwd.c_str()) == 0)
						{
							WTString nextwd = NxtCSym(&m_p[i]);
							if (_tcsicmp("If", cwd.c_str()) == 0)
							{
								// look for "Then  statement" on same line
								WTString def = GetLine(m_p, i);
								def.MakeLower();
								int p = def.find("then");
								if (p == -1)
									nextwd.Empty();
								else
									nextwd = NxtCSym(&def.c_str()[p + 4]);
							}
							if (!nextwd.GetLength() || !cwdEQ("Then"))
							{ // Then ' without statement on same line
								if (inParen)
									lscope[inParen] = scope;
								if (inParen < STACKSZ)
									inParen++;
								inMethod[inParen] = TRUE;
								if (cwdEQ("Then"))
									scope = scope + DB_SEP_STR + "If";
								else
									scope = scope + DB_SEP_STR + cwd;
								inAssignment[inParen] = FALSE;
								agrCount[inParen] = 0;
								agrOffset[inParen] = i;
							}
						}
					if (_tcsicmp("End", cwd.c_str()) == 0 /*|| _tcsicmp("ElseIf", cwd.c_str()) == 0 */ ||
					    _tcsicmp("Next", cwd.c_str()) == 0)
					{
						// preScope(scope);
#ifdef _DEBUG
						if (!inParen && m_writeToDFile)
						{
							//							MessageBeep(0xffffffff);
							SetStatus("Scope hosed");
						}
#endif // _DEBUG
						if (inParen > 0)
							inParen--;
						scope = lscope[inParen];
					}
					if (_tcsicmp("Declare", cwd.c_str()) == 0)
					{
						isDeclare = TRUE;
					}
					//					if(_tcsicmp("End", lwd[inParen]) != 0) // not for end Select
					//					if(_tcsicmp("Region", cwd.c_str()) == 0){
					//						if(inParen)
					//							lscope[inParen] = scope;
					//						if(inParen < STACKSZ)
					//							inParen++;
					//						inAssignment[inParen] = FALSE;
					//						agrCount[inParen] = 0;
					//						agrOffset[inParen] = i;
					//					}
					if (_tcsicmp("End", lwd[inParen].c_str()) != 0 &&
					    _tcsicmp("Exit", lwd[inParen].c_str()) != 0) // not for end Select
						if (_tcsicmp("For", cwd.c_str()) == 0 || _tcsicmp("Get", cwd.c_str()) == 0 ||
						    _tcsicmp("Set", cwd.c_str()) == 0 || _tcsicmp("With", cwd.c_str()) == 0 ||
						    _tcsicmp("While", cwd.c_str()) == 0)
						{
							if (inParen)
								lscope[inParen] = scope;
							if (inParen < STACKSZ)
								inParen++;
							inMethod[inParen] = TRUE;
							inAssignment[inParen] = FALSE;
							agrCount[inParen] = 0;
							agrOffset[inParen] = i;
							scope = scope + DB_SEP_STR + cwd;
						}
					if (_tcsicmp("End", lwd[inParen].c_str()) == 0) // not for end Select
						if (_tcsicmp("Enum", cwd.c_str()) == 0)
						{
							inenum = false;
						}
					if (_tcsicmp("End", lwd[inParen].c_str()) != 0 && !isDeclare) // not for end Select
						if (_tcsicmp("Function", cwd.c_str()) == 0 || _tcsicmp("Sub", cwd.c_str()) == 0 ||
						    _tcsicmp("Structure", cwd.c_str()) == 0 || _tcsicmp("Property", cwd.c_str()) == 0)
						{
							WTString meth = NxtCSym(&m_p[i]);
							if (meth.GetLength())
							{
								uint type = FUNC;
								if (_tcsicmp("Property", cwd.c_str()) == 0)
									type = PROPERTY;
								if (_tcsicmp("Struct", cwd.c_str()) == 0)
									type = STRUCT;
								if (m_writeToDFile && (m_parseAll || !inMethod[inParen]))
								{
									WTString def = GetLine(m_p, i);
									DBOut(scope + DB_SEP_STR + meth, def, type, attribute, m_line);
									_ASSERTE(!"when does this happen - tell sean what command was invoked in what file "
									          "(DType TYPEMASK issue)");
									// DBOut(scope + DB_SEP_STR + meth, itos(m_line+1) + "|" + itos(type|attribute),
									// METHODDEFINITION, 0, m_line);
								}
								// args in js are declared w/o var
								if (_tcsicmp("Delegate", lwd[inParen].c_str()) != 0)
								{
									if (inParen)
										lscope[inParen] = scope;
									if (inParen < STACKSZ)
										inParen++;
									inMethod[inParen] = TRUE;
									scope = scope + DB_SEP_STR + meth;
									inAssignment[inParen] = FALSE;
									agrCount[inParen] = 0;
									agrOffset[inParen] = i;
									if (FileType() == JS)
										jsARG = true;
								}
							}
						}
						else if (Is_VB_VBS_File(FileType()) && (cwdEQ("Class") || cwdEQ("Enum")) &&
						         _tcsicmp("End", lwd[inParen].c_str()) != 0)
						{
							WTString cls = NxtCSym(&m_p[i]);
							WTString def = GetLine(m_p, i);
							//						extern void ReplaceAll (WTString& scope, TCHAR from, TCHAR to);
							//						ReplaceAll(base, '.', ':');
							if (cls.GetLength() && (m_parseAll || !inMethod[inParen]))
							{
								m_baseClass = scope + DB_SEP_STR + cls;
								m_baseClassList = GetBaseClassList(m_baseClass);
								WTString def2 = GetLine(m_p, i);
								if (m_writeToDFile)
								{
									DBOut(scope + DB_SEP_STR + cls, def2, (cwdEQ("Class") ? CLASS : VAR), attribute,
									      m_line);
									DBOut(scope + DB_SEP_STR + cls + DB_SEP_STR + "Me", cls, VAR, 0, m_line);
									_ASSERTE(!"when does this happen - tell sean what command was invoked in what file "
									          "(DType TYPEMASK issue)");
									// DBOut(scope + DB_SEP_STR + cls, itos(m_line+1) + "|" +
									// itos((cwdEQ("Class")?CLASS:VAR)|attribute), METHODDEFINITION, 0, m_line);
								}
								// args in js are declared w/o var
								if (cwdEQ("Enum"))
									inenum = true;
								if (FileType() == JS)
									jsARG = true;
								if (inParen)
									lscope[inParen] = scope;
								if (inParen < STACKSZ)
									inParen++;
								inMethod[inParen] = inMethod[inParen - 1];
								scope = scope + DB_SEP_STR + cls;
								inAssignment[inParen] = FALSE;
								agrCount[inParen] = 0;
								agrOffset[inParen] = i;
							}
						}
				}
				if (m_p[i] == ',' || m_p[i] == '\n')
					lwd[inParen].Empty();
				else
					lwd[inParen] = cwd;
				if ((m_len - i) < 255)
				{ // get def of cwd
					if (m_p[i] == '.' || m_p[i] == '(')
					{ // get def of cwd
						DType* data = NULL;
						WTString bcl = GetBaseClassList(lwScope[inParen]);
						if (!lwScope[inParen].length() || lwScope[inParen] == DB_SEP_STR)
							data = FindSym(&cwd, &scope, NULL);
						else
							data = FindSym(&cwd, &scope, &bcl);
						if (data)
						{
							m_xrefScope = lwScope[inParen] = data->SymScope();
						}
					}
					else
					{
						m_xref = false;
						m_xrefScope.Empty();
						// lwScope[inParen].Empty();
					}
				}
				begWord = -1;
			}
			if (Is_Tag_Based(FileType()))
			{
				if (m_p[i] == '<')
				{
					WTString cwd = NxtCSym(&m_p[i + 1]);
					isScript = m_p[i + 1] != '/' && (cwdEQ("script") || cwdEQ("tab"));
					if (m_HTML_Scope == HTML_inScript)
					{
						if (m_p[i + 1] == '/')
							m_HTML_Scope = HTML_inTag;
					}
					else
						m_HTML_Scope = HTML_inTag;
				}
				if (m_p[i] == '>')
				{
					if (isScript)
						m_HTML_Scope = HTML_inScript;
					else if (m_HTML_Scope == HTML_inTag)
						m_HTML_Scope = HTML_inText;
				}
			}
			if (m_p[i] == '(')
			{
				if (inParen)
					lscope[inParen] = scope;
				if (!Is_VB_VBS_File(FileType()))
					scope = scope + DB_SEP_STR + lwd[inParen];
				if (inParen < STACKSZ)
					inParen++;
				inMethod[inParen] = inMethod[inParen - 1];
				inAssignment[inParen] = FALSE;
				agrCount[inParen] = 0;
				agrOffset[inParen] = i;
				lwd[inParen].Empty();
				m_inParenCount++;
			}
			if (m_p[i] == ')')
			{
				if (inParen > 0)
					inParen--;
				scope = lscope[inParen];
				jsARG = false;
				m_inParenCount--;
			}
			if (m_p[i] == '{' /*&& lwd[inParen].length()*/)
			{
				if (scope != DB_SEP_STR + lwd[inParen])
					scope += DB_SEP_STR + lwd[inParen];
				if (inParen < STACKSZ)
					inParen++;
				inMethod[inParen] = inMethod[inParen - 1];
				inAssignment[inParen] = FALSE;
				agrCount[inParen] = 0;
				agrOffset[inParen] = i;
				lwd[inParen].Empty();
				m_inParenCount = 0;
			}
			if (m_p[i] == '}')
			{
				preScope(scope);
				if (inParen > 0)
					inParen--;
				scope = lscope[inParen];
			}
			// get unquotes string
			if (inString && inString == m_p[i])
			{
				inString = '\0';
				continue;
			}
			if (commentBlock && m_p[i] == '*' && m_p[i + 1] == '/')
			{
				commentBlock = FALSE;
			}
			if (commentLine || commentBlock || inString)
				continue;
		}
	}
#ifdef _DEBUG
	if (inParen && m_writeToDFile)
	{
		MessageBeep(0xffffffff);
		SetStatus("Scope hosed");
	}
#endif // _DEBUG
	if (inString)
		scope = "String";
	else if (commentLine || commentBlock)
		scope = "Comment";
	if (inParen)
	{
		m_argTemplate = def(lwScope[inParen - 1]);
		m_argParenOffset = agrOffset[inParen];
		m_argCount = agrCount[inParen];
	}
	if (Is_Tag_Based(FileType()) && m_HTML_Scope != HTML_inScript)
		scope = "Comment";
	m_lastScope = scope;
	return TRUE;
}

DType* MultiParse::FindExact(LPCSTR symScope)
{
	const WTString sym = StrGetSym(symScope);
	const WTString scope = StrGetSymScope(symScope);
	DB_READ_LOCK;
	DEFTIMER(FindSymTimer);
	try
	{
		WTString bcl;
		FindData fds(&sym, &scope);
		DType* data = g_pGlobDic->Find(&fds);
		// pick out reserved words
		if (data)
		{
			switch (data->MaskedType())
			{
			case CLASS: // added for local typedefs
				if (!data->Def().contains("typedef"))
					break;
			case TYPE:
			case RESWORD:
				return data;
			}
		}
		data = m_pLocalDic->Find(&fds);
		if (!data || (data->MaskedType() == CLASS)) // ignore forward class defs
			data = SDictionary()->Find(&fds);
		return data;
	}
	catch (...)
	{
		VALOGEXCEPTION("MP2:");
		ASSERT(FALSE);
		Log("Exception caught in FindSym\n");
		// just make sym undefined
		return NULL;
	}
}

static WTString GetStringTableLine(LPCSTR p, int i)
{
	for (; i > 0 && !strchr("\n", p[i - 1]); i--)
		; // get bol
	for (; p[i] && strchr(" \t", p[i]); i++)
		; // eat indent
	LPCSTR endChars = "\r\n";
	p = &p[i];
	int quoteCnt = 0;
	for (i = 0; p[i] && !strchr(endChars, p[i]); i++)
	{
		if (p[i] == '\"')
			++quoteCnt;
	}

	if (!(quoteCnt % 2))
	{
		// check to see if string def is on following line
		const int oldi = i;
		// goto next line
		for (; p[i] && strchr(endChars, p[i]); i++)
			;
		// read next line
		for (; p[i] && !strchr(endChars, p[i]); i++)
		{
			if (p[i] == '\"')
				++quoteCnt;
		}

		if (!(quoteCnt % 2))
			i = oldi;
	}

	WTString theLine(p, i);
	if (quoteCnt % 2)
	{
		theLine.ReplaceAll("\r", " ", false);
		theLine.ReplaceAll("\n", " ", false);
	}
	return theLine;
}
