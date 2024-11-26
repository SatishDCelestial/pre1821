#include "stdafxed.h"
#include "mparse.h"
#include "foo.h"
#include "timer.h"
#include "macroList.h"
#include "wtcsym.h"
#include "DevShellAttributes.h"
#include "project.h"
#include "Import.h"
#include "file.h"
#include "FileTypes.h"
#include "wt_stdlib.h"
#include "Settings.h"
#include "FileId.h"
#include "Registry.h"
#include "FileFinder.h"
#include "VAParse.h"
#include "LogElapsedTime.h"
#include "RegKeys.h"

using OWL::string;
using OWL::TRegexp;

static const char g_ignorePPChs[] = "(";
static const char g_ignorePPChs2[] = ",";

struct resWord
{
	const char* keyword;
	int keyWordLen;
	const struct resWord* nextSet;
	const char* ignoreChs;
};

static const resWord onOffResWords[] = {{"on", 2}, {"off", 3}, {""}};

static const resWord ifResWords[] = {{"defined", 7}, {""}};

static const resWord pragmaPackResWords[] = {{"push", 4}, {"pop", 3}, {""}};

static const resWord pragmaComponentBrowserResWords[] = {{"on", 2, pragmaComponentBrowserResWords, g_ignorePPChs2},
                                                         {"off", 3, pragmaComponentBrowserResWords, g_ignorePPChs2},
                                                         {"references", 10},
                                                         {""}};

static const resWord pragmaComponentResWords[] = {{"browser", 7, pragmaComponentBrowserResWords, g_ignorePPChs2},
                                                  {"minrebuild", 10, onOffResWords, g_ignorePPChs2},
                                                  {""}};

static const resWord pragmaCommentResWords[] = {{"compiler", 8}, {"exestr", 6}, {"lib", 3}, {"linker", 6}, {"user", 4}, {""}};

static const resWord pragmaWarningResWords[] = {{"disable", 7}, {"default", 7}, {"once", 4}, {"error", 5}, {"push", 4}, {"pop", 3}, {""}};

static const resWord pragmaResWords[] = {{"alloc_text", 10},
                                         {"comment", 7, pragmaCommentResWords, g_ignorePPChs},
                                         {"init_seg1", 9},
                                         {"optimize", 8, onOffResWords, g_ignorePPChs},
                                         {"auto_inline", 11, onOffResWords, g_ignorePPChs},
                                         {"component", 9, pragmaComponentResWords, g_ignorePPChs},
                                         {"inline_depth", 12},
                                         {"pack", 4, pragmaPackResWords, g_ignorePPChs},
                                         {"bss_seg", 7},
                                         {"data_seg", 8},
                                         {"inline_recursion", 16, onOffResWords, g_ignorePPChs},
                                         {"pointers_to_members1", 20},
                                         {"check_stack", 11, onOffResWords, g_ignorePPChs},
                                         {"function", 8},
                                         {"intrinsic", 9},
                                         {"setlocale", 9},
                                         {"code_seg", 8},
                                         {"hdrstop", 7},
                                         {"message", 7},
                                         {"vtordisp1", 9},
                                         {"const_seg", 9},
                                         {"include_alias", 13},
                                         {"once", 4},
                                         {"warning", 7, pragmaWarningResWords, g_ignorePPChs},
                                         {""}};

static const resWord preprocResWords[] = {{"define", 6},
                                          {"elif", 4, ifResWords, g_ignorePPChs},
                                          {"else", 4},
                                          {"endif", 5},
                                          {"error", 5},
                                          {"ifdef", 5},
                                          {"ifndef", 6},
                                          {"if", 2, ifResWords, g_ignorePPChs},
                                          {"import", 6},
                                          {"include", 7},
                                          {"line", 4},
                                          {"undef", 5},
                                          {"pragma", 6, pragmaResWords},
                                          {""}};

int ishexcsym(int c)
{
	if (wt_isxdigit(c) || c == 'x')
		return 1;
	return 0;
}

