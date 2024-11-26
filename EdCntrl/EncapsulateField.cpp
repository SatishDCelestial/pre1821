#include "stdafxed.h"
#include "EncapsulateField.h"
#include "FOO.H"
#include "VARefactor.h"
#include "UndoContext.h"
#include "AutotextManager.h"
#include "FreezeDisplay.h"
#include "TraceWindowFrame.h"
#include "VAAutomation.h"
#include "CommentSkipper.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define FSL_SEPARATOR_CHAR '@'
#define FSL_SEPARATOR_STR "@"

BOOL PV_InsertAutotextTemplate(const WTString& templateText, BOOL reformat, const WTString& promptTitle = NULLSTR);

const char cStatusMsg[] = "Getter &name (or empty):";
const char csStatusMsg[] = "Property &name:";

#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
const char cStatusMsgRead[] = "&Read name (or empty):";
const char cStatusMsgWrite[] = "&Write name (or empty):";
const char cStatusMsgSetter[] = "Setter nam&e (or empty):";
#endif

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(EncapsulateFieldDlg, UpdateReferencesDlg)
ON_BN_CLICKED(IDC_CHECK_ENC_FIELD_PUBLIC_ACCESSORS, OnTogglePublicAccessors)
ON_BN_CLICKED(IDC_RADIO_ENC_FIELD_PRIVATE, OnMoveVarPrivate)
ON_BN_CLICKED(IDC_RADIO_ENC_FIELD_PROTECTED, OnMoveVarProtected)
ON_BN_CLICKED(IDC_RADIO_ENC_FIELD_NONE, OnMoveVarNone)
ON_BN_CLICKED(IDC_RADIO_ENC_FIELD_INTERNAL, OnMoveVarInternal)
ON_EN_CHANGE(IDC_EDIT_SECOND, OnChangeEdit)
#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
ON_BN_CLICKED(IDC_RADIO_ENC_FIELD_PROPERTY_CPPB, OnTypePropertyCppB)
ON_BN_CLICKED(IDC_RADIO_ENC_FIELD_GETSET_CPPB, OnTypeGetSetCppB)
ON_BN_CLICKED(IDC_RADIO_ENC_PROP_PUBLISHED_CPPB, OnMovePropPublishedCppB)
ON_BN_CLICKED(IDC_RADIO_ENC_PROP_PUBLIC_CPPB, OnMovePropPublicCppB)
ON_BN_CLICKED(IDC_RADIO_ENC_PROP_PRIVATE_CPPB, OnMovePropPrivateCppB)
ON_BN_CLICKED(IDC_RADIO_ENC_PROP_PROTECTED_CPPB, OnMovePropProtectedCppB)
ON_BN_CLICKED(IDC_RADIO_ENC_PROP_NONE_CPPB, OnMovePropNoneCppB)
ON_BN_CLICKED(IDC_CHK_READ_FIELD_CPPB, OnToggleReadFieldCppB)
ON_BN_CLICKED(IDC_CHK_READ_VIRTUAL_CPPB, OnToggleReadVirtualCppB)
ON_BN_CLICKED(IDC_CHK_WRITE_FIELD_CPPB, OnToggleWriteFieldCppB)
ON_BN_CLICKED(IDC_CHK_WRITE_VIRTUAL_CPPB, OnToggleWriteVirtualCppB)
#endif
ON_WM_SIZE()
END_MESSAGE_MAP()
#pragma warning(pop)

extern void GetGeneratedPropName(WTString& generatedPropertyName);

bool ErodeScope(WTString& scope, WTString name)
{
	name.TrimLeft();
	while ((!name.IsEmpty() && (name[0] == '*' || name[0] == '&')) ||
	       (name.GetLength() >= 5 && name.Left(5) == "const"))
	{
		if (name[0] == '*' || name[0] == '&')
			name = name.Mid(1);
		if (name.Left(9) == "constexpr")
			name = name.Mid(9);
		if (name.Left(9) == "consteval")
			name = name.Mid(9);
		if (name.Left(9) == "constinit")
			name = name.Mid(9);
		if (name.Left(12) == "_CONSTEXPR17")
			name = name.Mid(12);
		if (name.Left(22) == "_CONSTEXPR20_CONTAINER")
			name = name.Mid(22);
		if (name.Left(12) == "_CONSTEXPR20")
			name = name.Mid(12);
		if (name.Left(5) == "const")
			name = name.Mid(5);
		name.TrimLeft();
	}

	if (!scope.IsEmpty() && scope[0] == ':')
		scope = scope.Mid(1);
	WTString left;
	int separator = scope.Find(':');
	if (separator == -1)
		left = scope;
	else
		left = scope.Left(separator);

	if (left == name)
	{ // let's erode
		if (separator == -1)
			scope = "";
		else
			scope = scope.Mid(separator + 1);
		return true;
	}

	return false;
}

// symScope: DType style scope including name. get it by calling DType::SymScope()
// node:	 e.g. outline root node. get it by calling LineMarkers::Root()
int GetSymbolLineByFindingItsNodeInOutline(WTString& symScope, LineMarkers::Node& node)
{
	for (size_t idx = 0; idx < node.GetChildCount(); ++idx)
	{
		LineMarkers::Node& ch = node.GetChild(idx);
		FileLineMarker& marker = *ch;

		if (marker.mType == CLASS || marker.mType == STRUCT || marker.mType == NAMESPACE || marker.mType == VAR)
		{
			WTString currentName = marker.mText;
			currentName = TokenGetField(currentName, "(");
			currentName.ReplaceAll("::", FSL_SEPARATOR_STR);
			if (marker.mType != NAMESPACE)
				currentName.ReplaceAll(".", FSL_SEPARATOR_STR);
			currentName.ReplaceAll(" methods", "");
			currentName = TokenGetField(currentName, ":"); // for things like "VAScopeInfo : public DefFromLineParse"
			currentName = TokenGetField(currentName, ";"); // for variable declarations
			currentName.TrimRight();

			do
			{
				WTString currentNamePart;
				int multipleNS = currentName.Find('.');
				if (multipleNS != -1)
				{
					currentNamePart = currentName.Left(multipleNS);
					currentName = currentName.Mid(multipleNS + 1);
				}
				else
				{
					currentNamePart = currentName;
					currentName = "";
				}

				int separator = currentNamePart.ReverseFind(FSL_SEPARATOR_CHAR);
				if (separator != -1)
				{
					WTString currentScope = currentNamePart.Left(separator);
					currentScope = TokenGetField(currentScope,
					                             "="); // public astShellTypesListbox mShell = astShellTypesListbox.v0;
					currentScope.TrimRight();
					ErodeScope(symScope, currentScope);
					currentNamePart = currentNamePart.Right(currentNamePart.GetLength() - separator - 1);
				}
				if (marker.mType == VAR)
				{
					// parse if multiple variables on the same line
					std::vector<WTString> vars;
					int s;
					do
					{
						s = currentNamePart.ReverseFind(',');
						if (s != -1)
						{
							WTString part = currentNamePart.Mid(s + 1);
							part = TokenGetField(part, "="); // for things like Apple = 5
							part.TrimRight();
							vars.push_back(part);
							currentNamePart = currentNamePart.Left(s);
						}
						else
						{
							currentNamePart = TokenGetField(currentNamePart, "="); // for things like Apple = 5
							currentNamePart.TrimRight();
							vars.push_back(currentNamePart);
						}
					} while (s != -1);

					for (uint i = 0; i < vars.size(); i++)
					{
						vars[i] = TokenGetField(vars[i], "[");
						vars[i].Trim();
						ErodeScope(symScope, vars[i]);
					}
				}
				else
				{
					currentNamePart = TokenGetField(currentNamePart, "[");
					ErodeScope(symScope, currentNamePart);
				}
				if (symScope.IsEmpty())
					return (int)marker.mGotoLine;
			} while (currentName.GetLength());
		}

		int symbolLine = GetSymbolLineByFindingItsNodeInOutline(symScope, ch);
		if (symbolLine != -1)
			return symbolLine;
	}

	return -1;
}

// Find symbol name + chain + parens + * backwards
// Examples: "obj.", "(obj).", "(*obj).", "obj->", "(*obj).(ptr)->", "(", etc.
int ChainExtendBackward(const WTString& buf, int startPos, int fileType)
{
	int lastNonWhiteSpacePos = startPos;
	TCHAR lastNonWhiteSpaceChar = buf[startPos];
	CommentSkipper cs(fileType);
	int squares = 0; // open square brackets
	int parens = 0;  // open parens
	for (int i = startPos - 1; i >= 4; i--)
	{
		TCHAR c = buf[i];
		if (cs.IsCodeBackward(buf, i))
		{
			if (!ISCSYM(buf[i + 1]) && c == 'e' && buf[i - 1] == 's' && buf[i - 2] == 'l' && buf[i - 3] == 'e' &&
			    !ISCSYM(buf[i - 4]))
			{
				return lastNonWhiteSpacePos;
			}
			if (c == ')')
			{
				if (!ISCSYM(lastNonWhiteSpaceChar))
				{
					parens++;
					goto next;
				}
			}
			if (c == ']')
			{
				squares++;
				goto next;
			}
			if (c == '(')
			{
				parens--;
				if (parens < 0)
					parens = 0;
				else
					goto next;
			}
			if (c == '[')
			{
				squares--;
				if (squares < 0)
					squares = 0;
				else
					goto next;
			}
			if (parens || squares)
				goto next;
			if (c == '.' || (i > 0 && c == '>' && buf[i - 1] == '-') || (c == '-' && buf[i + 1] == '>'))
				goto next;
			if (c == '*')
				goto next;
			if (!ISCSYM(c) && !IsWSorContinuation(c))
				return lastNonWhiteSpacePos;

		next:
			if (!IsWSorContinuation(c))
			{
				lastNonWhiteSpacePos = i;
				lastNonWhiteSpaceChar = c;
			}
		}
	}

	return startPos;
}

// Line to move variable
// return pair 1/2: the line number to move. < 0 means don't move the variable.
// return pair 2/2: do we want to create private or protected section?

bool GetNextWord(CStringW& nextWord, int& charPointer, const CStringW& text)
{
	if (!ISCSYM(text[charPointer]))
	{
		for (; charPointer < text.GetLength(); charPointer++)
			if (text[charPointer] == L',')
			{
				charPointer++;
				break;
			}
	}

	nextWord.Empty();
	for (; charPointer < text.GetLength(); charPointer++)
	{
		auto c = text[charPointer];
		if (IsWSorContinuation(c))
			continue;
		if (c == '*' || c == '&')
			continue;
		if (c == 'c' && charPointer + 5 < text.GetLength() && text[charPointer + 1] == 'o' &&
		    text[charPointer + 2] == 'n' && text[charPointer + 3] == 's' && text[charPointer + 4] == 't' &&
		    (IsWSorContinuation(text[charPointer + 5]) || text[charPointer + 5] == '*'))
		{
			charPointer += 4;
			continue;
		}
		if (!ISCSYM(c))
			break;
		nextWord += c;
	}

	return !nextWord.IsEmpty();
}

extern int FindWholeWordInCodeOutsideBrackets(const WTString& str, WTString subStr, int from = 0, int to = INT_MAX);

bool IsVariableDeclaration_DoNotIncludePointerStuff(WTString fileBuf, long& beg, long end)
{
	EdCntPtr ed(g_currentEdCnt);
	CommentSkipper cs(ed->m_ftype);
	int angleBrackets = 0;
	enum
	{
		neutral,
		typeName,
		separator,
	} state = neutral;
	bool newSection = true;
	for (int i = beg; i < end; i++)
	{
		TCHAR c = fileBuf[i];
		if (cs.IsCode(c))
		{
			// skipping white space
			if (IsWSorContinuation(c))
			{
				newSection = true;
				continue;
			}

			// skipping angle brackets
			if (c == '<')
			{
				angleBrackets++;
				state = separator;
				newSection = false;
				continue;
			}
			if (c == '>')
			{
				angleBrackets--;
				if (angleBrackets == 0)
				{
					state = typeName;
					newSection = true;
				}
				else
				{
					state = separator;
					newSection = false;
				}

				continue;
			}
			if (angleBrackets > 0)
			{
				newSection = true;
				continue;
			}

			if (c == '&' || c == '*' ||
			    (i + 5 < end && c == 'c' && fileBuf[i + 1] == 'o' && fileBuf[i + 2] == 'n' && fileBuf[i + 3] == 's' &&
			     fileBuf[i + 4] == 't' && (IsWSorContinuation(fileBuf[i + 5]) || fileBuf[i + 5] == '*')))
			{
				beg = i;
				return true;
			}

			if (c == '(') // function call
				return false;

			// invalid chars
			if (ISCSYM(c))
			{
				if (newSection && state == typeName)
				{
					beg = i;
					return true;
				}

				newSection = false;
				state = typeName;
				continue;
			}

			if (c == '.' || c == ':')
			{
				newSection = false;
				state = separator;
				continue;
			}

			return false;
		}
	}

	return false;
}

void NarrowScope(const WTString& outlineText, WTString name, int& selFrom, int& selTo)
{
	EdCntPtr ed(g_currentEdCnt);
	if (!ed)
		return;

	int origSelFrom = selFrom;
	int origSelTo = selTo;
	WTString buf = ed->GetBuf();
	// type = GetTypeFromDef(outlineText, ed->m_ftype);
	int varPos = FindWholeWordInCodeOutsideBrackets(buf, name, origSelFrom, origSelTo);
	if (varPos == -1)
		return; // leave the scope as it is

	bool commaFound = false;

	// extend backward
	long beg = selFrom;
	long end = selTo;
	IsVariableDeclaration_DoNotIncludePointerStuff(buf, beg, end);
	selFrom = beg;
	for (int i = varPos - 1; i >= beg; i--)
	{
		TCHAR c = buf[i];
		selFrom = i;
		if (c == ',')
		{
			commaFound = true;
			break;
		}
	}

	// extend forward
	int nameLen = varPos + name.GetLength();
	selTo = nameLen;
	for (int i = nameLen; i < origSelTo; i++)
	{
		TCHAR c = buf[i];
		if (c == ';')
		{
			selTo = i;
			break;
		}
		if (c != ',')
		{
			selTo = i + 1;
			continue;
		}
		else
		{
			if (!commaFound)
			{
				selTo = i + 1; // including comma
				for (int j = selTo; j < origSelTo; j++)
				{
					if (IsWSorContinuation(buf[j]))
						selTo = j + 1;
					else
						break;
				}
			}
			break;
		}
	}
}

#define ITER_2
#define ITER_3

