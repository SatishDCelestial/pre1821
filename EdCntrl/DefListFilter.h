#pragma once

#include "StringUtils.h"
#include "FOO.H"
#include "VAParse.h"
#include "PROJECT.H"
#include "FileId.h"
#include "BaseClassList.h"
#include "CreateMissingCases.h"
#include "SymbolPositions.h"

extern void ReplaceWithRealQualifiers(WTString& sc);
extern WTString UniformizeWhitespaces(WTString str);

class VirtualDefList
{
  public:
	virtual ~VirtualDefList() = default;

	virtual void SetList(SymbolPosList* _list)
	{
	}
	virtual void SetList(DTypeList* _list)
	{
	}
	virtual bool NotEnd() = 0;
	virtual WTString GetDef() = 0;
	virtual void EraseCurrentAndIterate() = 0;
	virtual void Iterate() = 0;
	virtual void ResetIterator() = 0;
	virtual bool IsEmpty() = 0;
	virtual int Size() = 0;
	virtual EdCntPtr GetFileSpecificEdCnt() = 0;

	int counter = 0;
};

class VirtualDefListGoto : public VirtualDefList
{
  public:
	SymbolPosList::iterator it;
	SymbolPosList* list = nullptr;

	void SetList(SymbolPosList* _list) override
	{
		list = _list;
	}
	using VirtualDefList::SetList;

	bool NotEnd() override
	{
		return it != list->end();
	}

	WTString GetDef() override
	{
		const SymbolPositionInfo& curInfo(*it);
		return curInfo.mDef;
	}

	void EraseCurrentAndIterate() override
	{
		list->erase(it++);
	}

	void Iterate() override
	{
		it++;
		counter++;
	}

	void ResetIterator() override
	{
		it = list->begin();
		counter = 0;
	}

	bool IsEmpty() override
	{
		return list->size() == 0;
	}

	virtual int Size() override
	{
		if (list == nullptr)
			return 0;

		return (int)list->size();
	}
	virtual EdCntPtr GetFileSpecificEdCnt() override
	{
		return GetOpenEditWnd(it->mFilename);
	}
};

class VirtualDefListGotoRelated : public VirtualDefList
{
  public:
	DTypeList::iterator it;
	DTypeList* list = nullptr;

	void SetList(DTypeList* _list)
	{
		list = _list;
	}
	using VirtualDefList::SetList;

	bool NotEnd()
	{
		return it != list->end();
	}

	WTString GetDef()
	{
		return it->Def();
	}

	void EraseCurrentAndIterate()
	{
		list->erase(it++);
	}

	void Iterate()
	{
		it++;
		counter++;
	}

	void ResetIterator()
	{
		it = list->begin();
		counter = 0;
	}

	bool IsEmpty()
	{
		return !list || list->size() == 0;
	}

	virtual int Size()
	{
		if (list == nullptr)
			return 0;

		return (int)list->size();
	}
	virtual EdCntPtr GetFileSpecificEdCnt()
	{
		return GetOpenEditWnd(gFileIdManager->GetFile(it->FileId()));
	}
};

class OverloadResolver;

class TypeListComparer : public std::vector<WTString>
{
  public:
	enum TargetMethodsType
	{
		CAST_OPERATOR,
		CAST_CONSTRUCTOR
	};

	TypeListComparer(OverloadResolver* owner)
	{
		Owner = owner;
	}

	static bool IsBasicType(const WTString& type)
	{
		return IsPrimitiveType(type) || IsStringType(type);
	}
	static bool IsStringType(const WTString& type)
	{
		if (IsCFile(gTypingDevLang))
		{
			return type == "string" || type == "CString" || type == "TCHAR*" || type == "WCHAR*" || type == "LPSTR" ||
			       type == "LPCSTR" || type == "LPWSTR" || type == "LPCWSTR" || type == "LPTSTR" || type == "LPCTSTR" ||
			       type == "char*" || type == "std::string";
		}
		else
		{
			return type == "string" || type == "String" || type == "System.String";
		}
	}
	static bool IsPrimitiveType(const WTString& type)
	{
		if (IsCFile(gTypingDevLang))
			return IsPrimitiveCType(type);
		else
			return IsPrimitiveCSType(type);
	}
	static bool IsPrimitiveCType(const WTString& type)
	{
		return type == "bool" || type == "char" || type == "int" || type == "float" || type == "double" ||
		       type == "void" || type == "wchar_t" || type == "unsigned" || type == "short" || type == "long" ||
		       type == "signed" || type == "TCHAR" || type == "WCHAR";
	}
	WTString GetCastedPrimitiveType(WTString type)
	{
		type.ReplaceAll("unsigned", "", TRUE);
		type.ReplaceAll("signed", "", TRUE);
		type.TrimLeft();

		return type;
	}

