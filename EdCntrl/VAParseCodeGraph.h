#include "CodeGraph.h"
#include "DatabaseDirectoryLock.h"
#include "ProjectInfo.h"
#include "FileTypes.h"
#include "FindReferences.h"
#include "Directories.h"
#include "FileFinder.h"

namespace CodeGraphNS
{
// GraphCodeFile() parses a file and adds all references to the passed node list.
// If SymScope is passed, it will only scope and add references in that scope only.
class VAParseCodeGraph : public VAParseMPScope
{
  public:
	VAParseCodeGraph(int fType) : VAParseMPScope(fType)
	{
		mVaIconId = 0;
		mEventDt = DType();
		mLastMember = DType();
		mLastType = DType();
		mIsPlusEqual = false;
	}
	VAParseCodeGraph(const VAParseCodeGraph &) = default;

	~VAParseCodeGraph()
	{
	}
	VAParseCodeGraph& operator=(const VAParseCodeGraph&) = default;

	static void StaticGraphCodeFile(LPCWSTR file, LPCSTR symscope, IReferenceNodesContainer* dgml);

  protected:
	virtual LPCSTR GetParseClassName() override
	{
		// So it behaves the same as FindUsage in xaml/html/...
		return "VAParseMPFindUsage";
	}

	virtual void ClearLineState(LPCSTR cPos) override
	{
		mLastSymbol.reset();
		__super::ClearLineState(cPos);
		mGraphInfo[m_deep] = 0;
		mEventDt = DType();
		mLastType = DType();
		if (m_cp > 0 &&
		    !strchr("(<", m_buf[m_cp - 1])) // button.click += new handler<event>(method); // don't clear in paren's
		{
			mLastMember = DType();
			mIsPlusEqual = false;
			mVaIconId = 0;
		}
	}

	virtual void IncCP() override
	{
		__super::IncCP();

		if (CurChar() == '+' && NextChar() == '=')
		{
			if (mVaIconId && State().m_parseState == VPS_ASSIGNMENT)
			{
				State().m_defType = (ULONG)mVaIconId;
				mVaIconId = 0;
			}
			if (!InComment() && State().m_parseState != VPS_ASSIGNMENT)
				mIsPlusEqual = true;
		}
	}

	virtual void OnDef() override
	{
		if (!InLocalScope())
		{
			DTypePtr type = GetCWData(m_deep);
			if (type)
				AddNodeReference(type.get(), mLastSymbol ? mLastSymbol.get() : NULL, Ref);
		}
		__super::OnDef();
	}

	virtual void OnCSym() override
	{
		mLastSymbol = GetCWData(m_deep);

		__super::OnCSym();

		if (StartsWithNC(CurPos(), "new"))
			mGraphInfo[m_deep] |= CreatesNew;
		if (StartsWithNC(CurPos(), "List") || StartsWithNC(CurPos(), "Array"))
			mGraphInfo[m_deep] |= OneToMany;
	}