BOOL EncapsulateField::Encapsulate(DType* sym, VAScopeInfo_MLC& info, WTString& methodScope, CStringW& filePath)
{
	UndoContext undoContext("VA Encapsulate Field");
	FreezeDisplay f(FALSE);

	const WTString snippetTitle("Refactor Encapsulate Field");
	RawSnippet = gAutotextMgr->GetSource(snippetTitle);

#ifdef ITER_3
	// *** step 1: update references (C++ and C#) [case: 91192]
	TraceScopeExit tse("Encapsulate Field exit");

	if (!sym)
		return FALSE;

	int fileType = info->FileType();
	std::pair<bool, long> hidden = std::pair<bool, int>(false, -1);
	sSetterGetter setterGetter;
	if ((IsCFile(fileType) && sym->Def().Find('[') == -1) || fileType == CS)
	{ // see case 1217
		setterGetter = GetSetterGetter(RawSnippet, sym);
		setterGetter.Valid = true;
#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
		// in case of C++ Builder always open the dialog since user have to be able to choose between encapsulate as
		// property or encapsulate as getter/setter
#else
		if (!setterGetter.GetterName.IsEmpty())
#endif
		{
			hidden = IsHiddenSection(sym);
			if (DoModal(std::make_shared<DType>(sym), setterGetter, hidden.first) == FALSE)
				return FALSE;

			// change the snippet with user-inputed names - $GeneratedPropertyName$ connected with setter and getter
			// names is overwritten in the process, so input dialog will not be triggered unnecessarily. (but it can
			// still show up e.g. if we put user inputs to the snippet)
			ChangeRawSnippet(setterGetter, sym, fileType);
		}
	}
#endif

	// *** step 2: find public section or create one (C++ only) [case: 7857]
	std::pair<int, bool> insertLn = std::pair<int, bool>(-1, false);
	if (IsCFile(fileType) && Psettings->mEncFieldPublicAccessors)
	{
		EdCntPtr ed(g_currentEdCnt);
		if (ed)
		{
			WTString fileText(ed->GetBuf(TRUE));
			MultiParsePtr mp = ed->GetParseDb();
			LineMarkers markers; // outline data
			GetFileOutline(fileText, markers, mp);
			WTString symScope = sym->SymScope();
			int ln = GetSymbolLineByFindingItsNodeInOutline(symScope, markers.Root()); // sym->Line();
			FindLnToInsert find((ULONG)ln);
			insertLn = find.GetLnToInsert(markers.Root());
		}
	}

	CStringW file = FileFromDef(sym);

	WTString def = sym->Def();
	if (!Is_VB_VBS_File(fileType))
		def += ";";
	def = DecodeTemplates(def);
	info->GetDefFromLine(def, 1);
	info->ParseEnvArgs();
	// Navigate after we have all of the old scope info saved. case=50344
	if (!GotoDeclPos(methodScope, file.GetLength() ? file : filePath))
		return FALSE;
	if (insertLn.first > -1)
	{
		EdCntPtr ed(g_currentEdCnt);
		if (ed)
			ed->SetPos((uint)ed->GetBufIndex((long)ed->LinePos(insertLn.first)));
		if (insertLn.second)
		{ // create new section
			const WTString lineBreak(ed->GetLineBreakString());
			RawSnippet = "public:" + lineBreak + RawSnippet;
		}
	}

#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
	// *** if it is CppB property then handle it here; otherwise just proceed as normal
	if (Psettings->mEncFieldTypeCppB == tagSettings::ceft_property)
	{
		EdCntPtr ed(g_currentEdCnt);
		if (ed)
		{
			WTString fileText(ed->GetBuf(TRUE));
			MultiParsePtr mp = ed->GetParseDb();
			LineMarkers markers; // outline data
			GetFileOutline(fileText, markers, mp);
			WTString symScope = sym->SymScope();
			int ln = GetSymbolLineByFindingItsNodeInOutline(symScope, markers.Root());
			FindLnToInsert find((ULONG)ln);
			int selFrom = -1;
			int selTo = -1;
			insertLn = find.GetLnToMoveCppB(markers.Root(), sym->Sym(), selFrom, selTo);
			if (insertLn.first < 0) // unexpected return value
				return FALSE;       // should not happen

			WTString fieldDefinitionTxt = fileText.Mid(selFrom, selTo - selFrom);
			int endLocation = fieldDefinitionTxt.find_last_of(";");
			if (endLocation > 0)
			{
				selTo = selFrom + endLocation + 1; // do not select new lines after last character line
				fieldDefinitionTxt = fileText.Mid(
				    selFrom,
				    selTo - selFrom); // get new text that will be replaced so that offset can be calculated correctly
			}

			ed->SetSel((long)selFrom, (long)selTo);

			bool isBodyCreated = false;
			WTString logicalSymName = ExtractLogicalVariableNameCppB(sym->Sym());
			WTString propertySnippetHead =
			    ConstructPropertySnippetCppB_Head(setterGetter, logicalSymName, sym->GetVisibility(), isBodyCreated);

			const WTString propertyHeadTxt(info->ExpandMacros(propertySnippetHead, true, false));
			if (!PV_InsertAutotextTemplate(propertyHeadTxt, TRUE, "Refactor Encapsulate Property"))
				return FALSE;

			if (!isBodyCreated)
			{
				WTString propertySnippetBody = ConstructPropertySnippetCppB_Body(setterGetter, logicalSymName);
				WTString propertyBodyTxt(info->ExpandMacros(propertySnippetBody, true, false));

				// calculate total offset for body location; depends on the head text and original field definition in
				// case of multiline
				int bodyOffset = 0;
				if (ln < insertLn.first)
					bodyOffset = propertySnippetHead.GetTokCount('\n') - fieldDefinitionTxt.GetTokCount('\n');

				ed->SetPos((uint)ed->GetBufIndex((long)ed->LinePos(insertLn.first + bodyOffset)));
				if (insertLn.second)
				{
					SymbolVisibility accessorsVisibility;
					if (Psettings->mEncFieldMoveVariable == tagSettings::cvv_private)
						accessorsVisibility = vPrivate;
					else
						accessorsVisibility = vProtected;

					WTString prependScopeTxt;
					prependScopeTxt.append(::GetVisibilityString(accessorsVisibility, gTypingDevLang));
					prependScopeTxt.append(":\n");
					propertyBodyTxt = prependScopeTxt + propertyBodyTxt;
				}

				propertyBodyTxt = "\n" + propertyBodyTxt + "\n\n";
				ed->InsertW(propertyBodyTxt.Wide(), false, vsfAutoFormat, false);
			}

			return TRUE;
		}
	}
#endif

	const WTString txt(info->ExpandMacros(RawSnippet, true, false));
	if (!PV_InsertAutotextTemplate(txt, TRUE, snippetTitle))
		return FALSE;
#ifdef ITER_2
	if (insertLn.first < 0 && !setterGetter.Valid ||
	    (!setterGetter.GetterName.IsEmpty() && !setterGetter.SetterName.IsEmpty()))
	{ // methods were created next to the variable - we are in public section
		// *** step 3: "cut"-and-"paste" to private/protected section (without clipboard) (C++ only) [case: 91192]

		// build new outline (I mean markers) and find the symbol (sym) by qualification in the new outline to get it's
		// new line number (it may or may not changed)	and its exact from-to location
		if (IsCFile(fileType) && Psettings->mEncFieldMoveVariable != tagSettings::cvv_no)
		{
			EdCntPtr ed(g_currentEdCnt);
			if (ed)
			{
				WTString fileText(ed->GetBuf(TRUE));
				MultiParsePtr mp = ed->GetParseDb();
				LineMarkers markers; // outline data
				GetFileOutline(fileText, markers, mp);
				WTString symScope = sym->SymScope();
				int ln = GetSymbolLineByFindingItsNodeInOutline(symScope, markers.Root()); // sym->Line();
				FindLnToInsert find((ULONG)ln);
				int selFrom = -1;
				int selTo = -1;
				bool oneLineSelection = false;
				int multiType = -1;
				insertLn = find.GetLnToMove(markers.Root(), sym->Sym(), selFrom, selTo, oneLineSelection,
				                            multiType); // getting the destination of the clipboardless cut-paste
				if (insertLn.first < 0)                 // unexpected return value
					return TRUE; // should not happen since we should have already been in a public section

				const WTString lineBreak(ed->GetLineBreakString());

				// [case: 141952] - calculate correct selTo in case of empty lines in beginning of the snippet
				if (!oneLineSelection && multiType == -1)
				{
					WTString fieldDefinitionTxt = fileText.Mid(selFrom, selTo - selFrom);
					fieldDefinitionTxt.TrimRight();
					fieldDefinitionTxt = fieldDefinitionTxt + lineBreak;
					selTo = selFrom + fieldDefinitionTxt.length();
				}

				// clipboardless cut: select
				if (oneLineSelection && selTo && (fileText[selTo - 1] == '\r' || fileText[selTo - 1] == '\n'))
					selTo--;
				if (oneLineSelection && selTo && (fileText[selTo - 1] == '\r' || fileText[selTo - 1] == '\n'))
					selTo--;
				if (oneLineSelection && selFrom && (fileText[selFrom - 1] == '\r' || fileText[selFrom - 1] == '\n'))
				{
					for (int i = selFrom; i < fileText.GetLength(); i++)
					{
						if (!IsWSorContinuation(fileText[i]))
							break;
						selFrom = i + 1;
					}

					for (int i = selTo; i < fileText.GetLength(); i++)
					{
						if (!IsWSorContinuation(fileText[i]))
							break;
						selTo = i + 1;
					}
				}

				bool needsSpace = selFrom > 0 && ISCSYM(fileText[selFrom - 1]) &&
				                  ISCSYM(fileText[selTo]); // this 2 characters will remain after deleting the text from
				                                           // the old place. see encapsule_field_order34
				ed->SetSel((long)selFrom, (long)selTo);

				// clipboardless cut: copy
				WTString copied = fileText.Mid(selFrom, selTo - selFrom);
				int copyPositionOffset =
				    copied.GetTokCount('\n') -
				    1; // [case: 141953] - need to calculate offset for case when declaration is split to multiple lines
				if (copyPositionOffset < 0)
					copyPositionOffset = 0;

				// clipboardless cut: delete
				if (needsSpace)
					ed->Insert(" ", false);
				else
					ed->Insert("", false);

				// assembling new type in buffer
				if (multiType != -1)
				{ // originates from a comma-separated multi-type e.g. int a, b, c;
					// trim buffer before inserting in editor
					copied.Trim();
					if (!copied.IsEmpty())
					{
						if (copied[copied.GetLength() - 1] == ',')
							copied = copied.Left(copied.GetLength() - 1);
						if (copied[0] == ',')
							copied = copied.Mid(1);
					}
					copied.Trim();

					// getting the type
					WTString type;
					CommentSkipper cs(fileType);
					long beg = multiType;
					long end = selTo;
					WTString typeFrom = fileText.Mid(multiType, selTo - multiType);
					typeFrom.TrimLeft();
					type = GetTypeFromDef(typeFrom, ed->m_ftype);
					IsVariableDeclaration_DoNotIncludePointerStuff(fileText, beg, end);
					WTString type2 = fileText.Mid(multiType, beg - multiType);
					type2.Trim();

					// stripping out comments because the prev. line can contain comment after ';'
					// e.g. while finding "b", the comment will be removed:
					// int a; // comment
					// int b;
					// 					cs.Reset();
					// 					WTString strippedType;
					// 					for (int i=0; i<type.GetLength(); i++) {
					// 						TCHAR c = type[i];
					// 						if (cs.IsCode(c))
					// 							strippedType += c;
					// 					}
					// 					strippedType.Trim();

					// new buffer
					copied = type2 + " " + copied + ";" + ed->GetLineBreakString();
				}

				// clipboardless paste
				ed->SetPos((uint)ed->GetBufIndex(
				    (long)ed->LinePos(insertLn.first + (multiType != -1 ? 1 : 0 - copyPositionOffset))));
				if (oneLineSelection)
					copied.Trim();
				if (insertLn.second)
				{
					if (Psettings->mEncFieldMoveVariable == tagSettings::cvv_private)
						copied = "private:" + lineBreak + copied;
					else
						copied = "protected:" + lineBreak + copied;
				}
				int copiedLen = copied.GetLength();
				if (oneLineSelection && copiedLen && (copied[copiedLen - 1] != '\n' && copied[copiedLen - 1] != '\r'))
					copied += lineBreak;

				ed->InsertW(copied.Wide(), false, vsfAutoFormat, false);
				// BOOL res = gAutotextMgr->InsertAsTemplate(ed, copied, FALSE);
			}
		}
	}
	if (fileType == CS)
	{ // step 3 in C#
		if (hidden.first == false && hidden.second != -1)
		{ // it is false when we have the keyword "public" for the member variable (field)
			if (Psettings->mEncFieldMoveVariable != tagSettings::cvv_no)
			{
				EdCntPtr ed(g_currentEdCnt);
				ed->SetSel(hidden.second, hidden.second + 6);
				WTString copied;
				switch (Psettings->mEncFieldMoveVariable)
				{
				case tagSettings::cvv_private:
					copied = "private";
					break;
				case tagSettings::cvv_protected:
					copied = "protected";
					break;
				case tagSettings::cvv_internal:
					copied = "internal";
					break;
				}

				ed->InsertW(copied.Wide(), false, vsfAutoFormat, false);
			}
		}
	}

#endif
	return TRUE;
}

#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
WTString EncapsulateField::ConstructPropertySnippetCppB_Head(sSetterGetter& setterGetter, const WTString& symName,
                                                             SymbolVisibility symVisibility, bool& outIsBodyCreated)
{
	SymbolVisibility propertyVisibilty = symVisibility;
	outIsBodyCreated = false;

	// create proper template for property according to selected options - property line part (head)
	WTString propertySnippet;

	if (Psettings->mEncFieldMovePropertyCppB != tagSettings::cpv_no)
	{
		// determining property visibility
		switch (Psettings->mEncFieldMovePropertyCppB)
		{
		case tagSettings::cpv_published:
			propertyVisibilty = vPublished;
			break;
		case tagSettings::cpv_public:
			propertyVisibilty = vPublic;
			break;
		case tagSettings::cpv_private:
			propertyVisibilty = vPrivate;
			break;
		case tagSettings::cpv_protected:
			propertyVisibilty = vProtected;
			break;
		default:
			break;
		}

		if (propertyVisibilty != symVisibility)
		{
			// if desired visibility is not the current one, append desired one
			propertySnippet.append(::GetVisibilityString(propertyVisibilty, gTypingDevLang));
			propertySnippet.append(":\n");
		}
	}

	propertySnippet.append("__property $SymbolType$ ");
	propertySnippet.append(symName.c_str());
	propertySnippet.append(" = { ");
	if (!setterGetter.GetterName.IsEmpty())
	{
		propertySnippet.append("read = ");
		propertySnippet.append(setterGetter.GetterName.c_str());
	}

	if (!setterGetter.SetterName.IsEmpty())
	{
		if (!setterGetter.GetterName.IsEmpty())
			propertySnippet.append(", ");
		propertySnippet.append("write = ");
		propertySnippet.append(setterGetter.SetterName.c_str());
	}

	propertySnippet.append(" };");

	SymbolVisibility accessorVisibilty = propertyVisibilty;

	if (Psettings->mEncFieldMoveVariable != tagSettings::cvv_no)
	{
		// determine successor visibility
		switch (Psettings->mEncFieldMoveVariable)
		{
		case tagSettings::cvv_private:
			accessorVisibilty = vPrivate;
			break;
		case tagSettings::cvv_protected:
			accessorVisibilty = vProtected;
			break;
		default:
			break;
		}
	}

	if (accessorVisibilty == propertyVisibilty)
	{
		// case when accessors are in the same scope as the property; then just put accessors after the property line
		WTString bodySnippet = ConstructPropertySnippetCppB_Body(setterGetter, symName);
		propertySnippet.append("\n\n");
		propertySnippet.append(bodySnippet.c_str());
		outIsBodyCreated = true;
	}

	if (propertyVisibilty != symVisibility)
	{
		// restoring original visibility if needed
		propertySnippet.append("\n\n");
		propertySnippet.append(::GetVisibilityString(symVisibility, gTypingDevLang));
		propertySnippet.append(":");
	}

	return propertySnippet;
}