	static bool IsPrimitiveCSType(WTString type)
	{
		if (type.Left(7) == "System.")
			type = type.Mid(7);

		return type == "bool" || type == "Boolean" || type == "byte" || type == "Byte" || type == "sbyte" ||
		       type == "SByte" || type == "char" || type == "Char" || type == "decimal" || type == "Decimal" ||
		       type == "double" || type == "Double" || type == "float" || type == "Single" || type == "int" ||
		       type == "Int32" || type == "uint" || type == "UInt32" || type == "long" || type == "Int64" ||
		       type == "ulong" || type == "UInt64" || type == "object" || type == "Object" || type == "short" ||
		       type == "Int16" || type == "ushort" || type == "UInt16";
	}

	WTString ExpandTypeDefOrMacro(const WTString& typdef, bool macro);
	static WTString ExpandTypeDefInner(const WTString& typdef, int startPos, bool macro);

	static bool IsTextMacro(const WTString& type)
	{
		return type.Left(3) == "_T(" || type.Left(6) == "_TEXT(" || type.Left(5) == "TEXT(";
	}

	static std::pair<int, WTString> GetIfSingleWord(const WTString& type, bool acceptQualifier = false);

	bool operator==(TypeListComparer& right)
	{
		if (HighPrecisionMode)
			return size() >= right.NonDefaultParams && (right.Ellipsis || size() <= right.size());

		if (!Ref && size() != right.size())
			return false;
		else if (size() < right.NonDefaultParams || (!right.Ellipsis && size() > right.size()))
			return false;

		// case 101287
		if (gTypingDevLang == CS && size() == right.NonDefaultParams && right.NonDefaultParams != right.size())
			right.SetDeleteMePriority(2);

		size_t consideredArrayLen = size();
		if (Ref && right.Ellipsis)
			consideredArrayLen = right.size() - 1;
		for (size_t i = 0; i < consideredArrayLen; i++)
		{
			WTString curr = operator[](i);
			WTString type = right[i];
			bool zero = false;
			if (curr == "const int0")
			{
				zero = true;
				curr = "const int";
			}

			WTString oriCurr = curr;
			WTString oriType = type;
			curr = ExpandTypeDefOrMacro(curr, false);
			type = ExpandTypeDefOrMacro(type, false);
			if (gTypingDevLang == CS)
			{
				int currPos = curr.Find('.');
				int typePos = type.Find('.');
				if (oriCurr != curr && currPos == -1 && typePos != -1)
					type = StrGetSym(type);
				else if (oriType != type && typePos == -1 && currPos != -1)
					curr = StrGetSym(curr);
			}

			curr.Trim();
			type.Trim();

			if (type.find('&') == -1 && type.find('*') == -1)
			{
				curr.ReplaceAll("const", "", TRUE);
				curr.ReplaceAll("CONST", "", TRUE);
				curr.Trim();
				type.ReplaceAll("const", "", TRUE);
				type.ReplaceAll("CONST", "", TRUE);
				type.Trim();
			}
			else if (Ref && (curr.Left(5) != "const" || curr.Left(5) != "CONST"))
			{
				type.ReplaceAll("const", "", TRUE);
				type.ReplaceAll("CONST", "", TRUE);
				type.Trim();
			}

			if (IsStringType(curr) && IsStringType(type))
				continue;

			if (Ref)
			{
				bool removeConst = false;
				while (type.GetLength() && (type.Right(1) == "&" /* || type.Right(1) == '*'*/))
				{
					type.ReplaceAt(type.GetLength() - 1, 1, "");
					removeConst = true;
				}
				if (removeConst) // case 109837 const variable breaks alt+g overload resolution
				{
					curr.ReplaceAll("const", "", TRUE);
					curr.ReplaceAll("CONST", "", TRUE);
					curr.Trim();
				}

				if (curr.GetLength() && curr.Right(1) == "&")
					curr.ReplaceAt(curr.GetLength() - 1, 1, "");
			}

			if (curr != type)
			{
				if (IsCFile(gTypingDevLang))
				{
					std::vector<WTString> targets = GetTargetMethods(curr, CAST_OPERATOR);
					bool castable = false;
					for (WTString val : targets)
					{
						if (val.find(IsCFile(gTypingDevLang) ? "::" : ".") != -1)
						{
							val = StrGetSym(val);
							val.Trim();
						}
						if (val == type || (IsStringType(type) && IsStringType(val)))
							castable = true;
					}

					if (castable)
						continue;

					// case 101515
					targets = GetTargetMethods(type, CAST_CONSTRUCTOR);
					castable = false;
					for (WTString val : targets)
					{
						if (val.find(IsCFile(gTypingDevLang) ? "::" : ".") != -1)
						{
							val = StrGetSym(val);
							val.Trim();
						}
						val.ReplaceAll("const", "", TRUE);
						val.ReplaceAll("CONST", "", TRUE);
						val = UniformizeWhitespaces(val);
						WTString curr2 = curr;
						curr2.ReplaceAll("const", "", TRUE);
						curr2.ReplaceAll("CONST", "", TRUE);
						curr2 = UniformizeWhitespaces(curr2);
						if (val == curr2 || (IsStringType(curr2) && IsStringType(val)))
							castable = true;
					}

					if (castable)
						continue;
				}

				EdCntPtr ed(g_currentEdCnt);
				// check type's base classes for match (case 100265)
				if (ed && !IsBasicType(curr) && curr != "" && type != "")
				{
					BaseClassFinder bcf(gTypingDevLang);
					MultiParsePtr parseDb = ed->GetParseDb();
					WTString bcl(bcf.GetBaseClassList(parseDb, curr, false, NULL));
					ReplaceWithRealQualifiers(bcl);
					if (::FindSymInBCL(bcl, type) != -1)
						continue;
				}

				WTString uniformizedType = UniformizeWhitespaces(type);
				if (Ref && zero && uniformizedType == "void*") // case 102922
				{
					right.SetDeleteMePriority(1);
					continue;
				}
				WTString uniCurr = UniformizeWhitespaces(curr);
				if (Ref && uniCurr == "const int" || uniCurr == "const long")
				{ // case 100313
					if (type.find('*') != -1)
					{
						right.SetDeleteMePriority(2);
						continue;
					}
				}
				if (Ref && curr == "nullptr" && type.find('*') != -1) // case 100313
					continue;
				if (Ref)
				{
					curr = GetCastedPrimitiveType(curr);
					type = GetCastedPrimitiveType(type);
					if (IsPrimitiveCType(curr) && IsPrimitiveCType(type))
					{
						right.SetDeleteMePriority(1);
						continue;
					}
				}
				if (Ref && curr == "null" &&
				    (!IsPrimitiveCSType(type) || ::FindInCode(type, '?', gTypingDevLang) != -1))
				{ // case 100751
					continue;
				}
				if (Ref && curr.Find("*") != -1 && uniformizedType == "void*")
				{ // case 100312
					right.SetDeleteMePriority(1);
					continue;
				}
				if (Ref && !IsBasicType(curr))
				{ // enum input at calling site. case 100311
					CreateMissingCases cmc;
					WTString scope = ed->m_lastScope;
					WTString methodScope;
					MultiParsePtr mp = ed->GetParseDb();
					DTypePtr method =
					    GetMethod(mp.get(), GetReducedScope(scope), scope, mp->m_baseClassList, &methodScope);
					if (method != nullptr)
					{
						WTString bcl = mp->GetBaseClassList(mp->m_baseClass);
						DType* dtype = nullptr;
						if (cmc.IsEnum(mp.get(), scope, methodScope, bcl, curr, &dtype) && IsPrimitiveCType(type) &&
						    type != "void")
						{
							if (dtype)
							{
								WTString enumDef = dtype->Def();
								if (gTypingDevLang == CS || !IsEnumClass(enumDef))
								{
									right.SetDeleteMePriority(1);
									continue;
								}
							}
						}
					}
				}

				return false;
			}
		}

		if (ConstFunc && !right.ConstFunc) // from a const method, we cannot call non-const methods
			return false;
		if (!ConstFunc && right.ConstFunc) // from a non-const method, we can call const methods but it isn't preferred
			right.SetDeleteMePriority(1);

		return true;
	}