	virtual int OnSymbol() override
	{
		int retval = __super::OnSymbol();

		if (StartsWith(State().m_begLinePos, "friend"))
			return retval;

		if (GetFType() == XAML)
		{
			DTypePtr dt = GetCWData(m_deep);
			DType base_cd = m_mp->FindExact2(GetBaseScope());
			if (dt && base_cd.type() && StartsWith(dt->Scope(), base_cd.Scope()))
			{
				if (mLastMember.type())
					AddNodeReference(&mLastMember, dt.get(), EventCB | Call);
				AddNodeReference(&base_cd, dt.get(), EventCB | Call);
				mLastMember = dt.get();
			}
			// 				else if(dt)
			// 					m_LastMember = dt;
			return retval;
		}

		if (State().m_parseState != VPS_ASSIGNMENT && !InParen(m_deep))
		{
			DTypePtr dt = GetCWData(m_deep);
			if (dt)
			{
				if (mLastType.type() != 0 && dt->FileId() == m_mp->GetFileID() && State().m_defType == TYPE)
				{
					if (GetCStr(State().m_lastWordPos) == mLastType.Sym())
						AddNodeReference(dt.get(), &mLastType, Inherit); // typedef Foo Bar; // Bar "is a" Foo
					else
						AddNodeReference(dt.get(), &mLastType,
						                 Member); // typedef List<Foo> Bar; // Bar "has a" Foo // case=67555
				}
				if (dt->IsType())
					mLastType = dt.get();
				else if ((dt->MaskedType() == EVENT || dt->MaskedType() == VAR || dt->MaskedType() == PROPERTY ||
				          (dt->MaskedType() >= Control_First && dt->MaskedType() <= Control_Last)) &&
				         !dt->IsSystemSymbol())
					mLastMember = dt.get();
			}
		}

		if (mGraphScope.GetLength() && Scope(m_deep).find(mGraphScope) != 0)
			return retval;
		DType scopeDt(TokenGetField(Scope(), "-"), TokenGetField(Scope(), "-"), FUNC, NULL, NULL, (int)CurLine(),
		              m_mp->GetFileID());
		WTString scope = Scope();
		WTString baseScope = StrGetSym(TokenGetField(scope, "-"));

		DType base_cd = m_mp->FindExact2(TokenGetField(scope, "-"));
		// TODO: if ends in "." or "->" don't add reference
		if (!base_cd.type())
		{
			if (m_deep > 1 && InTemplateArg(m_deep))
			{
				DTypePtr dt = GetCWData(m_deep - 2);
				DTypePtr ref_cd = GetCWData(m_deep);
				if (ref_cd != NULL)
					mLastType = ref_cd.get();
				if (dt != NULL && ref_cd != NULL)
				{
					if (ref_cd->IsType()) // class C{ T<MemberType> _mbr; } // Add C->(has)MemberType
						AddNodeReference(dt.get(), ref_cd.get(), Member);
					return retval;
				}
			}
			else
				base_cd = &scopeDt;
		}

		if (base_cd.type())
		{
			DTypePtr ref_cd = GetCWData(m_deep);

			// [case: 67855] c++ enum items are stored at two different
			// scopes :enumType:enunItem and :enumItem.
			// see VAParseMPReparse::OnAddSym.
			// the parser recognizes their usage as :enumItem, but for
			// spaghetti purposes we want to resolve them to
			// :enumType:enumItem. There is probably a better/faster
			// way of doing this reverse lookup...
			if (ref_cd && ref_cd->MaskedType() == C_ENUMITEM)
			{
				auto parent = m_mp->FindExact(ref_cd->Scope());
				if (!parent || parent->MaskedType() != C_ENUM)
				{
					// check for unnamed enum
					auto unnamedEnumReverseLookup = m_mp->FindExact(":va_unnamed_enum_scope" + ref_cd->SymScope());
					if (unnamedEnumReverseLookup != NULL)
					{
						auto fullyScopedEnum = m_mp->FindExact(unnamedEnumReverseLookup->Def());
						if (fullyScopedEnum != NULL)
						{
							ref_cd = std::make_shared<DType>(fullyScopedEnum);
						}
					}
					else
					{
						WTString def = ref_cd->Def();
						// Def() format is "enum EnumType ENUMITEM = 0"
						// extract "EnumType"
						if (def.Find("enum ") == 0)
						{
							def = def.Mid(5);
							token2 tk = def;
							def = tk.read(' ');
							if (def.GetLength())
							{
								WTString lookup = ref_cd->Scope() + DB_SEP_STR + def + DB_SEP_STR + ref_cd->Sym();
								auto newRefCd = m_mp->FindExact(lookup);
								if (newRefCd)
									ref_cd = std::make_shared<DType>(newRefCd);
							}
						}
					}
				}
			}

			if (ref_cd && ref_cd->SymScope().Find(EncodeChar('<')) != -1)
			{
				// MyTemplate<T_Arg>.foo(), strip <...> to  MyTemplate.foo()
				WTString tmplate = StripEncodedTemplates(ref_cd->SymScope());
				DType* dt = m_mp->FindExact(tmplate);
				if (dt)
					ref_cd = std::make_shared<DType>(dt);
			}

			if (ref_cd && mEventDt.type() && ref_cd->MaskedType() == FUNC)
				base_cd = DType(mEventDt);

			if (ref_cd && Is_C_CS_VB_File(GetFType()) && ref_cd->ScopeHash() == 0 && ref_cd->inproject())
			{
				// TODO: see if we are using a global from a c++ project in managed code.
				DType* pManaged = m_mp->SDictionary()->FindAnySym(ref_cd->Sym());
				if (pManaged)
					ref_cd = std::make_shared<DType>(pManaged);
			}

			if (ref_cd && ref_cd->MaskedType() == EVENT && !Is_Tag_Based(FileType()))
			{
				WTString lastword = GetCStr(State().m_lastWordPos);
				mDtypeInst = m_mp->FindSym(&lastword, &scope, NULL);
				if (!mDtypeInst.IsReservedType() && mDtypeInst.Def().length())
					mEventDt = &mDtypeInst;
				else
					mEventDt = ref_cd.get();
			}

			// 				if(FileType() == HTML)
			// 					AddNodeReference(&base_cd, ref_cd, NodeType_HTMLProperty);

			if (m_deep && State(m_deep - 1).m_lastChar == ':' &&
			    State(m_deep - 1).HasParserStateFlags(VPSF_INHERITANCE))
			{
				if (ref_cd)
				{
					switch (ref_cd->type())
					{
					case C_INTERFACE:
						AddNodeReference(&base_cd, ref_cd.get(), IInterface); // Interface
						break;
					case CLASS:
					case STRUCT: {
						AddNodeReference(&base_cd, ref_cd.get(), Inherit); // class inheritance
						DType* fromconstructor = m_mp->FindExact2(base_cd.SymScope() + COLONSTR + base_cd.Sym());
						DType* toconstructor = m_mp->FindExact2(ref_cd->SymScope() + COLONSTR + ref_cd->Sym());
						if (fromconstructor && toconstructor)
							AddNodeReference(fromconstructor, toconstructor, Call);
					}
					break;

					default:
						// type could be namespace, not class/iface, such as,
						// class Foo : ns1.IFoo
						break;
					}
				}
			}
			else if (!InLocalScope() && State().m_parseState != VPS_ASSIGNMENT)
			{
				WTString modStr = TokenGetField(CurPos(), ")=/',\r\n");

				if (modStr.contains(".") || modStr.contains("->") || modStr.contains("?."))
				{
					AddNodeReference(&base_cd, ref_cd.get(), Ref); // NS.CLASS _CLS;
				}
				else if (modStr.contains("("))
				{
					// return types of functions are refs not members.
					if (ref_cd &&
					    ref_cd->Scope() == base_cd.SymScope()) // class cls{ void func();} // func doesn't reference cls
						AddNodeReference(ref_cd.get(), NULL,
						                 Call); // Add node, cause it won't get added later if body is empty.
					else
						AddNodeReference(&base_cd, ref_cd.get(), Ref);
				}
				else if (ref_cd && ref_cd->IsType() &&
				         base_cd.MaskedType() != NAMESPACE) // class w/in a class [case=72538]
				{
					AddNodeReference(&base_cd, ref_cd.get(), Member);
				}
				else if (ref_cd && ref_cd->Scope() != base_cd.SymScope() &&
				         base_cd.MaskedType() !=
				             NAMESPACE) // [case=71837] Don't add links to actual members, just "has type" links
				{
					AddNodeReference(&base_cd, ref_cd.get(), Member);
				}
				else
				{
					AddNodeReference(&base_cd, NULL, Ref);
					AddNodeReference(ref_cd.get(), NULL, Ref);
				}

				if (m_deep && ref_cd && ref_cd->Scope() == base_cd.SymScope())
				{
					// Get overridden methods
					WTString baseClass = GetBaseScope();
					token2 tbc = m_mp->GetBaseClassList(baseClass, false, mMonitorForQuit, GetLangType());
					tbc.read('\f');
					WTString parentbcl = tbc.Str(); // .read("\f");
					if (parentbcl.GetLength())
					{
						WTString sym = ref_cd->Sym();
						DType* overrides = m_mp->FindSym(&sym, NULL, &parentbcl);
						if (overrides && !(*overrides == *ref_cd.get()))
							mDgml->AddLink(overrides, ref_cd.get(), Call | Override, (int)CurLine(), m_mp.get());
					}
				}
			}
			else if (Is_Tag_Based(FileType()))
			{
				AddNodeReference(&base_cd, ref_cd.get(), Ref);
			}
			else
			{
				UINT refType = /*(ref_cd && ref_cd->IsMethod()) ? Call :*/ GetRefType();
				if (!(refType & Call) && ref_cd && ref_cd->IsMethod())
					refType |= EventCB;
				if (ref_cd && ref_cd->Def().contains("[]"))
					refType |= OneToMany;
				if (ref_cd && mIsPlusEqual && mLastMember.type() && ref_cd->MaskedType() == FUNC)
				{
					refType = Call | EventCB;
					base_cd = DType(mLastMember);
				}
				if ((refType & Ref) && ref_cd && ref_cd->MaskedType() == EVENT)
					refType = Call | EventCB; // Fire event [case=72739]
				AddNodeReference(&base_cd, ref_cd.get(), refType);
			}
		}
		return retval;
	}