WTString EncapsulateField::ConstructPropertySnippetCppB_Body(sSetterGetter& setterGetter, const WTString& symName)
{
	// create proper template for property according to selected options - accessors part (body)
	WTString propertySnippet;

	propertySnippet.append("$SymbolType$ F");
	propertySnippet.append(symName.c_str());
	propertySnippet.append(";");

	if (!Psettings->mEnableEncFieldReadFieldCppB && !setterGetter.GetterName.IsEmpty())
	{
		propertySnippet.append("\n\n");

		if (Psettings->mEnableEncFieldReadVirtualCppB)
			propertySnippet.append("virtual ");

		propertySnippet.append("$SymbolType$ ");
		propertySnippet.append(setterGetter.GetterName.c_str());
		propertySnippet.append("()\n");

		propertySnippet.append("{\n");
		propertySnippet.append("return F");
		propertySnippet.append(symName.c_str());
		propertySnippet.append(";\n");
		propertySnippet.append("}");
	}

	if (!Psettings->mEnableEncFieldWriteFieldCppB && !setterGetter.SetterName.IsEmpty())
	{
		propertySnippet.append("\n\n");

		if (Psettings->mEnableEncFieldWriteVirtualCppB)
			propertySnippet.append("virtual ");

		propertySnippet.append("void ");
		propertySnippet.append(setterGetter.SetterName.c_str());
		propertySnippet.append("($SymbolType$ val)\n");

		propertySnippet.append("{\n");
		propertySnippet.append("F");
		propertySnippet.append(symName.c_str());
		propertySnippet.append(" = val;\n");
		propertySnippet.append("}");
	}

	return propertySnippet;
}

WTString EncapsulateField::ExtractLogicalVariableNameCppB(const WTString& originalVariableName)
{
	WTString retVal;

	if (originalVariableName.length() <
	    2) // if we don't have at least two letter in name then no point to extract since that is actually the name
	{
		retVal = originalVariableName;
	}
	else
	{
		WTString firtsLetter = originalVariableName.Left(1);
		if (firtsLetter == "F")
		{
			// check if second letter is upper case
			WTString secondLetter = originalVariableName.Mid(1, 1);
			if (StrIsUpperCase(secondLetter))
				retVal = originalVariableName.Mid(1);
			else
				retVal = originalVariableName;
		}
		else if (firtsLetter == "m")
		{
			// check if second letter is upper case or underscore
			WTString secondLetter = originalVariableName.Mid(1, 1);
			if (secondLetter == "_" && originalVariableName.length() > 2) // case m_*
			{
				retVal = originalVariableName.Mid(2);
			}
			else if (StrIsUpperCase(secondLetter))
			{
				retVal = originalVariableName.Mid(1);
			}
			else
			{
				retVal = originalVariableName;
			}
		}
		else if (firtsLetter == "_")
		{
			retVal = originalVariableName.Mid(1);
		}
		else
		{
			retVal = originalVariableName;
		}
	}

	// make uppercased first letter
	WTString t = retVal.Left(1);
	t.MakeUpper();
	retVal.SetAt(0, t[0]);

	return retVal;
}
#endif

BOOL EncapsulateField::DoModal(DTypePtr sym, sSetterGetter& setterGetter, bool hidden)
{
	EncapsulateFieldDlg dlg;
	if (!dlg.Init(sym, setterGetter, hidden))
		return FALSE;

	dlg.DoModal();
	setterGetter.SetterName = dlg.GetNewSetterName();
	setterGetter.GetterName = dlg.GetNewGetterName();

	return dlg.IsStarted(); // dlg.DoModal() always returns IDCANCEL (2)
}

// C++
EncapsulateField::sSetterGetter EncapsulateField::GetSetterGetter_C(const WTString& rawSnippet, DType* sym)
{
	const char generatedPropName[] = "$GeneratedPropertyName$";
	const char setterWord[] = "void";
	sSetterGetter res;

	// iterate through the rawSnippet and find two $SymbolName$ outside the {}s.
	int brackets = 0;
	int symCounter = 0;
	CommentSkipper cs(Src);
	bool isSetter = false;
	// setterFound is used for improved heuristic as part of [case: 95632].
	// Normally, we know which is the setter by finding "void" return type.
	// The improved version works even when user modified the snippet in a way that the setter have a different return
	// type. In this case, the second method will be treated as the setter. Apart from naming, we now also remove part
	// of the snippet (due to case 95632) if only the getter or only the setter is specified so it is getting
	// increasingly important to know which is which.
	bool setterFound = false;
	for (int i = 0; i < rawSnippet.GetLength(); i++)
	{
		TCHAR c = rawSnippet[i];
		if (!cs.IsCode(c))
			continue;
		if (c == '{')
			brackets++;
		if (c == '}')
			brackets--;
		if (c == '\r' || c == '\n')
		{
			isSetter = false;
			continue;
		}
		if (brackets <= 0)
		{
			int j;
			for (j = 0; setterWord[j] != 0; j++)
			{
				if (i + j >= rawSnippet.GetLength() || rawSnippet[i + j] != setterWord[j])
					goto check_propName;
			}
			if (i + j < rawSnippet.GetLength() && IsWSorContinuation(rawSnippet[i + j]))
			{
				isSetter = true;
				setterFound = true;
			}

		check_propName:
			j = 0;
			if (i + j >= rawSnippet.GetLength() ||
			    rawSnippet[i + j] != generatedPropName[j]) // comparing the first character, which is a '$' sign
				goto next_i;
			j = 1;
			if (i + j >= rawSnippet.GetLength() ||
			    toupper(rawSnippet[i + j]) !=
			        generatedPropName[j]) // the 'G' of $GeneratedPropertyName$ can be both in upper- and lowercase
				goto next_i;
			for (j = 2; generatedPropName[j] != 0; j++)
			{
				if (i + j >= rawSnippet.GetLength() || rawSnippet[i + j] != generatedPropName[j])
					goto next_i;
			}
		}
		else
			goto next_i;

		// we localize the boundaries of $SymbolName$'s whitespaceless surrounding
		{
			int surrBeg = i;
			for (int surrBegIt = i - 1; surrBegIt >= 0; surrBegIt--)
			{
				TCHAR ch = rawSnippet[surrBegIt];
				if (!ISCSYM(ch))
					break;

				surrBeg = surrBegIt;
			}

			int surrEnd = i + 1;
			{
				bool reservedWordEnded = false; // we accept '$' as part of the end of $GeneratedPropertyName$
				for (int surrEndIt = i + 1; surrEndIt < rawSnippet.GetLength(); surrEndIt++)
				{
					TCHAR ch = rawSnippet[surrEndIt];
					if (!ISCSYM(ch) && reservedWordEnded && ch != '$')
						break;
					if (ch == '$')
						reservedWordEnded = true;

					surrEnd = surrEndIt + 1;
				}
			}

			if (symCounter == 2)
			{
				return sSetterGetter(); // if there is more than two $GeneratedPropertyName$, return empty strings - we
				                        // cannot update references
			}
			else if (isSetter || (symCounter == 1 && !setterFound))
			{
				for (int copyIt = surrBeg; copyIt < surrEnd; copyIt++)
					res.SetterName += rawSnippet[copyIt];
				res.SetterFrom = surrBeg;
				res.SetterTo = surrEnd;
			}
			else
			{
				for (int copyIt = surrBeg; copyIt < surrEnd; copyIt++)
					res.GetterName += rawSnippet[copyIt];
				res.GetterFrom = surrBeg;
				res.GetterTo = surrEnd;
			}

			i = surrEnd - 1; // let's continue from the next, skipping everything further in our symbol string
			symCounter++;
		}
	next_i:;
	}

	if (symCounter == 1)
		return sSetterGetter();

	// replace $GeneratedPropertyName$ with the actual symbol name in the name string
	WTString symName = sym->Sym();
	GetGeneratedPropName(symName);
	if (symName == sym->Sym() && res.SetterName == res.GetterName)
	{
		// in order to prevent immediate error msg on dlg creation, make
		// Getter and Setter not use the sym name by default
		res.SetterName.ReplaceAll(generatedPropName, "Set" + symName);
		res.GetterName.ReplaceAll(generatedPropName, "Get" + symName);
	}
	else
	{
		res.SetterName.ReplaceAll(generatedPropName, symName);
		res.GetterName.ReplaceAll(generatedPropName, symName);
	}

	WTString lowerSymName;
	if (!(symName[0] & 0x80))
	{
		lowerSymName = symName.Left(1);
		lowerSymName.MakeLower();
		lowerSymName += symName.Mid(1);
	}
	else
		lowerSymName = symName;

	if (lowerSymName == symName)
	{
		// in order to prevent immediate error msg on dlg creation, make
		// Getter and Setter not use the sym name by default
		res.SetterName.ReplaceAll("$generatedPropertyName$", "Set" + lowerSymName);
		res.GetterName.ReplaceAll("$generatedPropertyName$", "Get" + lowerSymName);
	}
	else
	{
		res.SetterName.ReplaceAll("$generatedPropertyName$", lowerSymName);
		res.GetterName.ReplaceAll("$generatedPropertyName$", lowerSymName);
	}

	return res;
}

// C#
EncapsulateField::sSetterGetter EncapsulateField::GetProperty_CS(const WTString& rawSnippet, DType* sym)
{
	const char generatedPropName[] = "$GeneratedPropertyName$";
	sSetterGetter res;

	// iterate through the rawSnippet and find two $SymbolName$ outside the {}s.
	int symCounter = 0;
	CommentSkipper cs(CS);
	for (int i = 0; i < rawSnippet.GetLength(); i++)
	{
		TCHAR c = rawSnippet[i];
		if (!cs.IsCode(c))
			continue;

		int j = 0;
		if (i + j >= rawSnippet.GetLength() ||
		    rawSnippet[i + j] != generatedPropName[j]) // comparing the first character, which is a '$' sign
			goto next_i;
		j = 1;
		if (i + j >= rawSnippet.GetLength() ||
		    toupper(rawSnippet[i + j]) !=
		        generatedPropName[j]) // the 'G' of $GeneratedPropertyName$ can be both in upper- and lowercase
			goto next_i;
		for (j = 2; generatedPropName[j] != 0; j++)
		{
			if (i + j >= rawSnippet.GetLength() || rawSnippet[i + j] != generatedPropName[j])
				goto next_i;
		}

		{
			// we localize the boundaries of $SymbolName$'s whitespaceless surrounding
			int surrBeg = i;
			for (int surrBegIt = i - 1; surrBegIt >= 0; surrBegIt--)
			{
				TCHAR ch = rawSnippet[surrBegIt];
				if (!ISCSYM(ch))
					break;

				surrBeg = surrBegIt;
			}

			int surrEnd = i + 1;
			bool reservedWordEnded = false; // we accept '$' as part of the end of $GeneratedPropertyName$
			for (int surrEndIt = i + 1; surrEndIt < rawSnippet.GetLength(); surrEndIt++)
			{
				TCHAR ch = rawSnippet[surrEndIt];
				if (!ISCSYM(ch) && reservedWordEnded && ch != '$')
					break;
				if (ch == '$')
					reservedWordEnded = true;

				surrEnd = surrEndIt + 1;
			}

			if (symCounter == 1)
			{
				return sSetterGetter(); // if there is more than two $GeneratedPropertyName$, return empty strings - we
				                        // cannot update references
			}
			else
			{
				for (int copyIt = surrBeg; copyIt < surrEnd; copyIt++)
					res.GetterName += rawSnippet[copyIt];
				res.GetterFrom = surrBeg;
				res.GetterTo = surrEnd;
				break; // in C#, there is only one $GeneratedPropertyName$, so we exit the loop
			}
		}

	next_i:;
	}

	// replace $GeneratedPropertyName$ with the actual symbol name in the name string
	WTString symName = sym->Sym();
	GetGeneratedPropName(symName);
	res.GetterName.ReplaceAll(generatedPropName, symName);
	WTString lowerSymName;
	if (symName[0] & 0x80)
		lowerSymName = symName;
	else
	{
		lowerSymName = symName.Left(1);
		lowerSymName.MakeLower();
		lowerSymName += symName.Mid(1);
	}
	res.GetterName.ReplaceAll("$generatedPropertyName$", lowerSymName);

	return res;
}

EncapsulateField::sSetterGetter EncapsulateField::GetSetterGetter(const WTString& rawSnippet, DType* sym)
{
	EdCntPtr ed(g_currentEdCnt);
	if (IsCFile(ed->m_ftype))
		return GetSetterGetter_C(rawSnippet, sym);
	else
		return GetProperty_CS(rawSnippet,
		                      sym); // only one accessor in C# which we put in the Getter field of sSetterGetter
}