static const uint s_macro1scope = WTHashKey(":1");

DType* MultiParse::GetMacro(const WTString& macro)
{
	if (macro.IsEmpty())
		return NULL;

	DType* pFirstHit = m_pLocalDic->FindExact(macro, s_macro1scope, VaMacroDefArg, false); // use local macros first

	DType* pMacro = nullptr;
	if (pFirstHit && pFirstHit->IsSysLib() && !m_isSysFile)
		; // [case: 115155] m_pLocalDic could have temporary syslib macros before load of idx data
	else
		pMacro = pFirstHit;

	if (!pMacro && !m_isSysFile) // don't use project macros when parsing system files
		pMacro = g_pGlobDic->FindExact(macro, s_macro1scope, FALSE, VaMacroDefArg, false);
	if (!pMacro) // look for system macros
		pMacro = SDictionary()->FindExact(macro, s_macro1scope, FALSE, VaMacroDefArg, false);
	if (!pMacro)
		pMacro = pFirstHit;
	if (pMacro && FileType() == CS && !pMacro->IsDontExpand())
		return NULL;
	if (pMacro && g_loggingEnabled && -1 != pMacro->Def().Find('\f'))
		vLog("warn: MP::GM found concat mac def");
	return pMacro;
}

static const uint s_macro2scope = WTHashKey(":2");

DType* MultiParse::GetMacro2(const WTString& macro)
{
	if (macro.IsEmpty())
		return NULL;

	DType* pFirstHit = m_pLocalDic->FindExact(macro, s_macro2scope, VaMacroDefNoArg, false); // use local macros first

	DType* pMacro = nullptr;
	if (pFirstHit && pFirstHit->IsSysLib() && !m_isSysFile)
		; // [case: 115155] m_pLocalDic could have temporary syslib macros before load of idx data
	else
		pMacro = pFirstHit;

	if (!pMacro && !m_isSysFile) // don't use project macros when parsing system files
		pMacro = g_pGlobDic->FindExact(macro, s_macro2scope, FALSE, VaMacroDefNoArg, false);
	if (!pMacro) // look for system macros
		pMacro = SDictionary()->FindExact(macro, s_macro2scope, FALSE, VaMacroDefNoArg, false);
	if (!pMacro)
		pMacro = pFirstHit;
	if (!pMacro)
		pMacro = GetMacro(macro); // search :1 scope
	if (pMacro && FileType() == CS && !pMacro->IsDontExpand())
		return NULL;
	if (pMacro && g_loggingEnabled && -1 != pMacro->Def().Find('\f'))
		vLog("warn: MP::GM2 found concat mac def");
	return pMacro;
}