	virtual BOOL IsDone() override
	{
		if (!IsCFile(gTypingDevLang) && mBlockdeep > m_deep)
			return TRUE;
		return __super::IsDone();
	}

	virtual void DecDeep() override
	{
		if (m_inIFDEFComment)
		{
			// [case: 73884]
			return;
		}

		if (m_deep && InLocalScope(m_deep) && !InLocalScope(m_deep - 1))
		{
			int linesOfCode = (int)CurLine() - (int)State(m_deep - 1).m_StatementBeginLine;
			if (linesOfCode > 1)
				SetNodeLines(State(m_deep - 1).m_lwData.get(), (uint)linesOfCode);
		}
		__super::DecDeep();
		if (m_Scoping && CurChar() == '}' && mBlockdeep > m_deep && mGraphScope.GetLength())
		{
			m_Scoping = FALSE;
			mBlockdeep = 0;
		}
	}

	virtual void IncDeep() override
	{
		if (m_inIFDEFComment)
		{
			// [case: 73884]
			return;
		}

		__super::IncDeep();
		mGraphInfo[m_deep] = 0;
		if (CurChar() == '[' && TokenGetField(CurPos(), " \t\r\n[(") == "DllImport")
		{
			ReadToCls rtc(GetLangType());
			rtc.ReadTo(State(m_deep - 1).m_begLinePos,
			           GetBufLen() - ptr_sub__int(State(m_deep - 1).m_begLinePos, GetBuf()), "(;");
			WTString impName = GetCStr(rtc.State().m_lastScopePos);
			AddNodeReference(m_mp->FindExact(Scope() + COLONSTR + impName), m_mp->FindExact2(COLONSTR + impName),
			                 Call); // FindExact2 so we can get gotodefs
		}

		if (!mGraphScope.GetLength())
			m_Scoping = TRUE;
		else if (CurChar() == '{' && mGraphScope.GetLength())
		{
			if (!m_Scoping && CurChar() == '{' && Scope().contains(mGraphScope))
			{
				m_Scoping = TRUE;
				mBlockdeep = m_deep;
			}
		}
	}