// overwrite setter and getter names. e.g. Set$GenertatedPropertyName$ becomes SetApple with m_Apple if user does not
// change it in the references list dialog (EncapsulateFieldDlg)
void EncapsulateField::ChangeRawSnippet(const sSetterGetter& sg, DType* sym, int fileType)
{
	// replace the lower one first so the position of the upper one will not change
	if (sg.GetterFrom > sg.SetterFrom)
	{
		_ASSERTE(sg.GetterFrom || sg.GetterTo);
		RawSnippet.ReplaceAt(sg.GetterFrom, sg.GetterTo - sg.GetterFrom, sg.GetterName.c_str());
		if (sg.SetterFrom || sg.SetterTo)
			RawSnippet.ReplaceAt(sg.SetterFrom, sg.SetterTo - sg.SetterFrom, sg.SetterName.c_str());
	}
	else
	{
		if (sg.SetterFrom || sg.SetterTo)
			RawSnippet.ReplaceAt(sg.SetterFrom, sg.SetterTo - sg.SetterFrom, sg.SetterName.c_str());
		_ASSERTE(sg.GetterFrom || sg.GetterTo);
		RawSnippet.ReplaceAt(sg.GetterFrom, sg.GetterTo - sg.GetterFrom, sg.GetterName.c_str());
	}

	// delete the getter or the setter if needed
	int endOfFirst = 0;
	if (IsCFile(fileType) && (sg.GetterName.IsEmpty() || sg.SetterName.IsEmpty()))
	{
		// find the boundaries of the first method in the snippet
		CommentSkipper cs(Src);
		int curlyCounter = 0;
		for (int i = 0; i < RawSnippet.GetLength(); i++)
		{
			TCHAR c = RawSnippet[i];
			if (cs.IsCode(c))
			{
				if (c == '{')
					curlyCounter++;
				if (c == '}')
				{
					curlyCounter--;
					if (curlyCounter == 0)
					{
						endOfFirst = i + 1;
						while ((RawSnippet[endOfFirst] == '\r' || RawSnippet[endOfFirst] == '\n') &&
						       endOfFirst + 1 < RawSnippet.GetLength())
							endOfFirst++;
						break;
					}
				}
			}
		}

		if (endOfFirst)
		{
			if (sg.GetterName.IsEmpty()) // deleting the getter from the snippet
			{
				if (sg.GetterFrom < sg.SetterFrom)
					RawSnippet = RawSnippet.Mid(endOfFirst);
				else
					RawSnippet = RawSnippet.Left(endOfFirst);
			}
			if (sg.SetterName.IsEmpty()) // deleting the setter from the snippet
			{
				if (sg.GetterFrom < sg.SetterFrom)
					RawSnippet = RawSnippet.Left(endOfFirst);
				else
					RawSnippet = RawSnippet.Mid(endOfFirst);
			}
		}
	}
}

#ifdef _DEBUG
void DumpMarkers(LineMarkers::Node& n, int depth)
{
	FileLineMarker& mkr = n.Contents();
	WTString txt;
	txt.WTFormat("%u\t%lu\t%lu\t%lu\t%lu\t%.8lx\t%.8lx\t%.8lx\t%s\n", depth, mkr.mStartLine, mkr.mStartCp, mkr.mEndLine,
	             mkr.mEndCp, mkr.mType, mkr.mAttrs, mkr.mDisplayFlag, WTString(mkr.mText).c_str());
	OutputDebugStringW(txt.Wide());

	for (size_t i = 0; i < n.GetChildCount(); ++i)
		DumpMarkers(n.GetChild(i), depth + 1);
}
#endif

std::pair<bool, long> EncapsulateField::IsHiddenSection(DType* sym)
{
	EdCntPtr ed(g_currentEdCnt);

	WTString fileText(ed->GetBuf(TRUE));
	MultiParsePtr mp = ed->GetParseDb();
	LineMarkers markers; // outline data
	::GetFileOutline(fileText, markers, mp);
#if 0
	::DumpMarkers(markers.Root(), 0);
#endif
	WTString symScope = sym->SymScope();
	int ln = GetSymbolLineByFindingItsNodeInOutline(symScope, markers.Root()); // sym->Line();
	FindLnToInsert find((ULONG)ln);
	return find.IsHiddenSection(markers.Root(), sym->Sym());
}

LineMarkers::Node* EncapsulateField::FindLnToInsert::GetVisibilityNode(CStringW labelWithColon)
{
	if (DeepestClass == nullptr)
		return nullptr;

	for (size_t j = 0; j < DeepestClass->GetChildCount(); j++)
	{
		LineMarkers::Node& ch = DeepestClass->GetChild(j);
		FileLineMarker& marker = *ch;
		if (marker.mText == labelWithColon)
			return &ch;
	}

	return nullptr;
}

// Line to insert getters and setters
// return pair 1/2: the line number to insert. < 0 means no insert.
// return pair 2/2: do we want to create a public section?
std::pair<int, bool> EncapsulateField::FindLnToInsert::GetLnToInsert(LineMarkers::Node& node)
{
	for (size_t idx = 0; idx < node.GetChildCount(); ++idx)
	{
		LineMarkers::Node& ch = node.GetChild(idx);
		FileLineMarker& marker = *ch;
		if (Ln >= (uint)marker.mStartLine && Ln <= uint(marker.mEndLine - 1))
		{
			if (marker.mType == VAR)
			{
				// are we in a public visibility part of class/struct?
				if (Label == "public:" || (SymbolType == eSymbolType::STRUCT && Label == ""))
					return std::pair<int, bool>(-1, false); // already in public section - do it in the old way

				// find public node
				LineMarkers::Node* publicNode = GetVisibilityNode("public:");
				if (publicNode)
				{
					FileLineMarker& marker2 = **publicNode;
					return std::pair<int, bool>((int)marker2.mEndLine, false);
				}

				// create public: section at end of class/struct
				size_t childCount = 0;
				if (DeepestClass)
					childCount = DeepestClass->GetChildCount();
				if (childCount > 0)
				{
					int endOfClassPos = (int)DeepestClass->GetChild(childCount - 1).Contents().mEndLine;
					return std::pair<int, bool>(endOfClassPos, true);
				}
				else
				{
					return std::pair<int, bool>(-1, false);
				}
			}
			if (marker.mType == CLASS || marker.mType == STRUCT)
			{
				Label = "";
				SymbolType = marker.mType == CLASS ? eSymbolType::CLASS : eSymbolType::STRUCT;
				DeepestClass = &ch;
				std::pair<int, bool> res = GetLnToInsert(ch);
				if (res.first != -2)
					return res;
			}
			else
			{
				if (marker.mText == L"public:" || marker.mText == L"private:" || marker.mText == L"protected:" ||
				    marker.mText == L"__published:")
					Label = marker.mText;
				std::pair<int, bool> res = GetLnToInsert(ch);
				if (res.first != -2)
					return res;
			}
		}
	}

	return std::pair<int, bool>(-2, false);
}

// Line to move variable
// return pair 1/2: the line number to move. < 0 means don't move the variable.
// return pair 2/2: do we want to create private or protected section?
std::pair<int, bool> EncapsulateField::FindLnToInsert::GetLnToMove(LineMarkers::Node& node, WTString symName,
                                                                   int& selFrom, int& selTo, bool& oneLineSelection,
                                                                   int& multiType)
{
	for (size_t idx = 0; idx < node.GetChildCount(); ++idx)
	{
		LineMarkers::Node& ch = node.GetChild(idx);
		FileLineMarker& marker = *ch;

		if ((Ln >= marker.mStartLine && Ln <= marker.mEndLine - 1) ||
		    (Ln == marker.mStartLine && marker.mStartLine == marker.mEndLine))
		{
			if (marker.mType == VAR)
			{
				CStringW nextWord;
				int charPointer = 0;
				CStringW text = marker.mText;
				bool commaList = text.Find(L',') != -1;
				while (GetNextWord(nextWord, charPointer, text))
				{
					if (nextWord == symName.Wide())
					{
						// are we in a private/protected visibility part of class/struct?
						if (Label == "protected:" || Label == "private:" ||
						    (SymbolType == eSymbolType::CLASS && Label == ""))
							return std::pair<int, bool>(
							    -1,
							    false); // already in hidden section - no need to move. normally, this should not happen
							            // because GetLnToMove() is not called when the variable is in a hidden section.

						// find hidden node
						bool noLineDelete = false;
						if (marker.mStartLine == marker.mEndLine)
						{
							noLineDelete = true;
							oneLineSelection = true;
						}
						else
						{
							if (idx > 0)
							{
								LineMarkers::Node& ch_prev = node.GetChild(idx - 1);
								FileLineMarker& marker_prev = *ch_prev;
								oneLineSelection = marker_prev.mStartLine == marker.mStartLine;
								noLineDelete = oneLineSelection;
							}
						}
						if (Psettings->mEncFieldMoveVariable == tagSettings::cvv_private)
						{
							LineMarkers::Node* privateNode = GetVisibilityNode("private:");
							if (privateNode)
							{
								FileLineMarker& marker2 = **privateNode;
								selFrom = (int)marker.mStartCp;
								selTo = (int)marker.mEndCp;
								if (commaList)
								{
									multiType = selFrom;
									NarrowScope(text, nextWord, selFrom, selTo);
								}
								return std::pair<int, bool>(int(noLineDelete ? marker2.mEndLine : marker2.mEndLine - 1),
								                            false);
							}
						}
						if (Psettings->mEncFieldMoveVariable == tagSettings::cvv_protected)
						{
							LineMarkers::Node* protectedNode = GetVisibilityNode("protected:");
							if (protectedNode)
							{
								selFrom = (int)marker.mStartCp;
								selTo = (int)marker.mEndCp;
								if (commaList)
								{
									multiType = selFrom;
									NarrowScope(text, nextWord, selFrom, selTo);
								}
								FileLineMarker& marker2 = **protectedNode;
								return std::pair<int, bool>(int(noLineDelete ? marker2.mEndLine : marker2.mEndLine - 1),
								                            false);
							}
						}

						// create default hidden section at end of class/struct (private or protected)
						size_t childCount = 0;
						if (DeepestClass)
							childCount = DeepestClass->GetChildCount();
						if (childCount > 0)
						{
							int endOfClassPos = (int)DeepestClass->GetChild(childCount - 1).Contents().mEndLine -
							                    (noLineDelete ? 0 : 1);
							selFrom = (int)marker.mStartCp;
							selTo = (int)marker.mEndCp;
							if (commaList)
							{
								multiType = selFrom;
								NarrowScope(text, nextWord, selFrom, selTo);
							}
							return std::pair<int, bool>(endOfClassPos, true);
						}
						else
						{
							return std::pair<int, bool>(-1, false); // should not happen, abort mission
						}
					}
				}
				continue;
			}
			if (marker.mType == CLASS || marker.mType == STRUCT)
			{
				Label = "";
				SymbolType = marker.mType == CLASS ? eSymbolType::CLASS : eSymbolType::STRUCT;
				DeepestClass = &ch;
				std::pair<int, bool> res = GetLnToMove(ch, symName, selFrom, selTo, oneLineSelection, multiType);
				if (res.first != -2)
					return res;
			}
			else
			{
				if (marker.mType == RESWORD)
				{
					CStringW text = marker.mText;
					if (text == L"public:" || text == L"private:" || text == L"protected:" || text == L"__published:")
						Label = text;
				}
				std::pair<int, bool> res = GetLnToMove(ch, symName, selFrom, selTo, oneLineSelection, multiType);
				if (res.first != -2)
					return res;
			}
		}
	}

	return std::pair<int, bool>(-2, false);
}

std::pair<bool, long> EncapsulateField::FindLnToInsert::IsHiddenSection(LineMarkers::Node& node, WTString symName)
{
	for (size_t idx = 0; idx < node.GetChildCount(); ++idx)
	{
		LineMarkers::Node& ch = node.GetChild(idx);
		FileLineMarker& marker = *ch;

		if ((Ln >= (uint)marker.mStartLine && Ln <= uint(marker.mEndLine - 1)) ||
		    (Ln == (uint)marker.mStartLine && marker.mStartLine == marker.mEndLine))
		{
			if (marker.mType == VAR)
			{
				CStringW nextWord;
				int charPointer = 0;
				CStringW text = marker.mText;
				//				bool commaList = text.Find(L',') != -1;
				while (GetNextWord(nextWord, charPointer, text))
				{
					if (nextWord == symName.Wide())
					{
						// are we in a private/protected visibility part of class/struct?
						EdCntPtr ed(g_currentEdCnt);
						if (ed->m_ftype == CS)
						{
							WTString buf = ed->GetBuf();
							CommentSkipper cs(ed->m_ftype);
							for (size_t i = (size_t)marker.mEndCp + 6; i >= (size_t)marker.mStartCp; i--)
							{
								if (cs.IsCodeBackward(buf, (int)i))
								{
									if (buf[i - 6] == 'p' && buf[i - 5] == 'u' && buf[i - 4] == 'b' &&
									    buf[i - 3] == 'l' && buf[i - 2] == 'i' && buf[i - 1] == 'c' && !ISCSYM(buf[i]))
									{
										if (i == 0 || !ISCSYM(buf[i - 7]))
											return std::pair<bool, long>(false, long(i - 6));
									}
								}
							}
							return std::pair<bool, long>(true, 0);
						}
						else
						{
							if (Label == "protected:" || Label == "private:" ||
							    (SymbolType == eSymbolType::CLASS && Label == ""))
								return std::pair<bool, long>(true, 0); // already in hidden section
							return std::pair<bool, long>(false, -1);
						}
					}
				}
				continue;
			}
			if (marker.mType == CLASS || marker.mType == STRUCT)
			{
				Label = "";
				SymbolType = marker.mType == CLASS ? eSymbolType::CLASS : eSymbolType::STRUCT;
				DeepestClass = &ch;
				std::pair<bool, long> res = IsHiddenSection(ch, symName);
				if (res.second >= 0)
					return res;
			}
			else
			{
				if (marker.mType == RESWORD)
				{
					CStringW text = marker.mText;
					if (text == L"public:" || text == L"private:" || text == L"protected:" || text == L"__published:")
						Label = text;
				}
				std::pair<bool, long> res = IsHiddenSection(ch, symName);
				if (res.second >= 0)
					return res;
			}
		}
	}

	return std::pair<bool, long>(false, -1);
}