void MultiParse::AddMacro(LPCSTR macro, LPCSTR macrotxt, BOOL forExpandAllOnly /*= FALSE */)
{
	if (gShellAttr->IsDevenv11OrHigher())
	{
		// [case 61468] we may just need to change the order of include files in dev11 so that it picks up the correct
		// #define first
		if (strcmp(macro, "std") == 0)
			return;
	}

	const uint macType = uint(forExpandAllOnly ? VaMacroDefNoArg : VaMacroDefArg);
	const WTString macroScope = forExpandAllOnly ? ":2:" : ":1:";

	uint attrs = V_HIDEFROMUSER;

	if (FileType() == Src)
		attrs |= V_LOCAL;
	else if (m_isSysFile)
	{
		_ASSERTE(mFormatAttrs & V_SYSLIB);
		attrs |= mFormatAttrs; // mFormatAttrs == V_SYSLIB and possibly V_VA_STDAFX for misc/stdafx files
	}
	else
		attrs |= V_INPROJECT;

	if (!m_writeToDFile)
		return;

	// Changing the g_ReservedWords->IsReserved call to ::IsReservedWord breaks 5 unit tests
	if (g_ReservedWords->IsReserved(macro))
	{
		// g_ReservedWords is a limited list.
		// Its use here allows more symbols to be redefined than if IsReservedWord were used.
		// The unit tests that broke were related to definitions that used or redefined:
		// __declspec _declspec const
		return;
	}

	if (!macrotxt || !macrotxt[0])
		macrotxt = " ";

	DType* pMacro = GetMacro(macro);
	if (pMacro)
	{
		if (!pMacro->IsVaStdAfx()) // don't override misc/stdafxva.h macros
		{
			if ((attrs & V_LOCAL) && !pMacro->HasLocalFlag())
				pMacro = NULL; // allow local macros
			else if ((attrs & V_INPROJECT) && !pMacro->inproject())
				pMacro = NULL; // allow Project macros to override system macros
		}
	}

	// Only add first -- revisit this for [case: 88306]
	if (!pMacro)
	{
		if (mFormatAttrs & V_VA_STDAFX)
		{
			// [case: 70229] maintain attribute for macro processing
			attrs |= V_VA_STDAFX;
		}

		// #parserMacrosAreAddedTwiceHere
		// must add immediately so we see the macro for the rest of the file being parsed.
		// the entries added by the add() calls are removed by
		// MultiParse::ClearTemporaryMacroDefs() before the DBOut entries are
		// read in after the parse.  [case: 115155]
		// Double entry does not occur when loading from cache.

		if (m_parseType == ParseType_Locals || m_parseType == ParseType_GotoDefs)
		{
			++mDbOutMacroLocalCnt;
			m_pLocalDic->add(macroScope + macro, WTString(macrotxt), macType, attrs);
		}
		else if (!m_parseAll)
		{
			if (attrs & V_SYSLIB)
			{
				if (attrs & V_VA_STDAFX)
					SDictionary()->add(macroScope + macro, WTString(macrotxt), macType, attrs, 0, GetFileID());
				else
				{
					// [case: 115155]
					// add immediate entries to local db so that they can be
					// easily purged in ClearTemporaryMacroDefs(), without
					// blocking access to SysDic, before load of idx data
					++mDbOutMacroSysCnt;
					m_pLocalDic->add(macroScope + macro, WTString(macrotxt), macType, attrs);
				}
			}
			else
			{
				if (attrs & V_VA_STDAFX)
					g_pGlobDic->add(macroScope + macro, WTString(macrotxt), macType, attrs, 0, GetFileID());
				else
				{
					// [case: 115155]
					++mDbOutMacroProjCnt;
					// add immediate entries to local db so that they can be
					// easily purged in ClearTemporaryMacroDefs(), without
					// blocking access to g_pGlobDic, before load of idx data
					m_pLocalDic->add(macroScope + macro, WTString(macrotxt), macType, attrs);
				}
			}
		}

		if (m_parseType != ParseType_GotoDefs)
			DBOut(macroScope + macro, WTString(macrotxt), macType, attrs, m_line);
	}
	else if (m_parseType == ParseType_Locals && !pMacro->DbFlags())
	{
		// [case: 100004]
		// this is where local macros are serialized.
		// they were added to hashtable as part of global parse (ParseType_GotoDefs), but not serialized at that point.
		// !DbFlags means entry is in hashtable but wasn't read out of the db file.
		DBOut(macroScope + macro, WTString(macrotxt), macType, attrs, m_line);
	}
}

#pragma warning(disable : 4129)
CStringW GetIncFileStr(LPCSTR incln, BOOL& isSystem)
{
	while (*incln && wt_isspace(*incln))
		incln++;

	if (wt_isalpha(*incln))
		return CStringW(); // boost: #include BOOST_PP_ITERATE()

	while (*incln && !strchr("\"<>()", *incln))
		incln++;
	isSystem = (*incln == '<');
	if (*incln)
	{
		++incln;
		if ((isSystem && *incln == '>') || (!isSystem && *incln == '\"'))
			return CStringW(); // empty #include
		return CStringW(TokenGetField(incln, "\"<>").Wide());
	}
	return CStringW();
}