  private:
	void AddNodeReference(DType* refScope, DType* pointsTo, UINT refType)
	{
		bool isCreates = (refType & CreatesNew) != 0;
		bool isCalls = (refType & Call) != 0;

		if (isCreates || isCalls)
		{
			if (refScope && pointsTo && pointsTo->IsType())
			{
				if (pointsTo->MaskedType() == TEMPLATETYPE || pointsTo->MaskedType() == TYPE ||
				    refScope->SymHash() == 0)
				{
					// skip
				}
				else
				{
					isCreates = true;

					// Class *x = new Class();
					// Class x = Class();
					// Add link to constructor

					if (refScope && refScope->MaskedType() == FUNC)
					{
						DType* constructor =
						    m_mp->FindExact2(pointsTo->SymScope() + WTString(DB_SEP_CHR) + pointsTo->Sym());
						if (constructor)
							mDgml->AddLink(refScope, constructor, Call, (int)CurLine(), m_mp.get());
					}
					mDgml->AddLink(refScope, pointsTo, CreatesNew, (int)CurLine(), m_mp.get());
				}
			}
		}

		else if (mVaIconId && refScope && refScope->MaskedType() == VAR)
			refScope->setType((uint)mVaIconId, refScope->Attributes(),
			                  refScope->DbFlags()); // Change Var to a UI icon type
		else if (mVaIconId && pointsTo && pointsTo->MaskedType() == VAR)
			pointsTo->setType((uint)mVaIconId, pointsTo->Attributes(),
			                  pointsTo->DbFlags()); // Change Var to a UI icon type

		if (isCreates)
			refType &= ~(CreatesNew | Call); // clear flags, already handled above

		if (refType)
			mDgml->AddLink(refScope, pointsTo, refType, (int)CurLine(), m_mp.get());
	}