#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
std::pair<int, bool> EncapsulateField::FindLnToInsert::GetLnToMoveCppB(LineMarkers::Node& node, WTString symName,
                                                                       int& selFrom, int& selTo)
{
	for (size_t idx = 0; idx < node.GetChildCount(); ++idx)
	{
		LineMarkers::Node& ch = node.GetChild(idx);
		FileLineMarker& marker = *ch;

		if ((Ln >= (uint)marker.mStartLine && Ln <= uint(marker.mEndLine - 1)) ||
		    (Ln == (uint)marker.mStartLine && marker.mStartLine == marker.mEndLine))
		{
			if (marker.mType == VAR)
			{
				CStringW nextWord;
				int charPointer = 0;
				CStringW text = marker.mText;
				bool commaList = text.Find(L',') != -1;
				while (GetNextWord(nextWord, charPointer, text))
				{
					if (nextWord == symName.Wide())
					{
						if (Psettings->mEncFieldMoveVariable == tagSettings::cvv_private)
						{
							LineMarkers::Node* privateNode = GetVisibilityNode("private:");
							if (privateNode)
							{
								FileLineMarker& marker2 = **privateNode;
								selFrom = (int)marker.mStartCp;
								selTo = (int)marker.mEndCp;
								if (commaList)
									NarrowScope(text, nextWord, selFrom, selTo);

								return std::pair<int, bool>((int)marker2.mEndLine, false);
							}
						}
						if (Psettings->mEncFieldMoveVariable == tagSettings::cvv_protected)
						{
							LineMarkers::Node* protectedNode = GetVisibilityNode("protected:");
							if (protectedNode)
							{
								FileLineMarker& marker2 = **protectedNode;
								selFrom = (int)marker.mStartCp;
								selTo = (int)marker.mEndCp;
								if (commaList)
									NarrowScope(text, nextWord, selFrom, selTo);

								return std::pair<int, bool>((int)marker2.mEndLine, false);
							}
						}

						// create default hidden section at end of class/struct (private or protected)
						size_t childCount = 0;
						if (DeepestClass)
							childCount = DeepestClass->GetChildCount();

						if (childCount > 0)
						{
							int endOfClassPos = (int)DeepestClass->GetChild(childCount - 1).Contents().mEndLine;
							selFrom = (int)marker.mStartCp;
							selTo = (int)marker.mEndCp;
							if (commaList)
								NarrowScope(text, nextWord, selFrom, selTo);

							return std::pair<int, bool>(endOfClassPos, true);
						}
						else
						{
							return std::pair<int, bool>(-1, false); // should not happen, abort mission
						}
					}
				}
				continue;
			}
			if (marker.mType == CLASS || marker.mType == STRUCT)
			{
				Label = "";
				SymbolType = marker.mType == CLASS ? eSymbolType::CLASS : eSymbolType::STRUCT;
				DeepestClass = &ch;
				std::pair<int, bool> res = GetLnToMoveCppB(ch, symName, selFrom, selTo);
				if (res.first != -2)
					return res;
			}
			else
			{
				if (marker.mType == RESWORD)
				{
					CStringW text = marker.mText;
					if (text == L"public:" || text == L"private:" || text == L"protected:" || text == L"__published:")
						Label = text;
				}

				std::pair<int, bool> res = GetLnToMoveCppB(ch, symName, selFrom, selTo);
				if (res.first != -2)
					return res;
			}
		}
	}

	return std::pair<int, bool>(-2, false);
}
#endif

#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
EncapsulateFieldDlg::EncapsulateFieldDlg()
    : UpdateReferencesDlg("EncapsulateFieldDlg", IDD_ENCAPSULATEFIELD_CPPB, NULL,
                          Psettings->mIncludeProjectNodeInRenameResults, true)
#else
EncapsulateFieldDlg::EncapsulateFieldDlg()
    : UpdateReferencesDlg("EncapsulateFieldDlg", IDD_ENCAPSULATEFIELD, NULL,
                          Psettings->mIncludeProjectNodeInRenameResults, true)
#endif
{
	mColourize = true;
}

EncapsulateFieldDlg::~EncapsulateFieldDlg()
{
}

BOOL EncapsulateFieldDlg::Init(DTypePtr sym, EncapsulateField::sSetterGetter setterGetter, bool hidden)
{
	if (!sym)
		return FALSE;

	mSym = sym.get();

	const CStringW declFilePath = FileFromDef(mSym);
	mDeclFileType = GetFileType(declFilePath);
	FindSymDef_MLC decl(mDeclFileType);

	decl->FindSymbolInFile(declFilePath, mSym, FALSE);
	decl->mShortLineText.ReplaceAll("{...}", "");
	mTemplateStr = decl->GetTemplateStr();
	if (decl->mShortLineText.IsEmpty())
		return FALSE;

	// mEditTxt = mOrigSig = decl->mShortLineText;
	EdCntPtr ed(g_currentEdCnt);
	if (!ed)
		return rrError;

	if (IsCFile(ed->m_ftype))
	{
		mEditTxt = setterGetter.GetterName;
		mEditTxt2 = setterGetter.SetterName;
	}
	else
	{
		mEditTxt = setterGetter.GetterName;
	}

	m_symScope = sym->SymScope();

	mHidden = hidden;

#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
	mSymbolName = EncapsulateField::ExtractLogicalVariableNameCppB(mSym->Sym());
	mOriginalGetterName = setterGetter.GetterName;
	mOriginalSetterName = setterGetter.SetterName;
#endif

	return TRUE;
}

UpdateReferencesDlg::UpdateResult EncapsulateFieldDlg::UpdateReference(int refIdx, FreezeDisplay& _f)
{
	FindReference* curRef = mRefs->GetReference((size_t)refIdx);

	if (curRef->mData && curRef->mData->IsMethod())
		return rrSuccess; // nothing to do here

	if (!mRefs->GotoReference(refIdx))
		return rrError;

	EdCntPtr ed(g_currentEdCnt);
	if (!ed)
		return rrError;

	const WTString sel = ed->GetSelString();
	if (sel != mRefs->GetFindSym())
		return rrError;

	_f.ReadOnlyCheck();

	if (curRef->type == FREF_Unknown || curRef->type == FREF_Comment || curRef->type == FREF_IncludeDirective ||
	    curRef->type == FREF_Definition)
		return rrNoChange;

	if (curRef->lineNo == (ULONG)mSym->Line()) // it shouldn't happen sometimes VA thinks a definition is a reference
	                                           // (see VAAutoTest:EncField0013)
		return rrNoChange;

#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
	if (Psettings->mEncFieldTypeCppB == tagSettings::ceft_property)
	{
		// in case of CppB property, just change field with the property name
		CStringW propertyName = mSymbolName.Wide();
		if (!ed->ReplaceSelW(propertyName, noFormat))
			return rrError;
		if (gShellAttr->IsMsdev())
			ed->GetBuf(TRUE);
		ed->OnModified(TRUE);
		return rrSuccess;
	}
#endif

	if (ed->m_ftype == CS)
	{
		CStringW get = mNewGetterName.Wide();
		if (!ed->ReplaceSelW(get, noFormat))
			return rrError;
		if (gShellAttr->IsMsdev())
			ed->GetBuf(TRUE);
		ed->OnModified(TRUE);
		return rrSuccess;
	}

	// detect type
	enum class eRefType
	{
		REFERENCE,             // var
		ASSIGNMENT,            // var =
		INCREMENT_BEFORE,      // ++var
		DECREMENT_BEFORE,      // --var
		INCREMENT_AFTER,       // var++
		DECREMENT_AFTER,       // var--
		INCREASE_BY,           // var +=
		DECREASE_BY,           // var -=
		MULTIPLE_BY,           // var *=
		DIVIDE_BY,             // var /=
		AND_ASSIGNMENT,        // var &=
		OR_ASSIGNMENT,         // var |=
		XOR_ASSIGNMENT,        // var ^=
		LEFTSHIFT_ASSIGNMENT,  // var <<=
		RIGHTSHIFT_ASSIGNMENT, // var >>=
		MODULO_ASSIGNMENT,     // var %=
		SKIP,                  // var(, var{
	};

	// detecting ASSIGNMENT
	WTString buf = ed->GetBuf(TRUE);
	int symPos = (int)curRef->GetEdBufCharOffset(ed, sel, buf);
	eRefType refType = eRefType::REFERENCE;
	CommentSkipper cs(ed->m_ftype);
	int assPos = -1;
	for (int i = symPos + curRef->mData->Sym().GetLength(); i < buf.GetLength(); i++)
	{
		TCHAR c = buf[i];
		if (cs.IsCode(c) && cs.GetState() != CommentSkipper::COMMENT_MAY_START)
		{
			if (c == '=' && i + 1 < buf.GetLength() && buf[i + 1] != '=')
			{
				refType = eRefType::ASSIGNMENT;
				assPos = i;
				break;
			}
			if (!IsWSorContinuation(c))
				break;
		}
	}

	// detecting INCREMENT_BEFORE, DECREMENT_BEFORE
	cs.Reset();
	for (int i = symPos - 1; i >= 0; i--)
	{
		TCHAR c = buf[i];
		if (cs.IsCodeBackward(buf, i) && cs.GetState() != CommentSkipper::COMMENT_MAY_START)
		{
			if (c == '+' && i - 1 >= 0 && buf[i - 1] == '+')
			{
				refType = eRefType::INCREMENT_BEFORE;
				assPos = i - 1;
				break;
			}
			if (c == '-' && i - 1 >= 0 && buf[i - 1] == '-')
			{
				refType = eRefType::DECREMENT_BEFORE;
				assPos = i - 1;
				break;
			}
			if (c == '+' || c == '-' || c == ';' || c == '{' || c == '}' || c == '*' || c == '>' || c == '<' ||
			    c == '=' || c == '!' || c == '&' || c == '^' || c == '%' || c == '~' || c == '|' || c == '&' ||
			    c == '(' || c == ')')
				break;
		}
	}

	// detecting INCREMENT_AFTER, DECREMENT_AFTER
	cs.Reset();
	int symNameLen = curRef->mData->Sym().GetLength();
	for (int i = symPos + symNameLen; i < buf.GetLength(); i++)
	{
		TCHAR c = buf[i];
		if (cs.IsCode(c) && cs.GetState() != CommentSkipper::COMMENT_MAY_START)
		{
			if (c == '+' && i + 1 < buf.GetLength() && buf[i + 1] == '+')
			{
				refType = eRefType::INCREMENT_AFTER;
				assPos = i;
				break;
			}
			if (c == '-' && i + 1 < buf.GetLength() && buf[i + 1] == '-')
			{
				refType = eRefType::DECREMENT_AFTER;
				assPos = i;
				break;
			}
			if (!IsWSorContinuation(c))
				break;
		}
	}

	// detecting INCREASE_BY, DECREASE_BY, MULTIPLE_BY, DIVIDE_BY, AND_ASSIGNMENT, OR_ASSIGNMENT, XOR_ASSIGNMENT,
	// LEFTSHIFT_ASSIGNMENT, RIGHTSHIFT_ASSIGNMENT, MODULO_ASSIGNMENT
	cs.Reset();
	for (int i = symPos + symNameLen; i < buf.GetLength() - 1; i++)
	{
		TCHAR c = buf[i];
		if (cs.IsCode(c) && (c != '/' || buf[i + 1] != '*'))
		{
			if (c == '+' && i + 1 < buf.GetLength() && buf[i + 1] == '=')
			{
				refType = eRefType::INCREASE_BY;
				assPos = i;
				break;
			}
			if (c == '-' && i + 1 < buf.GetLength() && buf[i + 1] == '=')
			{
				refType = eRefType::DECREASE_BY;
				assPos = i;
				break;
			}
			if (c == '*' && i + 1 < buf.GetLength() && buf[i + 1] == '=')
			{
				refType = eRefType::MULTIPLE_BY;
				assPos = i;
				break;
			}
			if (c == '/' && i + 1 < buf.GetLength() && buf[i + 1] == '=')
			{
				refType = eRefType::DIVIDE_BY;
				assPos = i;
				break;
			}
			if (c == '&' && i + 1 < buf.GetLength() && buf[i + 1] == '=')
			{
				refType = eRefType::AND_ASSIGNMENT;
				assPos = i;
				break;
			}
			if (c == '|' && i + 1 < buf.GetLength() && buf[i + 1] == '=')
			{
				refType = eRefType::OR_ASSIGNMENT;
				assPos = i;
				break;
			}
			if (c == '^' && i + 1 < buf.GetLength() && buf[i + 1] == '=')
			{
				refType = eRefType::XOR_ASSIGNMENT;
				assPos = i;
				break;
			}
			if (c == '<' && i + 2 < buf.GetLength() && buf[i + 1] == '<' && buf[i + 2] == '=')
			{
				refType = eRefType::LEFTSHIFT_ASSIGNMENT;
				assPos = i;
				break;
			}
			if (c == '>' && i + 2 < buf.GetLength() && buf[i + 1] == '>' && buf[i + 2] == '=')
			{
				refType = eRefType::RIGHTSHIFT_ASSIGNMENT;
				assPos = i;
				break;
			}
			if (c == '%' && i + 1 < buf.GetLength() && buf[i + 1] == '=')
			{
				refType = eRefType::MODULO_ASSIGNMENT;
				assPos = i;
				break;
			}
			if (!IsWSorContinuation(c))
				break;
		}
	}

	// detecting SKIP
	cs.Reset();
	for (int i = symPos + symNameLen; i < buf.GetLength() - 1; i++)
	{
		TCHAR c = buf[i];
		if (cs.IsCode(c) && (c != '/' || buf[i + 1] != '*'))
		{
			if (c == '(' || c == '{')
			{
				refType = eRefType::SKIP;
				assPos = i;
				break;
			}
			if (!IsWSorContinuation(c))
				break;
		}
	}

	WTString complexObj;
	int lastNonWhiteSpacePos = ChainExtendBackward(buf, symPos, ed->m_ftype);
	complexObj = buf.Mid(lastNonWhiteSpacePos, symPos - lastNonWhiteSpacePos);
	CStringW getterFuncCall =
	    mNewGetterName.IsEmpty() ? curRef->mData->Sym().Wide() : mNewGetterName.Wide() + CStringW("() ");

	// Replacing
	switch (refType)
	{
	case eRefType::ASSIGNMENT: {
		CommentSkipper cs2(ed->m_ftype);
		int parens = 0;
		for (int i = assPos + 1; i < buf.GetLength(); i++)
		{
			TCHAR c = buf[i];
			if (cs2.IsCode(c))
			{
				if (c == '(')
				{
					parens++;
					continue;
				}
				if (c == ')')
				{
					parens--;
					continue;
				}
				if (parens <= 0 && (c == ',' || c == ';'))
				{
					ed->SetPos((uint)i);
					if (!mNewSetterName.IsEmpty())
					{
						if (!ed->ReplaceSelW(L")", noFormat))
							return rrError;
					}
					CStringW set = mNewSetterName.IsEmpty() ? curRef->mData->Sym().Wide() + CStringW(" = ")
					                                        : mNewSetterName.Wide() + CStringW("(");
					if (!ReplaceAroundEqualitySign(buf, assPos + 1, symPos, set))
						return rrError;
					return rrSuccess;
				}
			}
		}
		return rrError;
	}
	case eRefType::REFERENCE: {
		if (mNewGetterName.IsEmpty())
			return rrSuccess;
		CStringW get = mNewGetterName.Wide() + CStringW("()");
		if (!ed->ReplaceSelW(get, noFormat))
			return rrError;
		if (gShellAttr->IsMsdev())
			ed->GetBuf(TRUE);
		ed->OnModified(TRUE);
		return rrSuccess;
	}
	case eRefType::SKIP:
		return rrNoChange;
	case eRefType::INCREMENT_BEFORE:
	case eRefType::DECREMENT_BEFORE: {
		ed->SetPos(uint(assPos + 2));
		if (!mNewSetterName.IsEmpty())
		{
			if (!ed->ReplaceSelW(")", noFormat))
				return rrError;
		}

		CStringW incdec;
		if (refType == eRefType::INCREMENT_BEFORE)
			incdec = L"+";
		else
			incdec = L"-";

		CStringW setterFuncCall = mNewSetterName.IsEmpty() ? curRef->mData->Sym().Wide() + CStringW(" = ")
		                                                   : mNewSetterName.Wide() + CStringW("(");
		CStringW endFuncCall = mNewSetterName.IsEmpty() ? L" 1" : L" 1)";
		CStringW set = complexObj.Wide() + setterFuncCall + complexObj.Wide() + getterFuncCall + incdec + endFuncCall;

		// replace code
		int plus = mNewSetterName.IsEmpty() ? 0 : 1;
		ed->SetSel((long)assPos, (long)symPos + symNameLen + plus);
		if (!ed->ReplaceSelW(set, noFormat))
			return rrError;
		return rrSuccess;
	}
	case eRefType::INCREMENT_AFTER:
	case eRefType::DECREMENT_AFTER: {
		ed->SetPos(uint(assPos + 2));
		if (!mNewSetterName.IsEmpty())
		{
			if (!ed->ReplaceSelW(")", noFormat))
				return rrError;
		}

		CStringW incdec;
		if (refType == eRefType::INCREMENT_AFTER)
			incdec = "+";
		else
			incdec = "-";

		CStringW setterFuncCall = mNewSetterName.IsEmpty() ? curRef->mData->Sym().Wide() + CStringW(" = ")
		                                                   : mNewSetterName.Wide() + CStringW("(");
		CStringW set = setterFuncCall + complexObj.Wide() + getterFuncCall + incdec + CStringW(" 1");

		// replace code
		ed->SetSel((long)symPos, (long)assPos + 2);
		if (!ed->ReplaceSelW(set, noFormat))
			return rrError;
		return rrSuccess;
	}
	case eRefType::INCREASE_BY:
	case eRefType::DECREASE_BY:
	case eRefType::MULTIPLE_BY:
	case eRefType::DIVIDE_BY:
	case eRefType::AND_ASSIGNMENT:
	case eRefType::OR_ASSIGNMENT:
	case eRefType::XOR_ASSIGNMENT:
	case eRefType::LEFTSHIFT_ASSIGNMENT:
	case eRefType::RIGHTSHIFT_ASSIGNMENT:
	case eRefType::MODULO_ASSIGNMENT: {
		CommentSkipper cs2(ed->m_ftype);
		int parens = 0;
		for (int i = assPos + 2; i < buf.GetLength(); i++)
		{
			TCHAR c = buf[i];
			if (cs2.IsCode(c))
			{
				if (c == '(')
				{
					parens++;
					continue;
				}
				if (c == ')')
				{
					parens--;
					continue;
				}
				if (parens <= 0 && (c == ',' || c == ';'))
				{
					ed->SetPos((uint)i);
					if (!mNewSetterName.IsEmpty())
					{
						if (!ed->ReplaceSelW(")", noFormat))
							return rrError;
					}

					CStringW incdec;
					switch (refType)
					{
					case eRefType::INCREASE_BY:
						incdec = "+";
						break;
					case eRefType::DECREASE_BY:
						incdec = "-";
						break;
					case eRefType::MULTIPLE_BY:
						incdec = "*";
						break;
					case eRefType::DIVIDE_BY:
						incdec = "/";
						break;
					case eRefType::AND_ASSIGNMENT:
						incdec = "&";
						break;
					case eRefType::OR_ASSIGNMENT:
						incdec = "|";
						break;
					case eRefType::XOR_ASSIGNMENT:
						incdec = "^";
						break;
					case eRefType::LEFTSHIFT_ASSIGNMENT:
						incdec = "<<";
						break;
					case eRefType::RIGHTSHIFT_ASSIGNMENT:
						incdec = ">>";
						break;
					case eRefType::MODULO_ASSIGNMENT:
						incdec = "%";
						break;
					default:
						break;
					}

					CStringW setterFuncCall = mNewSetterName.IsEmpty() ? curRef->mData->Sym().Wide() + CStringW(" = ")
					                                                   : mNewSetterName.Wide() + CStringW("(");
					CStringW set = setterFuncCall + complexObj.Wide() + getterFuncCall + incdec + CStringW(" ");
					int more =
					    refType == eRefType::LEFTSHIFT_ASSIGNMENT || refType == eRefType::RIGHTSHIFT_ASSIGNMENT ? 3 : 2;
					if (!ReplaceAroundEqualitySign(buf, assPos + more, symPos, set))
						return rrError;
					return rrSuccess;
				}
			}
		}
		return rrError;
	}
	}

	return rrNoChange;
}