void MultiParse::AddPPLn()
{
	LogElapsedTime let("addppln");
	const CStringW fname(GetFilename());
	const char* p = &m_p[m_cp];
	while (*p == ' ' || *p == '\t')
		p++;
	if (m_writeToDFile)
	{
		if ((strncmp("include", p, 7) == 0)
#ifdef RECORD_IDL_IMPORTS
		    || (Idl == FileType() && StartsWith(p, "import"))
#endif // RECORD_IDL_IMPORTS
		)
		{
			// This block is partially duplicated in VAParseDirectiveC::OnDirective #includeDirectiveHandling
			BOOL isSysinclude = FALSE;
			CStringW file = ::GetIncFileStr(&p[7], isSysinclude);
			if (file.GetLength())
			{
				WTString origFile(file);
				file = gFileFinder->ResolveInclude(file, ::Path(fname), !isSysinclude);
				WTString includeDirectiveDef;
				if (file.GetLength())
				{
					file = ::MSPath(file);
					if (Binary == ::GetFileType(file))
					{
						if (!gShellAttr->SupportsCImportDirective())
							return; // Import only works if vc6 is installed.
						file = ::Import(file);
					}
					else if (!IsIncluded(file))
						ParseGlob((LPVOID)(LPCWSTR)file);

					includeDirectiveDef = gFileIdManager->GetFileIdStr(file);
				}
				else
				{
					CatLog("Parser.FileName", (WTString("#Include not found: ") + origFile + " in " + WTString(fname)).c_str());
					int pos = origFile.FindOneOf("\r\n");
					if (-1 != pos)
					{
						// [case: 116558]
						// include directive that is unterminated, eat line break
						origFile = origFile.Left(pos);
					}
					includeDirectiveDef = origFile + " (unresolved)";
					while ((pos = includeDirectiveDef.FindOneOf("/\\")) != -1)
						includeDirectiveDef = includeDirectiveDef.Mid(pos + 1);
				}

				// [case: 226] add entry even if we can't locate file
				DBOut(gFileIdManager->GetIncludeSymStr(fname), includeDirectiveDef, vaInclude,
				      V_IDX_FLAG_INCLUDE | V_HIDEFROMUSER, m_line);
				DBOut(WTString(gFileIdManager->GetIncludedByStr(file)), includeDirectiveDef, vaIncludeBy,
				      V_HIDEFROMUSER, m_line);
			}
			return;
		}
		else if (strncmp("define", p, 6) == 0)
		{
			AddPPLnMacro();
		}
	}

	// now handle coloring of line
	bool isImport = strncmp("import", p, 6) == 0;
	bool isInclude = strncmp("include", p, 7) == 0;
	bool isUsing = strncmp("using", p, 5) == 0;
	if (isInclude || isImport || isUsing)
	{
		if (isInclude)
		{
			p = strstr(p, "include");
			p += 7;
		}
		else if (isImport)
		{
			p = strstr(p, "import");
			p += 6;
		}
		else if (isUsing)
		{
			p = strstr(p, "using");
			p += 5;
		}
		while (*p == ' ' || *p == '\t')
			p++;
		// get filename
		WTString preprocFile;
		int i;
		for (i = p[0] ? 1 : 0; p[i] && p[i] != '"' && p[i] != '>' && p[i] != '\r' && p[i] != '\n'; i++)
			preprocFile += p[i];
		char ch = *p;
		if (ch == '"' || ch == '<')
		{
			char toStr[2] = " ";
			toStr[0] = ch;
			ReadTo(toStr, RESWORD); // output the #include/import
			if (m_writeToDFile && isUsing)
			{
			}
			else if (m_writeToDFile) // don't process includes on scope() calls
			{
				// the next couple of lines are from ProcessIncludeLn()
				BOOL doLocalSearch = FALSE;
				// mTypeAttrs will NEVER have V_SYSLIB set (it's either 0 or V_INPROJECT)
				if (ch == '\"' && !(mTypeAttrs & V_SYSLIB))
					doLocalSearch = TRUE;
				CStringW preprocFileW(preprocFile.Wide());
				preprocFileW = gFileFinder->ResolveInclude(preprocFileW, ::Path(fname), doLocalSearch);
				if (preprocFileW.GetLength())
				{
					toStr[0] = (p[i] ? p[i] : '"');
					ReadTo(toStr, STRING); // output filename as string
				}
				else
				{
					toStr[0] = (p[i] && p[i + 1] ? p[i + 1] : '"');
					if (m_showRed)
						ReadTo(toStr, UNDEF); // output filename as undefined
					else
						ReadTo(toStr, CTEXT);
				}
			}
			if (m_cp == m_len)
			{
				// for minihelp and so that caret movement doesn't cause loss of formatting
				m_type = PREPROCSTRING;
				mTypeAttrs = 0;
				m_lastScope = ":PP:"; // "PreProc";
				return;
			}
			ReadTo("\n", DEFINE, V_INPROJECT);
		}
		else
		{
			ReadTo("\n", RESWORD); // output the #include/import
		}
		return;
	}
	else
	{
		const uint defType = DEFINE;
		const uint attrs = V_INPROJECT;

		// do preproc keyword coloring - iterate thru keywords that start with #
		const resWord* wordList = preprocResWords;
		for (int idx = 0; *p && wordList[idx].keyWordLen && m_cp < m_len;)
		{
			if (*p != wordList[idx].keyword[0] || strncmp(wordList[idx].keyword, p, (uint)wordList[idx].keyWordLen))
			{
				idx++;
				continue;
			}

			// prevent 'if' in 'ifxx' from being colored like a resword
			bool okLen = true;
			for (int pIdx = 0; pIdx <= wordList[idx].keyWordLen; pIdx++)
			{
				if (!*(p + pIdx))
				{
					okLen = false;
					break;
				}
			}
			if (okLen && *(p + wordList[idx].keyWordLen) && ISALNUM(*(p + wordList[idx].keyWordLen)))
			{
				idx++;
				continue;
			}

			// found a keyword match
			p += wordList[idx].keyWordLen;
			WTString toStr(wordList[idx].keyword);
			toStr += *p;
			ReadTo(toStr.c_str(), RESWORD); // output keyword
			if (m_cp >= m_len)
			{
				// so that caret movement doesn't cause coloring to be lost
				m_type = PREPROCSTRING;
				mTypeAttrs = 0;
				m_lastScope = ":PP:"; // "PreProc";
				return;
			}
			m_cp--; // because we added the character after the resword to toStr
			// eat any whitespace after keyword
			while (*p == ' ' || *p == '\t')
			{
				p++;
				m_cp++;
			}
			// skip over paren or comma
			while (wordList[idx].ignoreChs && WTStrchr(wordList[idx].ignoreChs, *p))
			{
				// inc over ignoreCh
				p++;
				m_cp++;
				// eat any whitespace after ignoreCh
				while (*p == ' ' || *p == '\t')
				{
					p++;
					m_cp++;
				}
			}

			// either return, do rest of line as preproc or check for more keywords
			if (m_p[m_cp] == '\n' || m_p[m_cp] == '\r')
			{
				// only do a line at a time
				ReadTo("\n", defType, attrs);
				if (m_cp >= m_len)
				{
					// so that caret movement doesn't cause coloring to be lost
					m_type = PREPROCSTRING;
					mTypeAttrs = 0;
					m_lastScope = ":PP:"; // "PreProc";
				}
				return;
			}
			else if (!wordList[idx].nextSet)
			{
				// continue to next loop to process rest of line
				break;
			}
			else
			{
				// check next word - start over using the next list of res words
				wordList = wordList[idx].nextSet;
				idx = 0;
			}
		}
		/// end preproc keyword loop

		// color operators, comments, strings and numbers in preproc lines
		// this is basically ReadTo just for DEFINE lines with no include or import
		while (m_p[m_cp])
		{
			if (m_p[m_cp] == '\\')
			{
				// handle line continuation ch
				m_cp++; // did the '\'
				// eat any spaces after '\' before '\n'
				// Found \r\r\n on multi-line macros?
				while (m_cp < m_len && (m_p[m_cp] == '\r' || m_p[m_cp] == ' ' || m_p[m_cp] == '\t'))
					m_cp++;
				if (m_cp >= m_len)
					break;
				if (m_p[m_cp++] == '\n')
				{
					m_line++;
					continue;
				}
			}
			else
			{
				if (m_p[m_cp] == '/')
				{
					// handle comments
					if (m_p[m_cp + 1] == '/')
					{
						m_cp++; // so that view whitespace and background color settings work properly
						ReadTo("\n", COMMENT);
						return;
					}
					else if (m_p[m_cp + 1] == '*')
					{
						m_cp++;                // so that view whitespace and background color settings work properly
						ReadTo("*/", COMMENT); // continue to close #define ...\n
						if (m_cp >= m_len)
						{
							m_type = PREPROCSTRING;
							mTypeAttrs = 0;
							m_lastScope = ":PP:"; // "PreProc";
							return;
						}
						if (m_p[m_cp] == '\n')
							return; // fixes problems in unix files`
					}
					m_cp++; // from for loop
				}
				else if (m_p[m_cp] == '\"')
				{
					// handle string literals
					m_cp++;
					ReadTo("\"", STRING);
					if (m_cp >= m_len)
					{
						m_type = PREPROCSTRING;
						mTypeAttrs = 0;
						m_lastScope = ":PP:"; // "PreProc";
						return;
					}
				}
				else if (m_p[m_cp] == '\'')
				{
					// handle character literals
					m_cp++;
					ReadTo("\'", STRING);
					if (m_cp >= m_len)
					{
						m_type = PREPROCSTRING;
						mTypeAttrs = 0;
						m_lastScope = ":PP:"; // "PreProc";
						return;
					}
				}
				else if (!(ISALNUM(m_p[m_cp]) || wt_isspace(m_p[m_cp]) || m_p[m_cp] == '#'))
				{
					// handle operators
					m_cp++; // from for loop
					if (m_cp >= m_len)
					{
						m_type = PREPROCSTRING;
						mTypeAttrs = 0;
						m_lastScope = ":PP:"; // "PreProc";
						return;
					}
				}
				else if (wt_isdigit(m_p[m_cp]))
				{
					// handle numbers
					// check last ch
					if (m_cp && !ISALNUM(m_p[m_cp - 1]))
					{
						// loop until end of number
						bool doRead = false;
						int localCp;
						for (localCp = m_cp; localCp < m_len; localCp++)
						{
							if (ishexcsym(m_p[localCp]))
								continue;
							if (ISALNUM(m_p[localCp]))
							{
								if (m_p[localCp] == 'L' || m_p[localCp] == 'U')
								{
									doRead = true;
									localCp++;
									if (localCp < m_len && (m_p[localCp] == 'L' || m_p[localCp] == 'U'))
										localCp++;
								}
							}
							else
								doRead = true;
							break;
						}
						// read to found pos
						if (doRead)
						{
							// instead of ReadTo - add one to mimic ReadTo so that SinkCFile works
							m_cp = localCp + 1;
							if (m_cp >= m_len)
								return;
							m_cp--; // now take back the extra
						}
						else
							m_cp++;
					}
					else
						m_cp++;
				}
				else if (m_p[m_cp] == 'd' && !strncmp("defined", &m_p[m_cp], 7))
				{
					// handle 'defined' keyword
					// prevent 'defined' in 'definedssss' from being colored
					if (!m_p[m_cp + 7] || !ISALNUM(m_p[m_cp + 7]))
					{
						// instead of ReadTo - add one to mimic ReadTo so that SinkCFile works
						m_cp += 8;
						if (m_cp >= m_len)
						{
							// so that caret movement doesn't cause coloring to be lost
							m_type = PREPROCSTRING;
							mTypeAttrs = 0;
							m_lastScope = ":PP:"; // "PreProc";
							return;
						}
						m_cp--; // now take back the extra
					}
					else
						m_cp++;
				}
				else
					m_cp++; // from for loop
				if (m_p[m_cp] == '\n')
				{
					m_line++;
					m_cp++;
					return;
				}
			}
		}
		ReadTo("\n", defType, attrs);
		if (m_cp == m_len)
		{
			// so that caret movement doesn't cause coloring to be lost
			m_type = PREPROCSTRING;
			mTypeAttrs = 0;
			m_lastScope = ":PP:"; // "PreProc";
		}
	}
}
#pragma warning(default : 4129)