	void SetNodeLines(DType* refScope, UINT lines)
	{
		mDgml->SetLinesOfCode(refScope, lines, int(CurLine() - lines));
	}

	void GraphCodeFile(LPCWSTR file, LPCSTR symscope, IReferenceNodesContainer* dgml)
	{
		mDgml = dgml;
		mBlockdeep = 0;
		mGraphScope = symscope;
		m_Scoping = FALSE;
		if (Is_Tag_Based(FileType()))
			m_Scoping = TRUE;         // So GraphSymbol graphs associated xaml/asp/... file
		m_firstVisibleLine = 1000000; // prevent m_Scoping from being turned on
		m_parseTo = 0x7fffffff;
		if (GetLangType() == Src || GetLangType() == Header)
			m_processMacros = TRUE;

		LoadLocalsAndParseFile(file);
	}

	UINT GetRefType()
	{
		// Basic check from VAParseMPFindUsage.AddRef
		UINT type = Ref;
		LPCSTR p = CurPos();
		while (ISCSYM(p[0]))
			p++;
		if (p[0] == '.' || (p[0] == '-' && p[1] == '>'))
			return Ref; // foo.bar(), foo is only a reference, while bar is a call
		while (wt_isspace(p[0]))
			p++;
		if (p[0] == '=' && p[1] == '=')
			type = Ref;
		else if (m_deep && State(m_deep - 1).HasParserStateFlags(VPSF_CONSTRUCTOR))
			type = Mod;
		else if (p[0] == '(')
			type = Call; // initializer "Property(value)"
		else if (p[0] == '=' || (p[0] != '>' && p[0] != '<' && p[0] != '!' && p[1] == '=') ||
		         (p[0] == '+' && p[1] == '+') || (p[0] == '-' && p[1] == '-'))
			type = Mod;
		WTString scope = Scope();
		if (scope.contains(":if-") || scope.contains(":else-") || scope.contains(":switch-") ||
		    StartsWith(State().m_begLinePos, "if") || StartsWith(State().m_begLinePos, "else"))
		{
			type |= CalledInIf;
		}
		if (scope.contains(":while-") || scope.contains(":for-") || scope.contains(":foreach-"))
			type |= CalledInLoop;
		if (scope.contains(":try-"))
			type |= CalledInTry;
		return type | mGraphInfo[m_deep];
	}

	WTString mGraphScope;
	UINT mBlockdeep;
	IReferenceNodesContainer* mDgml;
	DType mLastType;
	DType mLastMember;
	bool mIsPlusEqual;
	int mVaIconId;
	DTypePtr mLastSymbol;
	DType mDtypeInst;
	DType mEventDt;
	int mGraphInfo[STATE_COUNT + 1];
};
} // namespace CodeGraphNS

extern "C" __declspec(dllexport) LPCWSTR VAGraphCallback(LPCWSTR idStrW);