#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
void EncapsulateFieldDlg::UpdateStatus(BOOL done, int fileCount)
{
	EdCntPtr ed(g_currentEdCnt);
	if (!ed)
		return;

	WTString msg;
	if (!done)
	{
		msg.WTFormat("Searching for references in %s to %s that may need to be updated...",
		             mRefs->GetScopeOfSearchStr().c_str(), mRefs->GetFindSym().c_str());
	}
	else if (mFindRefsThread && mFindRefsThread->IsStopped() && mRefs->Count())
	{
		msg.WTFormat("Search canceled before completion.  U&pdate references to %s at your own risk.",
		             mRefs->GetFindSym().c_str());
	}
	else if (mRefs->Count())
	{
		if (IsCFile(ed->m_ftype))
		{
			if (Psettings->mEncFieldTypeCppB == tagSettings::ceft_property)
				msg = cStatusMsgRead;
			else
				msg = cStatusMsg;
		}
		else
			msg = csStatusMsg;
	}

	if (msg.GetLength())
	{
		if (msg == cStatusMsg || msg == cStatusMsgRead)
			((CStatic*)GetDlgItem(IDC_EDIT2_LABEL))->ShowWindow(SW_SHOW);
		else
			((CStatic*)GetDlgItem(IDC_EDIT2_LABEL))->ShowWindow(SW_HIDE);

		::SetWindowTextW(GetDlgItem(IDC_STATUS)->GetSafeHwnd(), msg.Wide());
	}

	mStatusText = msg;
}
#else
void EncapsulateFieldDlg::UpdateStatus(BOOL done, int fileCount)
{
	EdCntPtr ed(g_currentEdCnt);
	if (!ed)
		return;

	WTString msg;
	if (!done)
	{
		msg.WTFormat("Searching for references in %s to %s that may need to be updated...",
		             mRefs->GetScopeOfSearchStr().c_str(), mRefs->GetFindSym().c_str());
	}
	else if (mFindRefsThread && mFindRefsThread->IsStopped() && mRefs->Count())
	{
		msg.WTFormat("Search canceled before completion.  U&pdate references to %s at your own risk.",
		             mRefs->GetFindSym().c_str());
	}
	else if (mRefs->Count())
	{
		if (IsCFile(ed->m_ftype))
			msg = cStatusMsg;
		else
			msg = csStatusMsg;
	}

	if (msg.GetLength())
	{
		if (msg == cStatusMsg)
			((CStatic*)GetDlgItem(IDC_EDIT2_LABEL))->ShowWindow(SW_SHOW);
		else
			((CStatic*)GetDlgItem(IDC_EDIT2_LABEL))->ShowWindow(SW_HIDE);

		::SetWindowTextW(GetDlgItem(IDC_STATUS)->GetSafeHwnd(), msg.Wide());
	}

	mStatusText = msg;
}
#endif