void MultiParse::AddPPLnMacro()
{
#ifdef _DEBUG
	if (!m_writeToDFile)
	{
		_ASSERTE(!"AddPPLnMacro called incorrectly");
		return;
	}

	const char* pTest = &m_p[m_cp];
	while (*pTest == ' ' || *pTest == '\t')
		pTest++;

	if (strncmp("define", pTest, 6) != 0)
	{
		_ASSERTE(!"AddPPLnMacro called incorrectly");
		return;
	}
#endif

	LogElapsedTime let("addpplnmac");
	WTString macrotxtBuf;
	const char* p = FormatDef(DEFINE);
	// Do not add macros that are appended,
	// or we will end up with "class foo{ ...",
	// missing closing brace messing up scope of anything below
	// TODO: add support for any length macros
	uint len = strlen_u(p);
	// see case=25876 for a problem this truncate state causes (see MultiParse::FormatDef)
	const BOOL truncated = (len > 3 && strcmp("...", &p[len - 3]) == 0);
	p = strstr(p, "define");
	p += 6;
	while (*p == ' ' || *p == '\t')
		p++;

		// get sym name
#define MAX_SYMLEN 200
	char sym[MAX_SYMLEN + 1];
	int i = 0;
	for (; i < MAX_SYMLEN && ISALNUM(*p); i++)
		sym[i] = *p++;
	sym[i] = 0;

	// #FunkyMacroParsingRegistryFlags
	// [case: 11759] change 13064 introduced a new string value "NoDepthLimit"
	// than LimitMacro value "No" in change 7610.
	// I suspect that "NoDepthLimit" is broken for macros that depend on other
	// macros defined in the same file, in which case "No" is better for macro parsing.
	// (Similar problem for "notExpandAllOnly" below.)
	static const BOOL expandMacrosInMacro = GetRegValue(HKEY_CURRENT_USER, ID_RK_APP, "LimitMacro") == "NoDepthLimit";
	if (expandMacrosInMacro)
	{
		// Expand all macros in this macro, so we don't have to expand again later. case=11759
		// Save mp state because calls to VAParseMPMacroC::OnDirective() will change them.
		const int cp = m_cp;
		const int ln = m_line;
		const int bufLen = m_len;
		const LPCSTR buf = m_p;

		// Expand macros...
		macrotxtBuf = p;
		macrotxtBuf = VAParseExpandMacrosInDef(shared_from_this(), macrotxtBuf);
		p = macrotxtBuf.c_str();

		// Restore mp state.
		m_cp = cp;
		m_line = ln;
		m_p = buf;
		m_len = bufLen;
	}

	const CStringW fname(GetFilename());
	// #FunkyMacroParsingRegistryFlags
	if (truncated || (Psettings->m_skipLocalMacros && !m_isSysFile && -1 == fname.Find(L"stdafx")) ||
	    (m_parseType != ParseType_GotoDefs && m_parseType != ParseType_Globals &&
	     m_parseType != ParseType_Locals)) // don't add while editing...
	{
		// don't add local macros to be processed
		if (truncated)
			vLog("WARN: pplnmac truncate: %s\n", sym);
	}
	else if (strncmp(sym, "ON_", 3) == 0) // don't expand ON_xxx(...) for speed purposes
	{
	}
	else if (*p == '(') // add macro
	{
		token t = p;
		token args = t.read(")");
		int n;
		for (n = 1; args.length(); n++)
		{
			// use ~~~nn and macro arg so there is no chance of them creating a macro w same args
			WTString carg;
			carg.WTFormat("~~~%.2d", n);
			WTString s = args.read("( \t,");
			if (s.GetLength())
				t.ReplaceAll(s, carg, true);
		}

		TRegexp PoundExp("[ \t]*#[# \t]*");
		t.ReplaceAll(PoundExp, string(""));
		if (t.length())
		{
			// don't expand macros starting with paren like "#define foo(x) (x+3)
			LPCSTR c = t.c_str();
			for (; c && ++c && wt_isspace(*c);)
				;

			if (*c == '(')
			{
				// add these as well, but not expandable
				AddMacro(sym, &t.c_str()[1], TRUE);
			}
			else
				AddMacro(sym, &t.c_str()[1]);
		}
	}
	else
	{
		const WTString symDef(p);
		bool notExpandAllOnly = false;
		if (!p || !p[0])
			notExpandAllOnly = true; // allow expanding empty macros
		else if (strchr(p, '{') || strchr(p, '}'))
		{
			// expand this #define
			notExpandAllOnly = true;

			// the condition for this block is too shallow -- we should
			// really check the definition of any macros (used in this
			// definition) for '{' '}' also.  But those macros might not
			// have been defined yet (see NEXT_LOOP_IN_FUNC_EDITING
			// in FlowGraph.h defined in terms of NEXT_LOOP_EDITING).
			// (Similar problem for "NoDepthLimit" above.)

			// Override of LimitMacroParsing via hidden reg compensates for
			// this incomplete logic, as does Settings::mEnhanceMacroParsing
			// added for [case: 108472]
		}
		else if (-1 != fname.Find(L"stdafx"))
			notExpandAllOnly = true; // allow "#define DUMMY" to remove DUMMY in stdafx files
		else
		{
			const int len2 = symDef.GetLength();
			if (len2 > 1)
			{
				const char lastCh = symDef[len2 - 1];
				const char secondToLastCh = symDef[len2 - 2];
				if (gShellAttr->IsDevenv11OrHigher() && ':' == lastCh && ':' == secondToLastCh)
					notExpandAllOnly = true; // case 61468, expand this dev11 macro #define _STD ::std::
				else if (')' == lastCh && '(' == secondToLastCh)
					notExpandAllOnly = true; // [case: 67322] #define Foo ::baz::bar()->bah()
			}
		}

		// expand all symbols in stdafx[va]
		// #FunkyMacroParsingRegistryFlags
		if (Psettings->m_limitMacroParseLocal && -1 == fname.Find(L"stdafx")) // defaults to true
			;                       // this is the original way - don't change flag
		else if (!notExpandAllOnly) // otherwise override
		{
			// mTypeAttrs will NEVER have V_SYSLIB set (it's either 0 or V_INPROJECT)
			// #FunkyMacroParsingRegistryFlags
			if (Psettings->m_limitMacroParseSys && (mTypeAttrs & V_SYSLIB) && -1 == fname.Find(L"stdafx"))
				; // don't expand sys #defines if m_limitMacroParseSys is true unless '{' or '}'
			else
				notExpandAllOnly = true; // expand all #defines
		}

		AddMacro(sym, p, !notExpandAllOnly);
	}

	if (m_parseType != ParseType_GotoDefs)
	{
		WTString def("#define ");
		def += sym;
		def += *p ? p : " ";
		const uint attrs = mFormatAttrs & (V_SYSLIB | V_VA_STDAFX);
		DBOut(DB_SEP_STR + sym, def, DEFINE, attrs, m_line);
	}
}