	bool operator!=(TypeListComparer& right)
	{
		return !operator==(right);
	}
	bool Ref = false; // is the caret on a reference (call-site)
	uint NonDefaultParams = 0;
	bool DefaultParams = false;
	bool Ignore = false;

	// Using different method for distinguishing overloads (based on parameter count) when the method with a given
	// parameter count has only one variant. This guarantees resolving overloads correctly even with complex cases where
	// the traditional method might fail. (It compares non-default parameter count)
	bool HighPrecisionMode;
	bool Ellipsis = false;
	bool ConstFunc = false;
	std::map<WTString, std::vector<WTString>> CastCache;        // casting operators
	std::map<WTString, std::vector<WTString>> ConstructorCache; // casting constructors
	std::map<WTString, WTString> TypeDefCache;
	int GetDeleteMePriority() const
	{
		return DeleteMePriority;
	}
	void SetDeleteMePriority(int val)
	{
		if (val > DeleteMePriority)
			DeleteMePriority = val;
	}
	int IteratorCounter = -1;
	int Empty = false;
	OverloadResolver* Owner = nullptr;

  private:
	static WTString GetWithouteLastWord(const WTString& def);
	std::vector<WTString> GetTargetMethods(WTString typeName, TargetMethodsType tmt);
	bool IsCastOperator(const WTString& def, int fileType);
	int DeleteMePriority = 0;
	bool IsCastConstructor(const WTString& def, const WTString& typeName, WTString& constructorParam, int ftype);
};