BOOL EncapsulateFieldDlg::OnInitDialog()
{
	__super::OnInitDialog();

	EdCntPtr ed(g_currentEdCnt);

	if (GetDlgItem(IDC_EDIT_SECOND))
	{
		// [case: 9194] do not use DDX_Control due to ColourizeControl.
		// Subclass with colourizer before SHAutoComplete (CtrlBackspaceEdit).
		mEdit_subclassed2.SubclassWindow(GetDlgItem(IDC_EDIT_SECOND)->m_hWnd);
		mEdit_subclassed2.SetText(mEditTxt2.Wide());
	}
	mEdit2 = &mEdit_subclassed2;

	if (CVS2010Colours::IsExtendedThemeActive())
	{
		Theme.AddThemedSubclasserForDlgItem<CThemedCheckBox>(IDC_CHECK_ENC_FIELD_PUBLIC_ACCESSORS, this);
		// Theme.AddThemedSubclasserForDlgItem<CThemedStatic>(IDC_STATIC_ENC_FIELD, this);
		Theme.AddDlgItemForDefaultTheming(IDC_STATIC_ENC_FIELD);
		Theme.AddThemedSubclasserForDlgItem<CThemedRadioButton>(IDC_RADIO_ENC_FIELD_PRIVATE, this);
		Theme.AddThemedSubclasserForDlgItem<CThemedRadioButton>(IDC_RADIO_ENC_FIELD_PROTECTED, this);
		Theme.AddThemedSubclasserForDlgItem<CThemedRadioButton>(IDC_RADIO_ENC_FIELD_NONE, this);
		Theme.AddThemedSubclasserForDlgItem<CThemedRadioButton>(IDC_RADIO_ENC_FIELD_INTERNAL, this);
		Theme.AddDlgItemForDefaultTheming(IDC_EDIT2_LABEL);
#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
		Theme.AddDlgItemForDefaultTheming(IDC_STATIC_ENCTYPE_CPPB);
		Theme.AddThemedSubclasserForDlgItem<CThemedRadioButton>(IDC_RADIO_ENC_FIELD_PROPERTY_CPPB, this);
		Theme.AddThemedSubclasserForDlgItem<CThemedRadioButton>(IDC_RADIO_ENC_FIELD_GETSET_CPPB, this);
		Theme.AddThemedSubclasserForDlgItem<CThemedCheckBox>(IDC_CHK_READ_FIELD_CPPB, this);
		Theme.AddThemedSubclasserForDlgItem<CThemedCheckBox>(IDC_CHK_WRITE_FIELD_CPPB, this);
		Theme.AddThemedSubclasserForDlgItem<CThemedCheckBox>(IDC_CHK_READ_VIRTUAL_CPPB, this);
		Theme.AddThemedSubclasserForDlgItem<CThemedCheckBox>(IDC_CHK_WRITE_VIRTUAL_CPPB, this);
		Theme.AddDlgItemForDefaultTheming(IDC_STATIC_ENC_PROP_CPPB);
		Theme.AddThemedSubclasserForDlgItem<CThemedRadioButton>(IDC_RADIO_ENC_PROP_PUBLISHED_CPPB, this);
		Theme.AddThemedSubclasserForDlgItem<CThemedRadioButton>(IDC_RADIO_ENC_PROP_PUBLIC_CPPB, this);
		Theme.AddThemedSubclasserForDlgItem<CThemedRadioButton>(IDC_RADIO_ENC_PROP_PRIVATE_CPPB, this);
		Theme.AddThemedSubclasserForDlgItem<CThemedRadioButton>(IDC_RADIO_ENC_PROP_PROTECTED_CPPB, this);
		Theme.AddThemedSubclasserForDlgItem<CThemedRadioButton>(IDC_RADIO_ENC_PROP_NONE_CPPB, this);
#endif
		ThemeUtils::ApplyThemeInWindows(TRUE, m_hWnd);
	}

	AddSzControl(IDC_EDIT2_LABEL, mdRelative, mdNone);
#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
	AddSzControl(IDC_CHK_WRITE_FIELD_CPPB, mdRelative, mdNone);
	AddSzControl(IDC_CHK_WRITE_VIRTUAL_CPPB, mdRelative, mdNone);
#endif

	((CButton*)GetDlgItem(IDC_CHECK_ENC_FIELD_PUBLIC_ACCESSORS))->SetCheck(Psettings->mEncFieldPublicAccessors);

	if (IsCFile(ed->m_ftype) && !mHidden && Psettings->mEncFieldMoveVariable == tagSettings::cvv_internal)
		Psettings->mEncFieldMoveVariable = tagSettings::cvv_private; // reset the setting from internal in C++
	switch (Psettings->mEncFieldMoveVariable)
	{
	case tagSettings::cvv_private:
		((CButton*)GetDlgItem(IDC_RADIO_ENC_FIELD_PRIVATE))->SetCheck(true);
		break;
	case tagSettings::cvv_protected:
		((CButton*)GetDlgItem(IDC_RADIO_ENC_FIELD_PROTECTED))->SetCheck(true);
		break;
	case tagSettings::cvv_no:
		((CButton*)GetDlgItem(IDC_RADIO_ENC_FIELD_NONE))->SetCheck(true);
		break;
	case tagSettings::cvv_internal:
		((CButton*)GetDlgItem(IDC_RADIO_ENC_FIELD_INTERNAL))->SetCheck(true);
		break;
	default:
		Psettings->mEncFieldMoveVariable = 0;
	}

#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
	WTString getterCaption = cStatusMsgRead;
	WTString setterCaption = cStatusMsgWrite;

	switch (Psettings->mEncFieldTypeCppB)
	{
	case tagSettings::ceft_property:
		((CButton*)GetDlgItem(IDC_RADIO_ENC_FIELD_PROPERTY_CPPB))->SetCheck(true);
		((CButton*)GetDlgItem(IDC_STATIC_ENC_FIELD))->SetWindowText("Accessors visibility");
		((CButton*)GetDlgItem(IDC_CHK_READ_VIRTUAL_CPPB))
		    ->ShowWindow(Psettings->mEnableEncFieldReadFieldCppB ? SW_HIDE : SW_SHOW);
		((CButton*)GetDlgItem(IDC_CHK_WRITE_VIRTUAL_CPPB))
		    ->ShowWindow(Psettings->mEnableEncFieldWriteFieldCppB ? SW_HIDE : SW_SHOW);
		break;
	case tagSettings::ceft_geter_setter:
		getterCaption = cStatusMsg;
		setterCaption = cStatusMsgSetter;
		((CButton*)GetDlgItem(IDC_RADIO_ENC_FIELD_GETSET_CPPB))->SetCheck(true);
		((CButton*)GetDlgItem(IDC_STATIC_ENC_PROP_CPPB))->ShowWindow(SW_HIDE);
		((CButton*)GetDlgItem(IDC_RADIO_ENC_PROP_PUBLISHED_CPPB))->ShowWindow(SW_HIDE);
		((CButton*)GetDlgItem(IDC_RADIO_ENC_PROP_PUBLIC_CPPB))->ShowWindow(SW_HIDE);
		((CButton*)GetDlgItem(IDC_RADIO_ENC_PROP_PRIVATE_CPPB))->ShowWindow(SW_HIDE);
		((CButton*)GetDlgItem(IDC_RADIO_ENC_PROP_PROTECTED_CPPB))->ShowWindow(SW_HIDE);
		((CButton*)GetDlgItem(IDC_RADIO_ENC_PROP_NONE_CPPB))->ShowWindow(SW_HIDE);
		((CButton*)GetDlgItem(IDC_CHK_READ_FIELD_CPPB))->ShowWindow(SW_HIDE);
		((CButton*)GetDlgItem(IDC_CHK_READ_VIRTUAL_CPPB))->ShowWindow(SW_HIDE);
		((CButton*)GetDlgItem(IDC_CHK_WRITE_FIELD_CPPB))->ShowWindow(SW_HIDE);
		((CButton*)GetDlgItem(IDC_CHK_WRITE_VIRTUAL_CPPB))->ShowWindow(SW_HIDE);
		break;
	default:
		Psettings->mEncFieldTypeCppB = tagSettings::ceft_property;
	}

	((CButton*)GetDlgItem(IDC_CHK_READ_FIELD_CPPB))->SetCheck(Psettings->mEnableEncFieldReadFieldCppB);
	((CButton*)GetDlgItem(IDC_CHK_READ_VIRTUAL_CPPB))->SetCheck(Psettings->mEnableEncFieldReadVirtualCppB);
	((CButton*)GetDlgItem(IDC_CHK_WRITE_FIELD_CPPB))->SetCheck(Psettings->mEnableEncFieldWriteFieldCppB);
	((CButton*)GetDlgItem(IDC_CHK_WRITE_VIRTUAL_CPPB))->SetCheck(Psettings->mEnableEncFieldWriteVirtualCppB);

	switch (Psettings->mEncFieldMovePropertyCppB)
	{
	case tagSettings::cpv_published:
		((CButton*)GetDlgItem(IDC_RADIO_ENC_PROP_PUBLISHED_CPPB))->SetCheck(true);
		break;
	case tagSettings::cpv_public:
		((CButton*)GetDlgItem(IDC_RADIO_ENC_PROP_PUBLIC_CPPB))->SetCheck(true);
		break;
	case tagSettings::cpv_private:
		((CButton*)GetDlgItem(IDC_RADIO_ENC_PROP_PRIVATE_CPPB))->SetCheck(true);
		break;
	case tagSettings::cpv_protected:
		((CButton*)GetDlgItem(IDC_RADIO_ENC_PROP_PROTECTED_CPPB))->SetCheck(true);
		break;
	case tagSettings::cpv_no:
		((CButton*)GetDlgItem(IDC_RADIO_ENC_PROP_NONE_CPPB))->SetCheck(true);
		break;
	default:
		Psettings->mEncFieldMovePropertyCppB = tagSettings::cpv_published;
	}

	SetGetterGUIState();
	SetSetterGUIState();

	SetGetterSetterCaptionCppB(getterCaption, setterCaption);
	SetGetterSetterTextCppB();
#endif

	if (!IsCFile(ed->m_ftype))
	{
		// C#
		if (mHidden)
		{
			((CStatic*)GetDlgItem(IDC_STATIC_ENC_FIELD))->ShowWindow(SW_HIDE);
			((CButton*)GetDlgItem(IDC_RADIO_ENC_FIELD_PRIVATE))->ShowWindow(SW_HIDE);
			((CButton*)GetDlgItem(IDC_RADIO_ENC_FIELD_PRIVATE))->SetWindowText("");
			((CButton*)GetDlgItem(IDC_RADIO_ENC_FIELD_PROTECTED))->ShowWindow(SW_HIDE);
			((CButton*)GetDlgItem(IDC_RADIO_ENC_FIELD_PROTECTED))->SetWindowText("");
			((CButton*)GetDlgItem(IDC_RADIO_ENC_FIELD_NONE))->ShowWindow(SW_HIDE);
			((CButton*)GetDlgItem(IDC_RADIO_ENC_FIELD_NONE))->SetWindowText("");
			((CButton*)GetDlgItem(IDC_RADIO_ENC_FIELD_INTERNAL))->ShowWindow(SW_HIDE);
			((CButton*)GetDlgItem(IDC_RADIO_ENC_FIELD_INTERNAL))->SetWindowText("");
		}

		ResizeEdit1();
		((CButton*)GetDlgItem(IDC_EDIT_SECOND))->ShowWindow(SW_HIDE);

		((CButton*)GetDlgItem(IDC_CHECK_ENC_FIELD_PUBLIC_ACCESSORS))->ShowWindow(SW_HIDE);
		((CButton*)GetDlgItem(IDC_CHECK_ENC_FIELD_PUBLIC_ACCESSORS))->SetWindowText("");
	}
	else
	{
		// C++
#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
		if (mHidden && Psettings->mEncFieldTypeCppB == tagSettings::ceft_geter_setter)
		{
			((CStatic*)GetDlgItem(IDC_STATIC_ENC_FIELD))->ShowWindow(SW_HIDE);
			((CButton*)GetDlgItem(IDC_RADIO_ENC_FIELD_PRIVATE))->ShowWindow(SW_HIDE);
			((CButton*)GetDlgItem(IDC_RADIO_ENC_FIELD_PROTECTED))->ShowWindow(SW_HIDE);
			((CButton*)GetDlgItem(IDC_RADIO_ENC_FIELD_NONE))->ShowWindow(SW_HIDE);
		}
		else
		{
			((CButton*)GetDlgItem(IDC_CHECK_ENC_FIELD_PUBLIC_ACCESSORS))->ShowWindow(SW_HIDE);

			if (Psettings->mEncFieldTypeCppB == tagSettings::ceft_geter_setter &&
			    (mNewGetterName.IsEmpty() || mNewSetterName.IsEmpty()))
			{
				// not hidden but is getter/setter and one of them is empty then hide option buttons (old functionality)
				((CStatic*)GetDlgItem(IDC_STATIC_ENC_FIELD))->ShowWindow(SW_HIDE);
				((CButton*)GetDlgItem(IDC_RADIO_ENC_FIELD_PRIVATE))->ShowWindow(SW_HIDE);
				((CButton*)GetDlgItem(IDC_RADIO_ENC_FIELD_PROTECTED))->ShowWindow(SW_HIDE);
				((CButton*)GetDlgItem(IDC_RADIO_ENC_FIELD_NONE))->ShowWindow(SW_HIDE);
			}
		}
#else
		if (mHidden)
		{
			((CStatic*)GetDlgItem(IDC_STATIC_ENC_FIELD))->ShowWindow(SW_HIDE);
			((CButton*)GetDlgItem(IDC_STATIC_ENC_FIELD))->SetWindowText("");
			((CButton*)GetDlgItem(IDC_RADIO_ENC_FIELD_PRIVATE))->ShowWindow(SW_HIDE);
			((CButton*)GetDlgItem(IDC_RADIO_ENC_FIELD_PRIVATE))->SetWindowText("");
			((CButton*)GetDlgItem(IDC_RADIO_ENC_FIELD_PROTECTED))->ShowWindow(SW_HIDE);
			((CButton*)GetDlgItem(IDC_RADIO_ENC_FIELD_PROTECTED))->SetWindowText("");
			((CButton*)GetDlgItem(IDC_RADIO_ENC_FIELD_NONE))->ShowWindow(SW_HIDE);
			((CButton*)GetDlgItem(IDC_RADIO_ENC_FIELD_NONE))->SetWindowText("");
		}
		else
		{
			((CButton*)GetDlgItem(IDC_CHECK_ENC_FIELD_PUBLIC_ACCESSORS))->ShowWindow(SW_HIDE);
			((CButton*)GetDlgItem(IDC_CHECK_ENC_FIELD_PUBLIC_ACCESSORS))->SetWindowText("");
		}
#endif
		((CButton*)GetDlgItem(IDC_RADIO_ENC_FIELD_INTERNAL))->ShowWindow(SW_HIDE);
		((CButton*)GetDlgItem(IDC_RADIO_ENC_FIELD_INTERNAL))->SetWindowText("");
	}

	// 	AddSzControl(IDC_WARNING, mdResize, mdRepos);
	//
	// 	mEdit->SetSel(0, 0);
	//
	// 	if (gTestLogger)
	// 	{
	// 		gTestLogger->LogStr(WTString("Encapsulate update refs dlg"));
	// 		gTestLogger->LogStr(WTString("\t" + mEditTxt));
	// 	}

	// 	if (mDisableEdit)
	// 	{
	// 		GetDlgItem(IDC_EDIT1)->EnableWindow(FALSE);
	// 		m_tree.SetFocus();
	// 	}

#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
	if (Psettings->mEncFieldTypeCppB == tagSettings::ceft_property)
	{
		// initial set focus on first available element
		if (!Psettings->mEnableEncFieldReadFieldCppB)
		{
			mEdit_subclassed.SetFocus();
			int len = mEdit_subclassed.GetWindowTextLength();
			mEdit_subclassed.SetSel(0, len);
		}
		else if (!Psettings->mEnableEncFieldWriteFieldCppB)
		{
			mEdit_subclassed2.SetFocus();
			int len = mEdit_subclassed2.GetWindowTextLength();
			mEdit_subclassed2.SetSel(0, len);
		}
		else
		{
			((CButton*)GetDlgItem(IDC_CHK_READ_FIELD_CPPB))->SetFocus();
		}
	}
	else
	{
		// it is getter/setter, set focus on getter
		mEdit_subclassed.SetFocus();
		int len = mEdit_subclassed.GetWindowTextLength();
		mEdit_subclassed.SetSel(0, len);
	}
#endif

	SetHelpTopic("dlgEncapsulateField");

	return FALSE;
}

void EncapsulateFieldDlg::ResizeEdit1()
{
	if (mEdit_subclassed.m_hWnd == nullptr || mEdit_subclassed2.m_hWnd == nullptr)
		return;

	RECT rect1;
	mEdit_subclassed.GetWindowRect(&rect1);
	ScreenToClient(&rect1);

	RECT rect2;
	mEdit_subclassed2.GetWindowRect(&rect2);
	ScreenToClient(&rect2);

	RECT rect3 = rect1;
	rect3.right = rect2.right;

	mEdit_subclassed.MoveWindow(&rect3);
}

BOOL EncapsulateFieldDlg::OnUpdateStart()
{
	mStated = true;

	return TRUE;
}

BOOL EncapsulateFieldDlg::ValidateInput()
{
	EdCntPtr ed(g_currentEdCnt);
	if (!ed)
		return rrError;

	if (IsCFile(ed->m_ftype))
	{
		mNewGetterName = mEditTxt;
		mNewGetterName.Trim();
		mNewSetterName = mEditTxt2;
		mNewSetterName.Trim();

		if (mNewGetterName.IsEmpty() && mNewSetterName.IsEmpty())
		{
			SetErrorStatus("Both getter and setter names cannot be empty");
			return FALSE;
		}

#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
		if (mSym->Sym() == mNewGetterName && !Psettings->mEnableEncFieldReadFieldCppB)
		{
			SetErrorStatus("The getter name cannot be the same as the variable name");
			return FALSE;
		}
		if (mSym->Sym() == mNewSetterName && !Psettings->mEnableEncFieldWriteFieldCppB)
		{
			SetErrorStatus("The setter name cannot be the same as the variable name");
			return FALSE;
		}
#else
		if (mSym->Sym() == mNewGetterName)
		{
			SetErrorStatus("The getter name cannot be the same as the variable name");
			return FALSE;
		}
		if (mSym->Sym() == mNewSetterName)
		{
			SetErrorStatus("The setter name cannot be the same as the variable name");
			return FALSE;
		}
#endif

		if (!mNewGetterName.IsEmpty() && !IsValidSymbol(mNewGetterName))
		{
			SetErrorStatus("Invalid character in getter name");
			return FALSE;
		}
		if (!mNewSetterName.IsEmpty() && !IsValidSymbol(mNewSetterName))
		{
			SetErrorStatus("Invalid character in setter name");
			return FALSE;
		}
	}
	else
	{
		mNewGetterName = mEditTxt;
		if (mNewGetterName.IsEmpty())
		{
			SetErrorStatus("Empty property name");
			return FALSE;
		}
		if (!IsValidSymbol(mNewGetterName))
		{
			SetErrorStatus("Invalid character in property name");
			return FALSE;
		}
		if (mSym->Sym() == mNewGetterName)
		{
			SetErrorStatus("Property name cannot be the same as the variable name");
			return FALSE;
		}
	}

	// default message
	WTString msg;
	if (IsCFile(ed->m_ftype))
	{
#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
		if (mStatusText != cStatusMsg && mStatusText != cStatusMsgRead)
		{
			if (Psettings->mEncFieldTypeCppB == tagSettings::ceft_property)
				mStatusText = cStatusMsgRead;
			else
				mStatusText = cStatusMsg;

			((CStatic*)GetDlgItem(IDC_EDIT2_LABEL))->ShowWindow(SW_SHOW);
			::SetWindowTextW(GetDlgItem(IDC_STATUS)->GetSafeHwnd(), mStatusText.Wide());
		}
#else
		if (mStatusText != cStatusMsg)
		{
			mStatusText = cStatusMsg;
			((CStatic*)GetDlgItem(IDC_EDIT2_LABEL))->ShowWindow(SW_SHOW);
			::SetWindowTextW(GetDlgItem(IDC_STATUS)->GetSafeHwnd(), mStatusText.Wide());
		}
#endif
	}
	else
	{
		if (mStatusText != csStatusMsg)
		{
			mStatusText = csStatusMsg;
			((CStatic*)GetDlgItem(IDC_EDIT2_LABEL))->ShowWindow(SW_HIDE);
			::SetWindowTextW(GetDlgItem(IDC_STATUS)->GetSafeHwnd(), mStatusText.Wide());
		}
	}

#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
	if (IsCFile(ed->m_ftype) && !mHidden && Psettings->mEncFieldTypeCppB == tagSettings::ceft_geter_setter)
#else
	if (IsCFile(ed->m_ftype) && !mHidden)
#endif
	{
		if (mNewGetterName.IsEmpty() || mNewSetterName.IsEmpty()) // hide
		{
			((CStatic*)GetDlgItem(IDC_STATIC_ENC_FIELD))->ShowWindow(SW_HIDE);
			((CButton*)GetDlgItem(IDC_RADIO_ENC_FIELD_PRIVATE))->ShowWindow(SW_HIDE);
			((CButton*)GetDlgItem(IDC_RADIO_ENC_FIELD_PROTECTED))->ShowWindow(SW_HIDE);
			((CButton*)GetDlgItem(IDC_RADIO_ENC_FIELD_NONE))->ShowWindow(SW_HIDE);
		}
		else // show
		{
			((CStatic*)GetDlgItem(IDC_STATIC_ENC_FIELD))->ShowWindow(SW_SHOW);
			((CButton*)GetDlgItem(IDC_RADIO_ENC_FIELD_PRIVATE))->ShowWindow(SW_SHOW);
			((CButton*)GetDlgItem(IDC_RADIO_ENC_FIELD_PROTECTED))->ShowWindow(SW_SHOW);
			((CButton*)GetDlgItem(IDC_RADIO_ENC_FIELD_NONE))->ShowWindow(SW_SHOW);

			((CButton*)GetDlgItem(IDC_CHECK_ENC_FIELD_PUBLIC_ACCESSORS))->ShowWindow(SW_HIDE);
		}
	}

	return TRUE;
}

BOOL EncapsulateFieldDlg::ReplaceAroundEqualitySign(const WTString& buf, int assPos, int symPos,
                                                    const CStringW& replaceWith) const
{
	int endPos = assPos;

	// find last non-whitespace after "=" / "+=" / "-=" to select
	for (int i = assPos; i < buf.GetLength(); i++)
	{
		TCHAR c = buf[i];
		if (!IsWSorContinuation(c))
		{
			endPos = i;
			break;
		}
	}

	// replace code
	EdCntPtr ed(g_currentEdCnt);
	if (!ed)
		return FALSE;

	ed->SetSel((long)symPos, (long)endPos);
	return ed->ReplaceSelW(replaceWith, noFormat);
}

bool EncapsulateFieldDlg::IsValidSymbol(const WTString& symbolName)
{
	if (symbolName.IsEmpty())
		return false;

	for (int i = 0; i < symbolName.GetLength(); i++)
		if (!ISCSYM(symbolName[i]))
			return false;

	return symbolName[0] <= '0' || symbolName[0] >= '9';
}

void EncapsulateFieldDlg::SetErrorStatus(LPCSTR msg)
{
	// 	if (mFindRefsThread && mFindRefsThread->IsRunning())
	// 		return;

	const WTString txt(msg);
	if (txt == mStatusText)
		return;

	mStatusText = txt;
	((CStatic*)GetDlgItem(IDC_EDIT2_LABEL))->ShowWindow(SW_HIDE);
	::SetWindowTextW(GetDlgItem(IDC_STATUS)->GetSafeHwnd(), txt.Wide());
}

void EncapsulateFieldDlg::OnDestroy()
{
	if (mEdit_subclassed2.m_hWnd)
		mEdit_subclassed2.UnsubclassWindow();

	mEdit2 = nullptr;

	__super::OnDestroy();
}

void EncapsulateFieldDlg::OnSize(UINT nType, int cx, int cy)
{
	__super::OnSize(nType, cx, cy);

	EdCntPtr ed(g_currentEdCnt);
	if (ed && !IsCFile(ed->m_ftype))
		ResizeEdit1();
}

void EncapsulateFieldDlg::OnChangeEdit()
{
	// get the latest text even when thread running
	if (mEdit_subclassed2.GetSafeHwnd())
	{
		CStringW txt;
		mEdit_subclassed2.GetText(txt);
		mEditTxt2 = txt;
	}

	if (mFindRefsThread && mFindRefsThread->IsRunning())
		return; // don't refresh while thread is still running

	BOOL ok = ValidateInput();

	CButton* pRenameBtn = (CButton*)GetDlgItem(IDC_RENAME);
	if (pRenameBtn)
		pRenameBtn->EnableWindow(ok);
}

void EncapsulateFieldDlg::OnTogglePublicAccessors()
{
	Psettings->mEncFieldPublicAccessors = !Psettings->mEncFieldPublicAccessors;
}

void EncapsulateFieldDlg::OnMoveVarPrivate()
{
	Psettings->mEncFieldMoveVariable = 0;
}

void EncapsulateFieldDlg::OnMoveVarProtected()
{
	Psettings->mEncFieldMoveVariable = 1;
}

void EncapsulateFieldDlg::OnMoveVarNone()
{
	Psettings->mEncFieldMoveVariable = 2;
}

void EncapsulateFieldDlg::OnMoveVarInternal()
{
	Psettings->mEncFieldMoveVariable = 3;
}

void EncapsulateFieldDlg::RegisterRenameReferencesControlMovers()
{
	EdCntPtr ed(g_currentEdCnt);
	AddSzControl(IDC_RENAME, mdRepos, mdNone);
	if (ed && IsCFile(ed->m_ftype))
		AddSzControl(IDC_EDIT1, mdHalfsize, mdNone);

	AddSzControl(IDC_EDIT_SECOND, mdHalfpossize, mdNone);
}

#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
void EncapsulateFieldDlg::OnTypePropertyCppB()
{
	Psettings->mEncFieldTypeCppB = tagSettings::ceft_property;

	SetGetterGUIState();
	SetSetterGUIState();
	SetGetterSetterTextCppB();

	WTString getterCaption = cStatusMsgRead;
	WTString setterCaption = cStatusMsgWrite;
	SetGetterSetterCaptionCppB(getterCaption, setterCaption);

	((CButton*)GetDlgItem(IDC_STATIC_ENC_PROP_CPPB))->ShowWindow(SW_SHOW);
	((CButton*)GetDlgItem(IDC_RADIO_ENC_PROP_PUBLISHED_CPPB))->ShowWindow(SW_SHOW);
	((CButton*)GetDlgItem(IDC_RADIO_ENC_PROP_PUBLIC_CPPB))->ShowWindow(SW_SHOW);
	((CButton*)GetDlgItem(IDC_RADIO_ENC_PROP_PRIVATE_CPPB))->ShowWindow(SW_SHOW);
	((CButton*)GetDlgItem(IDC_RADIO_ENC_PROP_PROTECTED_CPPB))->ShowWindow(SW_SHOW);
	((CButton*)GetDlgItem(IDC_RADIO_ENC_PROP_NONE_CPPB))->ShowWindow(SW_SHOW);
	((CButton*)GetDlgItem(IDC_CHK_READ_FIELD_CPPB))->ShowWindow(SW_SHOW);
	((CButton*)GetDlgItem(IDC_CHK_READ_VIRTUAL_CPPB))
	    ->ShowWindow(Psettings->mEnableEncFieldReadFieldCppB ? SW_HIDE : SW_SHOW);
	((CButton*)GetDlgItem(IDC_CHK_WRITE_FIELD_CPPB))->ShowWindow(SW_SHOW);
	((CButton*)GetDlgItem(IDC_CHK_WRITE_VIRTUAL_CPPB))
	    ->ShowWindow(Psettings->mEnableEncFieldWriteFieldCppB ? SW_HIDE : SW_SHOW);
	((CButton*)GetDlgItem(IDC_STATIC_ENC_FIELD))->SetWindowText("Accessor visibility");

	// in case that symbol is hidden in private or protected or is hidden because of empty
	// setter/getter we need to restore visibility on change
	((CStatic*)GetDlgItem(IDC_STATIC_ENC_FIELD))->ShowWindow(SW_SHOW);
	((CButton*)GetDlgItem(IDC_RADIO_ENC_FIELD_PRIVATE))->ShowWindow(SW_SHOW);
	((CButton*)GetDlgItem(IDC_RADIO_ENC_FIELD_PROTECTED))->ShowWindow(SW_SHOW);
	((CButton*)GetDlgItem(IDC_RADIO_ENC_FIELD_NONE))->ShowWindow(SW_SHOW);
	((CButton*)GetDlgItem(IDC_CHECK_ENC_FIELD_PUBLIC_ACCESSORS))->ShowWindow(SW_HIDE);

	// put focus back to control after all GUI changes
	((CButton*)GetDlgItem(IDC_RADIO_ENC_FIELD_PROPERTY_CPPB))->SetFocus();
}

void EncapsulateFieldDlg::OnTypeGetSetCppB()
{
	Psettings->mEncFieldTypeCppB = tagSettings::ceft_geter_setter;

	SetGetterGUIState();
	SetSetterGUIState();
	SetGetterSetterTextCppB();

	WTString getterCaption = cStatusMsg;
	WTString setterCaption = cStatusMsgSetter;
	SetGetterSetterCaptionCppB(getterCaption, setterCaption);

	((CButton*)GetDlgItem(IDC_STATIC_ENC_PROP_CPPB))->ShowWindow(SW_HIDE);
	((CButton*)GetDlgItem(IDC_RADIO_ENC_PROP_PUBLISHED_CPPB))->ShowWindow(SW_HIDE);
	((CButton*)GetDlgItem(IDC_RADIO_ENC_PROP_PUBLIC_CPPB))->ShowWindow(SW_HIDE);
	((CButton*)GetDlgItem(IDC_RADIO_ENC_PROP_PRIVATE_CPPB))->ShowWindow(SW_HIDE);
	((CButton*)GetDlgItem(IDC_RADIO_ENC_PROP_PROTECTED_CPPB))->ShowWindow(SW_HIDE);
	((CButton*)GetDlgItem(IDC_RADIO_ENC_PROP_NONE_CPPB))->ShowWindow(SW_HIDE);
	((CButton*)GetDlgItem(IDC_CHK_READ_FIELD_CPPB))->ShowWindow(SW_HIDE);
	((CButton*)GetDlgItem(IDC_CHK_READ_VIRTUAL_CPPB))->ShowWindow(SW_HIDE);
	((CButton*)GetDlgItem(IDC_CHK_WRITE_FIELD_CPPB))->ShowWindow(SW_HIDE);
	((CButton*)GetDlgItem(IDC_CHK_WRITE_VIRTUAL_CPPB))->ShowWindow(SW_HIDE);
	((CButton*)GetDlgItem(IDC_STATIC_ENC_FIELD))->SetWindowText("Field visibility");

	if (mHidden)
	{
		// in case that symbol is hidden in private or protected we need to restore visibility on change
		((CStatic*)GetDlgItem(IDC_STATIC_ENC_FIELD))->ShowWindow(SW_HIDE);
		((CButton*)GetDlgItem(IDC_RADIO_ENC_FIELD_PRIVATE))->ShowWindow(SW_HIDE);
		((CButton*)GetDlgItem(IDC_RADIO_ENC_FIELD_PROTECTED))->ShowWindow(SW_HIDE);
		((CButton*)GetDlgItem(IDC_RADIO_ENC_FIELD_NONE))->ShowWindow(SW_HIDE);
		((CButton*)GetDlgItem(IDC_CHECK_ENC_FIELD_PUBLIC_ACCESSORS))->ShowWindow(SW_SHOW);
	}

	// put focus back to control after all GUI changes
	((CButton*)GetDlgItem(IDC_RADIO_ENC_FIELD_GETSET_CPPB))->SetFocus();
}

void EncapsulateFieldDlg::OnToggleReadFieldCppB()
{
	Psettings->mEnableEncFieldReadFieldCppB = !!((CButton*)GetDlgItem(IDC_CHK_READ_FIELD_CPPB))->GetCheck();
	SetGetterSetterTextCppB(true, false);
	((CButton*)GetDlgItem(IDC_CHK_READ_VIRTUAL_CPPB))
	    ->ShowWindow(Psettings->mEnableEncFieldReadFieldCppB ? SW_HIDE : SW_SHOW);
	SetGetterGUIState();
}

void EncapsulateFieldDlg::OnToggleReadVirtualCppB()
{
	Psettings->mEnableEncFieldReadVirtualCppB = !!((CButton*)GetDlgItem(IDC_CHK_READ_VIRTUAL_CPPB))->GetCheck();
}

void EncapsulateFieldDlg::OnToggleWriteFieldCppB()
{
	Psettings->mEnableEncFieldWriteFieldCppB = !!((CButton*)GetDlgItem(IDC_CHK_WRITE_FIELD_CPPB))->GetCheck();
	SetGetterSetterTextCppB(false, true);
	((CButton*)GetDlgItem(IDC_CHK_WRITE_VIRTUAL_CPPB))
	    ->ShowWindow(Psettings->mEnableEncFieldWriteFieldCppB ? SW_HIDE : SW_SHOW);
	SetSetterGUIState();
}

void EncapsulateFieldDlg::OnToggleWriteVirtualCppB()
{
	Psettings->mEnableEncFieldWriteVirtualCppB = !!((CButton*)GetDlgItem(IDC_CHK_WRITE_VIRTUAL_CPPB))->GetCheck();
}

void EncapsulateFieldDlg::OnMovePropPublishedCppB()
{
	Psettings->mEncFieldMovePropertyCppB = tagSettings::cpv_published;
}

void EncapsulateFieldDlg::OnMovePropPublicCppB()
{
	Psettings->mEncFieldMovePropertyCppB = tagSettings::cpv_public;
}

void EncapsulateFieldDlg::OnMovePropPrivateCppB()
{
	Psettings->mEncFieldMovePropertyCppB = tagSettings::cpv_private;
}

void EncapsulateFieldDlg::OnMovePropProtectedCppB()
{
	Psettings->mEncFieldMovePropertyCppB = tagSettings::cpv_protected;
}

void EncapsulateFieldDlg::OnMovePropNoneCppB()
{
	Psettings->mEncFieldMovePropertyCppB = tagSettings::cpv_no;
}

void EncapsulateFieldDlg::SetGetterSetterCaptionCppB(const WTString& getterCaption, const WTString& setterCaption)
{
	::SetWindowTextW(GetDlgItem(IDC_STATUS)->GetSafeHwnd(), getterCaption.Wide());
	::SetWindowTextW(GetDlgItem(IDC_EDIT2_LABEL)->GetSafeHwnd(), setterCaption.Wide());
}

void EncapsulateFieldDlg::SetGetterSetterTextCppB(bool setGetter, bool setSetter)
{
	WTString getterName;
	WTString setterName;

	if (Psettings->mEncFieldTypeCppB == tagSettings::ceft_property)
	{
		// set read field text
		if (Psettings->mEnableEncFieldReadFieldCppB)
			getterName = "F" + mSymbolName;
		else
			getterName = "Get" + mSymbolName;

		// set write field text
		if (Psettings->mEnableEncFieldWriteFieldCppB)
			setterName = "F" + mSymbolName;
		else
			setterName = "Set" + mSymbolName;
	}
	else
	{
		getterName = mOriginalGetterName;
		setterName = mOriginalSetterName;
	}

	if (setGetter)
		mEdit_subclassed.SetText(getterName.Wide());

	if (setSetter)
		mEdit_subclassed2.SetText(setterName.Wide());
}

void EncapsulateFieldDlg::SetGetterGUIState()
{
	bool getterEnabled =
	    Psettings->mEncFieldTypeCppB == tagSettings::ceft_geter_setter ||
	    (Psettings->mEncFieldTypeCppB == tagSettings::ceft_property && !Psettings->mEnableEncFieldReadFieldCppB);
	mEdit_subclassed.EnableWindow(getterEnabled);

	if (getterEnabled)
	{
		// fix for edit box not became immediately enabled
		mEdit_subclassed.SetFocus();
		((CButton*)GetDlgItem(IDC_CHK_READ_FIELD_CPPB))->SetFocus();
	}
}

void EncapsulateFieldDlg::SetSetterGUIState()
{
	bool setterEnabled =
	    Psettings->mEncFieldTypeCppB == tagSettings::ceft_geter_setter ||
	    (Psettings->mEncFieldTypeCppB == tagSettings::ceft_property && !Psettings->mEnableEncFieldWriteFieldCppB);
	mEdit_subclassed2.EnableWindow(setterEnabled);

	if (setterEnabled)
	{
		// fix for edit box not became immediately enabled
		mEdit_subclassed2.SetFocus();
		((CButton*)GetDlgItem(IDC_CHK_WRITE_FIELD_CPPB))->SetFocus();
	}
}

#endif