class OverloadResolver
{
  public:
	enum ReferenceType
	{
		CALL_SITE,
		CONSTRUCTOR,
	};

	OverloadResolver(VirtualDefList& list) : List(list), CurTypes(this)
	{
	}
	void Resolve(ReferenceType referenceType);
	int GetNrOfEllipsisParams(WTString paramList, EdCntPtr ed);
	bool SeparateMergedEllipsisParam(WTString& strippedParamList);
	bool GetParamListFromSource(WTString& res, bool& constFunc, ReferenceType referenceType);
	static bool GetParamListFromDef(WTString def, const WTString& curName, WTString& paramList, bool& constFunc,
	                                int fileType);
	void ReplaceItemInTypeList(TypeListComparer& types, uint index, const WTString& val);
	TypeListComparer ExtractTypes(const WTString& paramList, bool ref, EdCntPtr fileSpecificEd);
	std::pair<WTString, int> GetTypeAndLocation(const WTString& paramList, int charIndex, bool ref, bool& defaultParam,
	                                            EdCntPtr fileSpecificEd);

	void QualifyTypeViaUsingNamespaces(WTString& typeName, MultiParse* mp);

	VirtualDefList& List;

  private:
	bool IsFunctionParameterPackAtEnd(const WTString& strippedParamList);
	bool IsCaretInsideConstMethod();
	std::pair<WTString, WTString> GetMethodNameAndDefWhichCaretIsInside();
	int GetArgPosByName(const WTString& argName, int ftype);

	TypeListComparer CurTypes;
	std::map<WTString, WTString> MacroCache;
	uint DefinitionIndex = 0;
	std::vector<WTString> DefinitionParameterLists;
	WTString GetParamName(const WTString& paramList, int pos);
	bool AreThereNamedArgs(const WTString& curParamList);
};

class DefListFilter
{
  public:
	DefListFilter() : kCurrentFileLn(0), kCurrentFileNextLn(0), kRemoveLn(-1), kCurrentPos(0)
	{
	}

	DefListFilter(const CStringW& filename, int curLn, int nextLn, int pos = 0, int removeLn = -1)
	    : mCurFile(filename), kCurrentFileLn(curLn), kCurrentFileNextLn(nextLn), kRemoveLn(removeLn), kCurrentPos(pos)
	{
	}

	const SymbolPosList& Filter(const SymbolPosList& defList, bool constructor = false);
	const SymbolPosList& FilterPlatformIncludes(const SymbolPosList& defList);

  private:
	// this also initializes mPosList
	void FilterDupesAndHides(const SymbolPosList& defList);

	// if there are items from the same project as the active ed,
	// then removes items from other projects
	// modifies mPosList
	void FilterNonActiveProjectDefs();

	// remove external project duplicates
	// modifies mPosList
	void FilterExternalDefs();

	// remove items that don't have V_PREFERREDDEFINITION but only if there
	// is an item in the list that does have V_PREFERREDDEFINITION (only for JS)
	// modifies mPosList
	void FilterForPreferredDefs();

	// exclude kCurrentFileLn and kCurrentFileNextLn
	// adjust display name to show shortest necessary file path
	// re-orders to place definitions before declarations
	bool TouchUpDefList();

	// C/C++ filtering of system symbols based upon current platform include
	// settings.  Platform includes can be unique to each project.  Our sys
	// db is shared among projects.
	// If item comes from sysdb and path is in current sys includes, let pass.
	// If there are items from both current includes and non-current, exclude non-current items.
	void FilterSystemDefs();

	// Exclude namespace entries we create on the fly for C#
	void FilterGeneratedNamespaces();

	// exclude generated source files (unless that would leave results empty)
	// modifies mPosList
	void FilterGeneratedSourceFiles();

  private:
	CStringW mCurFile;
	const int kCurrentFileLn;
	const int kCurrentFileNextLn;
	const int kRemoveLn;
	SymbolPosList mPosList;
	const int kCurrentPos;
	void FilterDeclaration();
};
