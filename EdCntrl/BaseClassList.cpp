#include "stdafxed.h"
#include "edcnt.h"
#include "mparse.h"
#include "foo.h"
#include "project.h"
#include "timer.h"
#include "DBLock.h"
#include "VAParse.h"
#include "BaseClassList.h"
#include "assert_once.h"
#include "FileTypes.h"
#include "WtException.h"
#include "fdictionary.h"
#include "LogElapsedTime.h"
#include "..\common\ThreadStatic.h"
#include "InferType.h"
#include "wt_stdlib.h"
#include "Settings.h"
#include "StringUtils.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

// This file originated from Mparse.cpp#47 (see changes 5377 and 5392)

const WTString kInvalidCache("+vaxd");
const WTString kBclCachePrefix("*");
static const WTString kIn1("in");
static const WTString kIn2(EncodeScope("in "));
static const WTString kIn3("in ");
static const WTString kOut1("out");
static const WTString kOut2(EncodeScope("out "));
static const WTString kOut3("out ");
const WTString kWildCardScopeStr(WILD_CARD_SCOPE);

class MakeTemplateSafety
{
  public:
	MakeTemplateSafety(volatile const INT* quitMon) : mQuit(quitMon)
	{
	}

	BOOL Check(const WTString& newTemplateName, const WTString& templateClass, const WTString& def,
	           const WTString& argClasses, uint fileId, int line)
	{
		mActiveTemplate.WTFormat("%s;%s;%s;%s;%x;%d;", newTemplateName.c_str(), templateClass.c_str(), def.c_str(),
		                         argClasses.c_str(), fileId, line);

		for (;;)
		{
			{
				AutoLockCs lck(sMakeTemplateLock);
				if (mQuit && *mQuit)
				{
					mActiveTemplate.Empty();
					return FALSE;
				}

				if (sActiveTemplates.find(mActiveTemplate) == sActiveTemplates.end())
				{
					sActiveTemplates.insert(mActiveTemplate);
					return TRUE;
				}
			}

			::Sleep(25);
		}
	}

	~MakeTemplateSafety()
	{
		if (mActiveTemplate.IsEmpty())
			return;

		// remove from active list
		AutoLockCs lck(sMakeTemplateLock);
		sActiveTemplates.erase(mActiveTemplate);
	}

  private:
	volatile const INT* mQuit;
	WTString mActiveTemplate;

	static CSpinCriticalSection sMakeTemplateLock;
	static std::unordered_set<WTString> sActiveTemplates;
};

CSpinCriticalSection MakeTemplateSafety::sMakeTemplateLock;
std::unordered_set<WTString> MakeTemplateSafety::sActiveTemplates;

// case-sensitive exact match
static bool VectorContainsExact(const WTStringV& vec, const WTString& str)
{
	const size_t kSize = vec.size();
	for (size_t idx = 0; idx < kSize; idx++)
	{
		if (str.GetLength() == vec[idx].GetLength() && ::_tcscmp(str.c_str(), vec[idx].c_str()) == 0)
			return true;
	}
	return false;
}

// case-insensitive match up to length of str (substr match)
static bool VectorContainsPartial(const WTStringV& vec, const WTString& str)
{
	const size_t kSize = vec.size();
	const size_t len = (size_t)str.GetLength();
	for (size_t idx = 0; idx < kSize; idx++)
	{
		if (::_tcsnicmp(str.c_str(), vec[idx].c_str(), len) == 0)
			return true;
	}
	return false;
}

BOOL BaseClassFinder::MakeTemplateInstance(const WTString& symScope)
{
	// declaring an instance of a template,
	// assign T from <class T> to <CWnd T> and store it in the instance local to foo::var
	// sym = foo::var::<>, def = <CWnd>
	// def of foo::var = "tfoo var"
	// tfoo:<> = "<class T>"
	// WTString bcl = mp->GetBaseClassList(s.Str()) + DB_SEP_STR;

	static const char lt_encoded(EncodeChar('<'));
	WTString templateClass =
	    {StrGetSymScope_sv(symScope), DB_SEP_STR, TokenGetField2(StrGetSym_sv(symScope), lt_encoded)};
	bool foundViaAlias = false;
	DType* templateData = GetDataForSym(templateClass, true, &foundViaAlias);
	if (!templateData)
		return FALSE;

	DType* resolved = ::TraverseUsing(templateData, m_mp.get());
	if (resolved && resolved != templateData)
	{
		// [case: 86670]
		templateData = resolved;
	}

	const WTString templateDataSymScope(templateData->SymScope());
	if (templateDataSymScope.Find(templateClass.c_str()) == -1)
	{
		// [case: 25945] GetDataForSym can resort to guess due to namespaces;
		// but if the guess doesn't even contain what we were looking for, punt.
		// [case: 57571] performance regression if this check is removed.
		// [case: 55626] simple Find fails; instead check to see if each portion is found
		token2 tok(templateClass);
		bool skip = false;
		WTString curItem;
		while (tok.more())
		{
			tok.read(DB_SEP_CHR, curItem);
			if (templateDataSymScope.Find(curItem) == -1)
			{
				skip = true;
				if (!foundViaAlias)
					break;
			}
			else if (foundViaAlias && skip)
			{
				// [case 1364] for namespace aliases, only skip if last item is not found
				skip = false;
			}
		}

		if (skip)
		{
			// we do not have a reproducible test case for big performance
			// hits when this return is not present.
			// case 57571 is a 12000 file solution with boost, Qt, stl and
			// many namespaces
			vLog("BCF::MTI skip guess: (%s) (%s) (%s)\n", templateClass.c_str(), symScope.c_str(),
			     templateDataSymScope.c_str());
			return FALSE;
		}
	}

	const WTString tmp(templateDataSymScope.Mid(1) + EncodeChar('<'));
	if (templateData->Def_sv().first.find(tmp) != -1)
	{
		// [case: 25945] we don't handle this recursion:
		// template< template <class> class T, typename TList >
		// struct TypeContainerImpl : public TypeContainerImpl<T, typename TList::Tail> { ... }
		// template<template <class> class T, typename Seq>
		// struct TypeContainer : public TypeContainerImpl<T, typename Seq::Type> { ... }
		vLog("BCF::MTI recurse: (%s) (%s) (%s)\n", symScope.c_str(), templateDataSymScope.c_str(),
		     templateData->Def().c_str());
		return FALSE;
	}

	templateClass = templateDataSymScope;
	DType* argData = m_mp->FindExact2((templateClass + DB_SEP_STR + "< >").c_str(), true, TEMPLATETYPE, false);
	if (!argData)
	{
		static const WTString emptyArgs("< >");
		argData = m_mp->FindSym(&emptyArgs, &templateClass, NULL);
	}

	if (!argData)
		return FALSE;

	if (argData->IsMarkedForDetach() || templateData->IsMarkedForDetach())
		mUsedDetachedData = true;

	const WTString newTemplateName(symScope);
	WTString argClasses = strchr(DecodeScope(newTemplateName).c_str(), '<');
	const WTString kOrigArgClasses(argClasses);
	bool encodedSpace = false;
	// encode template args
	{
		int intemp = 0;
		for (int i = 1; i < argClasses.GetLength(); i++)
		{
			char c = argClasses[i];
			if (c == '<')
				intemp++;
			if (c == ':')
				c = '.';
			if (intemp || strchr(" *\t&:.", c))
			{
				argClasses.SetAt(i, EncodeChar(c));
				if (c == ' ')
					encodedSpace = true;
			}
			if (c == '>')
				intemp--;
		}
	}

	if (encodedSpace && (-1 != argClasses.Find(kIn1) || -1 != argClasses.Find(kOut1.c_str())))
	{
		// [case: 63897] [case: 91733]
		// remove "in " and "out " from argClasses
		static const WTString sp = EncodeChar(' ');
		static const WTString cm = ",";
		static const WTString op = EncodeChar('<');
		static const WTString cmsp = cm + sp;
		static const WTString lt_in = "<" + kIn2;
		static const WTString lt_out = "<" + kOut2;
		static const WTString op_in = op + kIn2;
		static const WTString op_out = op + kOut2;
		static const WTString cm_in = cm + kIn2;
		static const WTString cm_out = cm + kOut2;
		static const WTString cmsp_in = cmsp + kIn2;
		static const WTString cmsp_out = cmsp + kOut2;

		argClasses.ReplaceAll(lt_in, "<", FALSE);
		argClasses.ReplaceAll(lt_out, "<", FALSE);
		argClasses.ReplaceAll(op_in, op, FALSE);
		argClasses.ReplaceAll(op_out, op, FALSE);
		argClasses.ReplaceAll(cm_in, cm, FALSE);
		argClasses.ReplaceAll(cm_out, cm, FALSE);
		argClasses.ReplaceAll(cmsp_in, cmsp, FALSE);
		argClasses.ReplaceAll(cmsp_out, cmsp, FALSE);
	}

	WTString argDataDef(argData->Def());
	// [case: 24739]
	MakeTemplateSafety makeSafe(mQuit);
	if (!makeSafe.Check(newTemplateName, templateClass, argDataDef, argClasses, templateData->FileId(),
	                    templateData->Line()))
		return FALSE;

	static const WTString monitorDef("BclMonitor");
	WTString monitorSym;
	token2 allargdata = argDataDef;
	LogElapsedTime let("BCF::MTI", 100);
	int argDataLoopCnt = 0;
	WTString argTemplate;
	for (; allargdata.more() && argDataLoopCnt < 20; ++argDataLoopCnt)
	{
		std::string_view allargdata_tok = allargdata.read_sv('\f');
		auto gt_i = allargdata_tok.find('<');
		if(gt_i != std::string_view::npos)
			argTemplate = allargdata_tok.substr(gt_i);
		else
			argTemplate.Clear();

		if (-1 != argTemplate.Find(kIn1) || -1 != argTemplate.Find(kOut1))
		{
			// [case: 63897]
			// remove "in " and "out " from argTemplate (C# modifiers)
			argTemplate.ReplaceAll("<in ", "<", FALSE);
			argTemplate.ReplaceAll("<out ", "<", FALSE);
			argTemplate.ReplaceAll(",in ", ",", FALSE);
			argTemplate.ReplaceAll(",out ", ",", FALSE);
			argTemplate.ReplaceAll(", in ", ", ", FALSE);
			argTemplate.ReplaceAll(", out ", ", ", FALSE);

			if (-1 != argTemplate.Find(kIn3) || -1 != argTemplate.Find(kOut3))
			{
				int reps = 0;
				reps += argTemplate.ReplaceAll(kIn1, "", TRUE);
				reps += argTemplate.ReplaceAll(kOut1, "", TRUE);
				_ASSERTE(!reps && "failed to strip some in/out params from template args - doing brute force removal");
			}
		}

		if (argTemplate.GetLength() == argClasses.GetLength())
		{
			if (argTemplate == argClasses || argTemplate == kOrigArgClasses)
			{
				vLog("BCF::MTI same params: (%s) (%s) (%s) (%s)\n", newTemplateName.c_str(), templateClass.c_str(),
				     argTemplate.c_str(), argClasses.c_str());
				continue;
			}
		}

		bool needToAdd = false;
		monitorSym.WTFormat("+BclMonitor_Template:%s;%s;%s;%s;%x;%d;", newTemplateName.c_str(), templateClass.c_str(),
		                    argTemplate.c_str(), argClasses.c_str(), templateData->FileId(), templateData->Line());
		DType* mon = g_pGlobDic->FindExactObj(monitorSym, 0, false);
		if (!mon || mon->IsMarkedForDetach())
		{
			// add using the fileId of the template
			// it will be removed if the file that the template is defined in gets reparsed
			g_pGlobDic->add(monitorSym, monitorDef, UNDEF, V_HIDEFROMUSER | V_TEMPLATE_ITEM, 0, templateData->FileId(),
			                templateData->Line());
			_ASSERTE(g_pGlobDic->FindExactObj(monitorSym, 0, false));
			needToAdd = true;
		}

		monitorSym.WTFormat("+BclMonitor_Template:%s;%s;%s;%s;%x;%d;", newTemplateName.c_str(), templateClass.c_str(),
		                    argTemplate.c_str(), argClasses.c_str(), argData->FileId(), argData->Line());
		mon = g_pGlobDic->FindExactObj(monitorSym, 0, false);
		if (!mon || mon->IsMarkedForDetach())
		{
			// also add using the fileId of the arg
			// it will be removed if the file that the arg is defined in gets reparsed
			g_pGlobDic->add(monitorSym, monitorDef, UNDEF, V_HIDEFROMUSER | V_TEMPLATE_ITEM, 0, argData->FileId(),
			                argData->Line());
			_ASSERTE(g_pGlobDic->FindExactObj(monitorSym, 0, false));
			needToAdd = true;
		}

		if (!needToAdd)
		{
			// [case: 20735] already exists, don't run FileDic::MakeTemplate again - is time consuming
			vLog("BCF::MTI skip: (%s) (%s) (%s) (%s)\n", newTemplateName.c_str(), templateClass.c_str(),
			     argTemplate.c_str(), argClasses.c_str());
			continue;
		}

#ifdef _DEBUG
		if (-1 != templateClass.Find(';') || -1 != newTemplateName.Find(';') || -1 != argTemplate.Find(';') ||
		    -1 != argClasses.Find(';'))
		{
			_ASSERTE(!"bad MakeTemplate logic");
		}
#endif

		m_mp->LDictionary()->MakeTemplate(templateClass, newTemplateName, argTemplate, argClasses);

		// [case: 142474]
		if (templateData->inproject() && argData->inproject() && !templateData->IsSysLib() && !argData->IsSysLib())
		{
			g_pGlobDic->MakeTemplate(templateClass, newTemplateName, argTemplate, argClasses);
		}
		else if (!templateData->inproject() && !argData->inproject() && templateData->IsSysLib() && argData->IsSysLib())
		{
			m_mp->SDictionary()->MakeTemplate(templateClass, newTemplateName, argTemplate, argClasses);
		}
		else
		{
			// Run maketemplate on both sys and proj db's for CArray fix.
			// Jer thinks it was related to the template arg's being either a global
			// project type or a local type defined in a source file, or a system type?
			// p4 change 3960
			// if(data->Type & V_SYSLIB)
			m_mp->SDictionary()->MakeTemplate(templateClass, newTemplateName, argTemplate, argClasses);
			// else
			g_pGlobDic->MakeTemplate(templateClass, newTemplateName, argTemplate, argClasses);
		}
	}

	if (argDataLoopCnt == 20)
	{
		// [case: 24739]
		vLog("WARN: BCF::MTI capped");
	}

	return TRUE;
}

WTString BaseClassFinder::GetTypesFromDef(DType* data)
{
	LogElapsedTime let("BCF::GTFD", 200);
	if (mQuit && *mQuit)
		return NULLSTR;

	WTString def(data->Def());
	const WTString key(data->SymScope());
	const uint dataType = data->MaskedType();
	if (Is_C_CS_File(mDevLang))
	{
		if (dataType == VAR || dataType == FUNC || dataType == CLASS || // [case: 67215]
		    dataType == TYPE)                                           // [case: 54439]
		{
			// working on parsing bug that uses macros in definitions like
			// MAKE_STR_TEMPLATE(CString) str; where it expands this into MyTemplate<CString> str;
			def = VAParseExpandMacrosInDef(m_mp, def);
		}
	}

	WTString originalDefaultTypes(::GetTypesFromDef(key, def, (int)data->MaskedType(), mDevLang));
	if (m_deep > 10)
	{
		// [case:18695] catch recursion
		return originalDefaultTypes;
	}

	if (originalDefaultTypes.IsEmpty() || DEFINE == dataType || DB_SEP_CHR != originalDefaultTypes[0] ||
	    key.ReverseFind(DB_SEP_CHR) < 1 ||
	    !(originalDefaultTypes.ReverseFind(DB_SEP_CHR) == 0 || strchr(originalDefaultTypes.c_str(), EncodeChar('>'))))
	{
		return originalDefaultTypes;
	}

	{
		const int pos = originalDefaultTypes.Find('\f');
		if (pos != originalDefaultTypes.GetLength() - 1)
		{
			if (false)
			{
				// [case: 112952]
				// it seems like this would make more sense rather than the bailout
				// of the else condition; the change breaks two unit tests:
				//	BaseClassFinder_test::testNamespaceAliasTemplates
				//	BaseClassFinder_test::testGenericInOutModifiers
				// I didn't investigate too much; it might make more sense than the bailout.
				originalDefaultTypes = originalDefaultTypes.Left(pos + 1);
			}
			else
			{
				// The original bail out is for case 57880 and case 1791
				return originalDefaultTypes;
			}
		}
	}

	int scc_index = def.Find(" ::");
	if (scc_index != -1)
	{
		scc_index = std::max(0, scc_index - 9);
		if (def.Find("public ::", scc_index) != -1 || def.Find("private ::", scc_index) != -1 || def.Find("protected ::", scc_index) != -1 || def.Find(", ::", scc_index) != -1)
		{
			// [case: 1791] don't mess with classes derived from global namespace classes
			return originalDefaultTypes;
		}
	}

	if (data->type() == VAR)
	{
		WTString strippedDef(def);
		strippedDef.ReplaceAll("const",                  "     ", TRUE);
		strippedDef.ReplaceAll("constexpr",              "         ", TRUE);
		strippedDef.ReplaceAll("consteval",              "         ", TRUE);
		strippedDef.ReplaceAll("constinit",              "         ", TRUE);
		strippedDef.ReplaceAll("_CONSTEXPR17",           "            ", TRUE);
		strippedDef.ReplaceAll("_CONSTEXPR20_CONTAINER", "                      ", TRUE);
		strippedDef.ReplaceAll("_CONSTEXPR20",           "            ", TRUE);
		strippedDef.ReplaceAll("static",                 "      ", TRUE);
		strippedDef.ReplaceAll("thread_local",           "            ", TRUE);
		strippedDef.ReplaceAll("extern",                 "      ", TRUE);
		strippedDef.ReplaceAll("typedef",                "       ", TRUE);
		strippedDef.TrimLeft();
		if (strippedDef.begins_with(':'))
		{
			strippedDef.MidInPlace(1);
			strippedDef.TrimLeft();
			if (strippedDef.begins_with(':'))
			{
				// [case: 85237]
				// We don't need to look for the type if it is explicitly global
				// const ::OuterCls foo;
				return originalDefaultTypes;
			}
		}
	}

	// see if the type returned comes from a namespace in our bcl;
	// if so, need to qualify it

	WTString qualifiedTypes;
	WTString theType(originalDefaultTypes.Left(originalDefaultTypes.GetLength() - 1));
	if (theType.IsEmpty())
		return originalDefaultTypes;

	WTString workingKey(key);
	WTString templateScope;
	const bool isTemplate = strchr(originalDefaultTypes.c_str(), EncodeChar('>')) != NULL;
	const bool isDeepTemplate = isTemplate && originalDefaultTypes.ReverseFind(DB_SEP_CHR) != 0;
	if (isDeepTemplate)
	{
		// [case: 9859] qualify the template args
		const int openPos = originalDefaultTypes.Find(EncodeChar('<'));
		const int closePos = originalDefaultTypes.ReverseFind(EncodeChar('>'));
		if (-1 == openPos || -1 == closePos)
			return originalDefaultTypes;

		if (closePos != (originalDefaultTypes.GetLength() - 2))
			return originalDefaultTypes;

		theType = originalDefaultTypes.Mid(openPos + 1, closePos - openPos - 1);
		DecodeScopeInPlace(theType);
		if (theType[0] != DB_SEP_CHR)
			theType.prepend(DB_SEP_CHR);
		templateScope = originalDefaultTypes.Left(openPos);
	}
	else if (isTemplate)
		QualifyTemplateArgs(key, originalDefaultTypes, theType);

	// ensure whole word match by searching for :type:
	_ASSERTE(theType[0] == DB_SEP_CHR);
	const bool theTypeIsTerminated = theType[theType.GetLength() - 1] == DB_SEP_CHR;
	const int posOfTypeInKey =
	    WTString(workingKey + DB_SEP_STR).Find(theTypeIsTerminated ? theType : theType + DB_SEP_STR);
	if (-1 == posOfTypeInKey)
	{
		int delimPos;
		WTString newType;
		while ((delimPos = workingKey.ReverseFind(DB_SEP_CHR)) != -1 && delimPos)
		{
			newType = workingKey.left_sv((uint32_t)delimPos);
			if (newType.Find('-') == -1 && -1 == theType.Find(newType))
			{
				newType += theType;
				BaseClassFinder bcf(mDevLang);
				bcf.m_deep = m_deep;
				bcf.GetBaseClassList(m_mp, newType);
				DType* pDat = bcf.GetDataForSym(newType, true);
				if (pDat && pDat->SymScope() == newType)
				{
					if (pDat->IsMarkedForDetach())
						mUsedDetachedData = true;

					if (isDeepTemplate)
					{
						if (DB_SEP_CHR == newType[0])
							newType.MidInPlace(1);
						newType.ReplaceAll(':', '.');
						newType.surround('<', '>');
						EncodeScopeInPlace(newType);
						qualifiedTypes = {templateScope, newType, "\f"};
					}
					else
						qualifiedTypes = {newType, "\f"};

					return qualifiedTypes;
				}
			}

			workingKey.LeftInPlace(delimPos);
		}

		if (theType[0] == DB_SEP_CHR && -1 == def.Find("::" + theType))
		{
			// [case: 85237]
			// similar to block of code in ResolveTypeStr
			WTString typeScope(DB_SEP_STR);
			WTString tmpBcl = data->Scope();
			WTString searchBcl = tmpBcl;
			WTString scp(tmpBcl), tmp;
			while (!scp.IsEmpty() && scp != DB_SEP_STR)
			{
				tmp = m_mp->GetBaseClassList(scp);
				if (tmp.IsEmpty() || tmp == "\f")
					break;

				if (-1 == tmp.Find(kWildCardScopeStr))
				{
					searchBcl = {tmp};
					tmp.ReplaceAll("\f", "");
					tmp.ReplaceAll(DB_SEP_STR, "");
					if (tmp.IsEmpty())
						searchBcl = {tmpBcl};
					break;
				}

				scp.MidInPlace(StrGetSymScope_sv(scp));
			}

			for (;;)
			{
				tmpBcl.MidInPlace(StrGetSymScope_sv(tmpBcl));
				if (tmpBcl.IsEmpty() || tmpBcl == DB_SEP_STR)
					break;

				searchBcl += {"\f", tmpBcl};
			}

			tmp = theType.Mid(1);
			DType* foundType = m_mp->FindSym(
			    &tmp, &typeScope, &searchBcl,
			    FDF_NoConcat | /*FDF_IgnoreGlobalNamespace |*/ FDF_NoNamespaceGuess /*| FDF_SlowUsingNamespaceLogic*/);
			if (foundType)
			{
				if (foundType->IsMarkedForDetach())
					mUsedDetachedData = true;

				if (isDeepTemplate)
				{
					tmp = foundType->SymScope_sv().first; // gmit: this was an old bug?!
					tmp.ReplaceAll(':', '.');
					tmp.surround('<', '>');
					EncodeScopeInPlace(tmp);
					qualifiedTypes = { templateScope + tmp + "\f" };
				}
				else
					qualifiedTypes = { foundType->SymScope_sv().first, "\f" };

				return qualifiedTypes;
			}
		}

		return originalDefaultTypes;
	}
	else if (0 != posOfTypeInKey)
	{
		// [case 1666] better scoping of duplicate types in different namespaces
		workingKey.LeftInPlace(posOfTypeInKey + theType.GetLength());
		WTString &newType = workingKey;
		if (isDeepTemplate)
		{
			if (DB_SEP_CHR == newType[0])
				newType.MidInPlace(1);
			newType.ReplaceAll(':', '.');
			newType.surround('<', '>');
			EncodeScopeInPlace(newType);
			qualifiedTypes = {templateScope, newType, "\f"};
		}
		else
			qualifiedTypes = { newType, "\f" };

		return qualifiedTypes;
	}

	return originalDefaultTypes;
}

static ThreadStatic<int> sDataForSymRecurseCnt;

DType* BaseClassFinder::GetDataForSym(const WTString& symScope, bool exactOnly /*= false*/,
                                      bool* foundViaAlias /*= NULL*/)
{
	LogElapsedTime let("BCF::GDS", symScope, 200);
	DType* data = m_mp->FindExact2(symScope);
	if (!data && mSymScopeList.size() > 1)
	{
		// look in scope previous scope
		WTString lscope = StrGetSymScope(mSymScopeList[mSymScopeList.size() - 2]);
		WTString symname;
		while (!data && lscope.GetLength())
		{
			symname = {lscope, symScope};
			data = m_mp->FindExact2(symname);
			lscope.MidInPlace(StrGetSymScope_sv(lscope));
		}
	}

	if (data && data->IsConstructor()) // Get the class, not the constructor
		data = m_mp->FindExact2(StrGetSymScope(data->SymScope()));

	if (!data && !exactOnly)
	{
		// exact scope not found, look to see if it is a local type in a namespace or class
		const WTString templateArgs = strchr(symScope.c_str(), EncodeChar('>'));
		if (!templateArgs.IsEmpty())
		{
			// [case: 16702]
			// if is a template, don't search everywhere - force template instance creation
			return NULL;
		}

		token2 tscope = symScope;
		WTString lscope(DB_SEP_STR);
		WTString lbcl(DB_SEP_STR);
		WTString cls;
		// passed foo.bar.baz, find any type foo, then with in it's bcl or scope find bar and so on...
		while (tscope.more() && m_mp)
		{
			if (mQuit && *mQuit)
				return NULL;

			tscope.read(DB_SEP_STR2, cls);
			data =
			    m_mp->FindSym(&cls, &lscope, &lbcl, FDF_TYPE | /*FDF_IgnoreGlobalNamespace |*/ FDF_NoAddNamespaceGuess);
			if (!data) // Look for the class in all namespaces/scopes
			{
				// Try one more variation before pulling a random guess
				WTString orgScope = mSymScopeList.size() ? mSymScopeList[0] : NULLSTR;
				data = m_mp->FindSym(&cls, &orgScope, &lbcl, FDF_TYPE | FDF_NoAddNamespaceGuess);

				if (!data && !strchr(StrGetSym(symScope), EncodeChar('>')))
					data = m_mp->FindSym(&cls, &orgScope, &lbcl, FDF_TYPE | FDF_GUESS | FDF_NoAddNamespaceGuess);
			}
			if (data && data->MaskedType() == NAMESPACE && !tscope.more())
				data = NULL; // Don't return namespaces.
			if (data)
			{
				lscope = data->SymScope();
				BaseClassFinder bclLooker(mDevLang);
				WTString bcl = bclLooker.GetBaseClassList(m_mp, data->SymScope(), FALSE);
				if (bclLooker.mUsedDetachedData)
					mUsedDetachedData = true;
				if (bcl.GetLength())
					lbcl = bcl;
			}
			else
			{
				break;
			}
		}
	}
	else if (!data) // exactOnly
	{
		// use this to search collected namespaces without resorting to guesses
		token2 tscope = symScope;
		WTString scope(DB_SEP_STR);
		const WTString bcl(DB_SEP_STR);
		WTString cls = tscope.read(DB_SEP_STR2);
		data = m_mp->FindSym(&cls, &scope, &bcl, FDF_TYPE);
		if (!data)
		{
			// [case: 1364] local namespace alias?
			scope = ::StrGetSymScope(mInitialSymscope);
			if (!scope.IsEmpty() && scope != DB_SEP_STR)
			{
				data = m_mp->FindSym(&cls, &scope, &bcl, FDF_TYPE);
				if (data && data->MaskedType() != NAMESPACE)
					data = NULL;
			}
		}

		if (data && data->MaskedType() == NAMESPACE && tscope.more() > 1)
		{
			if (-1 != data->Def().Find('='))
			{
				// [case: 1364] resolve namespace alias
				data = ::TraverseUsing(data, m_mp.get(), true);
				if (data)
				{
					cls = data->SymScope() + DB_SEP_STR + tscope.read();

					// [case: 57880] don't recursively call to get the same symScope
					if (cls != symScope)
					{
						// recursive call for new scope generated from alias resolution
						if (sDataForSymRecurseCnt() < 5)
						{
							++sDataForSymRecurseCnt();
							data = GetDataForSym(cls, exactOnly);
							--sDataForSymRecurseCnt();
							if (data && foundViaAlias)
								*foundViaAlias = true;
						}
						else
						{
							vLog("WARN: BCF::GDS skip recursion for %s %s", symScope.c_str(), cls.c_str());
						}
					}
				}
			}
			else
			{
#ifdef _DEBUG
				// [case: 56965]
				WTString ns(m_mp->GetNameSpaceString(data->SymScope()));
				if (ns.GetLength())
				{
					// iterate over /f delimited list
				}
#endif // _DEBUG
			}
		}
	}

	auto oldData = data;
	data = ::TraverseUsing(oldData, m_mp.get());
	if (data && data->IsMarkedForDetach())
		mUsedDetachedData = true;

	if (oldData != data && foundViaAlias)
		*foundViaAlias = true;

	return data;
}

WTString BaseClassFinder::GetCacheLookup(const WTString& symScope) const
{
	WTString bclCacheLookup(kBclCachePrefix);
	if (mInitialSymIsTemplate && mInitialSymscope != symScope)
		bclCacheLookup += {mInitialSymscope, symScope};
	else
		bclCacheLookup += symScope;

	if (mInitialDat && mInitialDat->FileId() && (symScope == mInitialDat->SymScope_sv().first))
	{
		// [case: 67831] make cache unique by appending fileId
		bclCacheLookup += _T('-') + ::itos((int)mInitialDat->FileId());
//		bclCacheLookup.WTAppendFormat("-%u", mInitialDat->FileId());
	}

	return bclCacheLookup;
}

BOOL BaseClassFinder::LoadFromCache(const WTString& cacheLookup, const WTString& symScope, DType*& data)
{
	// see if it is already cached
	data = GetCachedBclDtype(cacheLookup);
	if (!data)
		return FALSE;

	const WTString defs(data->Def());
	if (defs == kInvalidCache)
		return FALSE;

	token2 t(defs);
	WTString cls;
	while (t.more())
	{
		t.read('\f', cls);
		if (!cls.IsEmpty())
		{
			if (!::VectorContainsExact(mBaseClassList, cls))
				mBaseClassList.push_back(cls);
		}
	}

	bool doAdd = true;
	for (WTStringV::iterator it = mBaseClassList.begin(); it != mBaseClassList.end(); ++it)
	{
		const WTString& cur(*it);
		if (-1 != cur.Find(symScope))
		{
			doAdd = false;
			break;
		}
	}

	if (doAdd)
	{
		// [case: 73444]
		// sometimes we were returning TRUE that symScope was in cache
		// while it wasn't in the current bcl.  The token2 while loop above
		// does not add it (symScope) to bcl but does add its bcl to this bcl.
		mBaseClassList.push_back(symScope);
	}

	return TRUE;
}

DType* BaseClassFinder::GetCachedBclDtype(const WTString& cacheLookup)
{
	DType* data = m_mp->FindCachedBcl(cacheLookup);
	if (!data)
		return nullptr;

	if (data->IsMarkedForDetach())
		mUsedDetachedData = true;
	return data;
}

class IncDeepCls
{
  public:
	int* pDeep;
	IncDeepCls(int& deep)
	{
		pDeep = &deep;
		pDeep[0]++;
	}
	~IncDeepCls()
	{
		pDeep[0]--;
	}
};

WTString BaseClassFinder::GetBaseClassList(const WTString& symScope)
{
	IncDeepCls DD(m_deep);
	if (mQuit)
	{
		vCatLog("Parser.BaseClassFinder", "GetBaseClassList %d %s %d", m_deep, symScope.c_str(), *mQuit);
	}
	else
	{
		vCatLog("Parser.BaseClassFinder", "GetBaseClassList %d %s", m_deep, symScope.c_str());
	}

	if (symScope.IsEmpty())
		return NULLSTR;

	const WTString cacheLookup(GetCacheLookup(symScope));
	DType* cachedBcl = nullptr;

	if (m_force)
		cachedBcl = GetCachedBclDtype(cacheLookup);
	else if (LoadFromCache(cacheLookup, symScope, cachedBcl))
		return NULLSTR;

	if (m_deep > 30)
	{
		VALOGERROR((WTString("BCL deepError: ") + symScope).c_str());
		ASSERT_ONCE(!"BCL DeepError");
		return NULLSTR;
	}

	if (mQuit && *mQuit)
		return NULLSTR;

	if (IsRecursive(symScope))
		return NULLSTR;

	if (symScope[0] == ':')
	{
		switch (symScope[1])
		{
			// 		case 'c':
			// 			if (symScope.GetLength() == 5 && symScope == ":char")
			// 			{
			// 				mSymScopeList.push_back(symScope);
			// 				const WTString bcl("\f:char\f:\f");
			// 				CreateOrUpdateCacheEntry(cachedBcl, cacheLookup, bcl, DMain);
			// 				return bcl;
			// 			}
			// 			break;
			// 		case 'd':
			// 			if (symScope.GetLength() == 7 && symScope == ":double")
			// 			{
			// 				mSymScopeList.push_back(symScope);
			// 				const WTString bcl("\f:double\f:\f");
			// 				CreateOrUpdateCacheEntry(cachedBcl, cacheLookup, bcl, DMain);
			// 				return bcl;
			// 			}
			// 			break;
			// 		case 'i':
			// 			if (symScope.GetLength() == 4 && symScope == ":int")
			// 			{
			// 				mSymScopeList.push_back(symScope);
			// 				const WTString bcl("\f:int\f:\f");
			// 				CreateOrUpdateCacheEntry(cachedBcl, cacheLookup, bcl, DMain);
			// 				return bcl;
			// 			}
			// 			break;
		case 's':
			if (symScope.GetLength() == 4 && symScope == ":std")
			{
				mSymScopeList.push_back(symScope);
				// hardcode std bcl due to all the messy macros that we can just skip
				const WTString bcl("\f:std\f:\f");
				CreateOrUpdateCacheEntry(false, cachedBcl, cacheLookup, bcl, DMain);
				return bcl;
			}
			break;
		case 'Q':
			if (symScope.GetLength() == 14)
			{
				if (symScope == ":Q_CORE_EXPORT" || symScope == ":Q_DECL_IMPORT" || symScope == ":Q_DECL_EXPORT")
					return NULLSTR;
			}
			break;
		}
	}
	else
	{
		if (wt_isdigit(symScope[0]))
		{
			// [case: 140843]
			vLog("  digit bc sym\n");
			return NULLSTR;
		}
	}

	mSymScopeList.push_back(symScope);
	// Set cache to null so we don't recurse
	cachedBcl = CreateOrUpdateCacheEntry(true, cachedBcl, cacheLookup, DB_SEP_STR, DLoc);

	const size_t begListPos = mBaseClassList.size();

	// Get data for symScope
	DTypePtr dataBak;
	DType* data;
	if (mInitialDat && mInitialDat->SymScope() == symScope)
		data = mInitialDat;
	else
		data = GetDataForSym(symScope);
	if (!data)
	{
		if (strchr(symScope.c_str(), EncodeChar('>')))
		{
			const WTString sym(StrGetSym(symScope));
			if (strchr(sym.c_str(), EncodeChar('>')))
			{
				// maketemplate
				if (MakeTemplateInstance(symScope))
					data = m_mp->FindExact2(symScope.c_str());
			}
			else
			{
				const WTString symDefScope(StrGetSymScope(symScope));
				if (InstantiateTemplateForSymbolScope(symDefScope))
					data = m_mp->FindExact2(symScope.c_str());
			}
		}
	}

	if (mQuit && *mQuit)
	{
		InvalidateCache(cacheLookup);
		return NULLSTR;
	}

	if (data)
	{
		// Moved from GetDataForSym to prevent recursion. case:17563
		// In c#, classes have properties of the same name as the class
		// class foo{ String String{}; };  // Need to change foo.String into System.String
		if (data->MaskedType() == PROPERTY)
		{
			WTString propDef = GetTypesFromDef(data);
			if (!propDef.GetLength())
			{
				WTString lscope = data->SymScope();
				BaseClassFinder bclLooker(mDevLang);
				bclLooker.m_deep = m_deep;
				WTString cls = StrGetSym(symScope);
				WTString bcl = bclLooker.GetBaseClassList(m_mp, lscope, FALSE, mQuit);
				if (bclLooker.mUsedDetachedData)
					mUsedDetachedData = true;
				if (mQuit && *mQuit)
				{
					InvalidateCache(cacheLookup);
					return NULLSTR;
				}

				WTString colonStr(DB_SEP_STR);
				DType* typeData = m_mp->FindSym(&cls, &colonStr, &bcl, FDF_TYPE); // getting rid of Tdef
				if (typeData)
					data = typeData;
			}
		}
		if (data->IsType() || mDevLang == JS || Is_Tag_Based(mDevLang))
		{
			if (data->MaskedType() == NAMESPACE)
			{
				// class with same name of namespace
				DType* dcls = m_mp->FindExact2(data->SymScope() + DB_SEP_STR + StrGetSym(data->SymScope()));
				if (dcls && dcls->MaskedType() != NAMESPACE && IS_OBJECT_TYPE(dcls->MaskedType()))
					data = dcls;
			}

			const WTString dss(data->SymScope());
			if (::VectorContainsExact(mBaseClassList, dss))
				; // [case: 64484] "return NULLSTR;" breaks some coloring
			else
				mBaseClassList.push_back(dss);
		}
		token2 tdef = "";
		try
		{
			if (data->MaskedType() == LINQ_VAR && (m_mp->FileType() == CS || IsCFile(m_mp->FileType())))
			{
				dataBak = std::make_shared<DType>(data);
				dataBak = InferTypeFromAutoVar(dataBak, m_mp, m_mp->FileType(), TRUE);
				if (dataBak)
					data = dataBak.get();
			}

			tdef = GetTypesFromDef(data);
			if (!tdef.GetLength() &&
			    (data->MaskedType() == CLASS || data->MaskedType() == STRUCT || data->MaskedType() == C_INTERFACE))
				tdef = data->SymScope();
			if (!tdef.GetLength())
			{
				// property with same name as type
				DType* orgType = GetDataForSym(DB_SEP_STR + StrGetSym(symScope));
				if (orgType && orgType != data)
				{
					if (orgType->IsMarkedForDetach())
						mUsedDetachedData = true;

					if (orgType->IsType())
					{
						const WTString oss(orgType->SymScope());
						if (::VectorContainsExact(mBaseClassList, oss))
							; // [case: 64484] "return NULLSTR;" breaks some coloring
						else
							mBaseClassList.push_back(oss);
						tdef = GetTypesFromDef(orgType);
					}
				}
			}

			// See if there is also a definition in SysLib
			if (!data->IsSysLib())
			{
				DType* sysData = m_mp->SDictionary()->Find(data->SymScope());
				if (sysData)
					tdef += GetTypesFromDef(sysData);
			}

			if (data && data->IsMarkedForDetach())
				mUsedDetachedData = true;
		}
		catch (const WtException& err)
		{
			WTString msg("BCF::GBCL exception caught : ");
			msg += err.GetDesc();
			VALOGEXCEPTION(msg.c_str());
			Log(msg.c_str());
			return NULLSTR;
		}

		WTString sym;
		while (tdef.more() > 1)
		{
			tdef.read('\f', sym);
			GetBaseClassList(sym);
			if (mQuit && *mQuit)
			{
				InvalidateCache(cacheLookup);
				return NULLSTR;
			}
		}

		if (m_mp->FileType() == CS)
		{
			if (data->IsType())
			{
				// HANDLE default types, "class foo" automatically inherits Object"
				if ((data->MaskedType()) == C_ENUM)
					GetBaseClassList(DBColonToSepStr(":System:Enum"));
				else if ((data->MaskedType()) == DELEGATE)
					GetBaseClassList(DBColonToSepStr(":System:Delegate"));
				else if ((data->MaskedType()) == CLASS)
					GetBaseClassList(DBColonToSepStr(":System:Object"));
				else if ((data->MaskedType()) == C_INTERFACE)
					GetBaseClassList(DBColonToSepStr(":System:Object"));
			}
			else
			{
				if (data->Def().contains("["))
				{
					// HANDLE arrays int[] ary; ary.Length;
					GetBaseClassList(DBColonToSepStr(":System:Array"));
					GetBaseClassList(DBColonToSepStr(":System:Linq:Enumerable")); // Enumerable Arrays in VS2008 Linq
				}
			}
		}
	}
	else if (mDevLang != JS)
	{
		bool addWildcard = true;
		if (Psettings && Psettings->mUnrealEngineCppSupport && 1 == symScope.GetTokCount(':') &&
		    symScope.Find("_API") == symScope.GetLength() - 4)
		{
			if (::StrIsUpperCase(symScope))
			{
				// [case: 114494]
				// global symbol that ends with _API.
				// assume is macro with no body, as in:
				// class MYGAME_API MyClass...
				// ignore missing symbol and do not add WildCard
				addWildcard = false;
			}
		}

		if (addWildcard)
		{
			// add scope to bcl list
			if (::VectorContainsExact(mBaseClassList, kWildCardScopeStr))
				return NULLSTR;
			mBaseClassList.push_back(kWildCardScopeStr);
		}
	}

	if (mQuit && *mQuit)
	{
		InvalidateCache(cacheLookup);
		return NULLSTR;
	}

	// save to cache
	WTString bcl = "\f";
	int count = 0;
	for (size_t i = begListPos; count < ScopeHashAry::MAX_LAYER && i < mBaseClassList.size(); i++)
	{
		// cheezy uniq
		WTString bc = WTString(mBaseClassList[i]) + '\f';
		if (bcl.find(WTString("\f") + bc) == -1)
		{
			bcl += bc;
			count++;
		}
	}

	DictionaryType dbType = DMain;
	if (!data ||
	    // Store locally in C# since "Type" could be different in different files
	    m_mp->FileType() == CS ||
	    // [case: 15921] fix for identically named local vars in different identically named functions
	    data->HasLocalFlag() || mUsedDetachedData)
	{
		dbType = DLoc;
	}
	else if (m_mp->FileType() == Src)
	{
		if (-1 != bcl.Find('\xe'))
		{
			// [case: 62858]
			EdCntPtr ed(g_currentEdCnt);
			if (ed && ed->GetParseDb() == m_mp)
				dbType = DLoc;
		}
	}

	if (data && !m_mp->GetFileID())
	{
		// [case: 91437]
		// bcl cache failed to be cleared due to unset fileid in m_mp
		CreateOrUpdateCacheEntry(false, cachedBcl, cacheLookup, bcl, dbType, data->FileId());
	}
	else if (data && m_mp->GetFileID() != data->FileId())
	{
		// #seanPossibleBclCacheTrouble
		// this is a situation where we might consider changing the old behavior and
		// pass in FileId and Line from data rather than use fileId and line of m_mp.
		// This behavior means that bcl cache will not be updated if the symbol defined
		// in data->FileId() is modified but m_mp->GetFileID() is not modified.
		// The bcl cache could be incorrect because the bcl for the sym was associated
		// with the fileId that the user was (assumed) to be working in rather than
		// where the sym was defined.
		CreateOrUpdateCacheEntry(false, cachedBcl, cacheLookup, bcl, dbType);
	}
	else
		CreateOrUpdateCacheEntry(false, cachedBcl, cacheLookup, bcl, dbType);

	return bcl;
}

WTString BaseClassFinder::GetBaseClassList(MultiParsePtr mp, DType* pDat, const WTString& symScope,
                                           BOOL force /*= FALSE*/, volatile const INT* bailMonitor /*= NULL*/)
{
	m_mp = mp;
	m_force = force;
	mQuit = bailMonitor;
	mInitialSymscope = symScope;
	mInitialDat = pDat;
	static const char kEndTemp = EncodeChar('<');
	if (-1 != mInitialSymscope.Find(kEndTemp))
		mInitialSymIsTemplate = true;

	if (!force)
	{
		const WTString cacheLookup(GetCacheLookup(mInitialSymscope));
		DType* data = m_mp->FindCachedBcl(cacheLookup);
		if (data)
		{
			const WTString defs(data->Def());
			if (defs != kInvalidCache)
			{
				if (data->IsMarkedForDetach())
					mUsedDetachedData = true;

				return defs;
			}
		}
	}

	Reset();
	return GetBaseClassList(mInitialSymscope);
}

DType* BaseClassFinder::CreateOrUpdateCacheEntry(bool init, DType* previousCache, const WTString& cacheLookup,
                                                 const WTString& bcl, DictionaryType dt, uint fileId /*= 0*/)
{
	uint attrs = V_HIDEFROMUSER;
	switch (dt)
	{
	case DMain:
		// set V_INPROJECT so that cache updates know which hashtable the found DType came from
		attrs |= V_INPROJECT;
		break;
	case DLoc:
		// set V_INFILE so that cache updates know which hashtable the found DType came from
		attrs |= V_INFILE;
		break;
	default:
		// if this assert fires, then need to fix attrs and handling of previousCache may need to be revisited
		_ASSERTE(DMain == dt || DLoc == dt);
	}

	if (!fileId)
		fileId = m_mp->GetFileID();

	DType* retval = nullptr;
	if (previousCache)
	{
		_ASSERTE((previousCache->inproject() && !previousCache->infile()) ||
		         (!previousCache->inproject() && previousCache->infile()));

		if (DMain == dt)
		{
			// this condition only occurs during update when a cached item already exists
			_ASSERTE(!init);
			if (!previousCache->inproject())
			{
				// mismatch between dt and previousCache dt, don't reuse it.
				// the cachedBcl is DLoc, leave it as is.
				// DMain gets priority over DLoc in GetCachedBclDtype.
				previousCache = nullptr;
			}
		}
		else if (DLoc == dt)
		{
			// this block could be for init or for update, but in either case, a
			// cache entry already exists
			if (!previousCache->infile())
			{
				// mismatch between dt and previousCache dt, don't reuse it.

				// since previous cache is from DMain, check to see if we also have a DLoc item (as DMain gets priority
				// over DLoc)
				DType* oldLocalCache;
				if (m_mp->FileType() == Src || m_mp->FileType() == UC)
					oldLocalCache =
					    m_mp->LocalHcbDictionary()->FindExact(cacheLookup, 0, false, CachedBaseclassList, false);
				else
					oldLocalCache = m_mp->LDictionary()->FindExact(cacheLookup, 0, CachedBaseclassList, false);

				if (init)
				{
					// if init is true, then we got here due to m_force being
					// set and found a previous bcl entry in DMain.

					// return DMain cache entry since it will likely be updated
					retval = previousCache;

					if (oldLocalCache)
					{
						// prevent dupe of DLoc entry
						oldLocalCache->SetDef(bcl);
						return retval;
					}
				}
				else
				{
					// we are attempting to update DLoc item when a DMain item exists.
					// old logic didn't delete or modify DMain in this situation.
					// I don't know if it is common or even possible.
					// I contemplated calling SetDef(kInvalidCache) but decided
					// against it because it would have been a change in behavior.
					// An update to DLoc won't be seen since cache lookups give
					// priority to DMain.

					if (oldLocalCache)
					{
						// prevent dupe of DLoc entry
						vLog("WARN: BCL cache mgmt 1");
						oldLocalCache->SetDef(bcl);
						return nullptr; // only the init call uses the returned DType
					}

					vLog("WARN: BCL cache mgmt 2");
				}

				previousCache = nullptr;
			}
			else
			{
				// reuse cache, no problem for either init or update
			}
		}

		if (previousCache)
		{
			// we are going to reuse the previousCache
			if (previousCache->FileId() != fileId)
				previousCache->SetFileId(fileId);

			previousCache->SetDef(bcl);

			if (init)
				return previousCache;
			else
				return nullptr; // only the init call uses the returned DType
		}
	}

	m_mp->add(cacheLookup, bcl, dt, CachedBaseclassList, attrs, 0, fileId, -1);
	if (init)
	{
		if (retval)
		{
			// return the cached DMain entry
			return retval;
		}

		// if we created a DType to init cache, return it so that it can be used in
		// potential call to update it.
		// Note that the returned item might not actually be the created item, since
		// the created item was DLoc and the lookup gives preference to DMain.
		// That's ok though, since if in DMain, more likely that it will be the one
		// to be updated after this init.
		return GetCachedBclDtype(cacheLookup);
	}

	return nullptr; // only the init call uses the returned DType
}

bool BaseClassFinder::InstantiateTemplateForSymbolScope(const WTString& symbolScope)
{
	// creating type from an uninstantiated template per these cases:
	// case=12098:		foo<bar>::typedef objInstance;
	//					need to make sure that foo<bar> is created
	// case=11842:		foo< bar<bah, bah>, baz<hum, bug> >::typedef objInstance;
	//					need to make sure that these are created:
	//						bar<bah, bah>
	//						baz<hum, bug>
	//						foo< bar<bah, bah>, baz<hum, bug> >

	bool retval = false;
	WTString curItem;
	const WTString decodedScope(::DecodeScope(symbolScope));
	const int templateCount = decodedScope.GetTokCount('<');

	if (1 == templateCount)
	{
		if (MakeTemplateInstance(symbolScope))
			retval = true;
		return retval;
	}

	// more than one template; need to handle sub-templates

	// build list of '<' positions
	std::vector<int> startPositions;
	int pos = 0;
	for (int idx = 0; idx < templateCount; ++idx, ++pos)
	{
		const char* templateOpen = "<";
		pos = decodedScope.Find(templateOpen, pos);
		_ASSERTE(pos != -1);
		startPositions.push_back(pos);
	}

	// iterate over position list to produce list of sub-templates
	std::list<WTString> subTemplates;
	for (std::vector<int>::iterator it = startPositions.begin(); it != startPositions.end(); ++it)
	{
		int startPos, endPos;
		int curPos = *it;
		int inTemp = 1;

		// inc forward to matching '>' allowing for nesting
		for (endPos = curPos + 1; endPos < decodedScope.GetLength(); endPos++)
		{
			const char curCh = decodedScope[endPos];
			if (curCh == '<')
				inTemp++;
			else if (curCh == '>')
				inTemp--;

			if (!inTemp)
			{
				endPos++;
				break;
			}
		}

		// back up from cur pos to prev ",:>" allowing for nesting
		for (startPos = curPos - 1; startPos >= 0; startPos--)
		{
			const char curCh = decodedScope[startPos];
			if (curCh == '<')
				inTemp--;
			else if (curCh == '>')
				inTemp++;
			else if (!startPos)
				break;
			else if (!inTemp && strchr(",>\f", curCh))
			{
				startPos++;
				break;
			}
		}

		if (-1 == startPos)
			startPos = 0;

		curItem = decodedScope.Mid(startPos, endPos - startPos);
		curItem.Trim();
		if (curItem[0] != DB_SEP_CHR)
			curItem.prepend(DB_SEP_STR.c_str());

		// template args use . instead of : for name qualification
		// restore for non-arg sub-template (see EncodeTemplates and MakeTemplateInstance)
		for (curPos = 0; curItem[curPos] != '<'; curPos++)
		{
			if (curItem[curPos] == '.')
				curItem.SetAt(curPos, ':');
		}

		curItem = ::EncodeScope(curItem);

		// add string to template list
		if (curItem.GetTokCount(EncodeChar('<')) == 1)
			subTemplates.push_front(curItem); // instantiate non-nested templates before nested ones
		else
			subTemplates.push_back(curItem); // nested templates should be instantiated after ones that aren't nested
	}

	// iterate over sub-template list to instantiate
	for (std::list<WTString>::iterator it = subTemplates.begin(); it != subTemplates.end(); ++it)
	{
		if (mQuit && *mQuit)
			return retval;

		curItem = *it;
		DType* data = GetDataForSym(curItem);
		if (data || MakeTemplateInstance(curItem))
			retval = true;

		if (data && data->IsMarkedForDetach())
			mUsedDetachedData = true;
	}

	return retval;
}

bool BaseClassFinder::IsRecursive(const WTString& bc) const
{
	if (::VectorContainsExact(mSymScopeList, bc))
	{
		// prevent hang in case=3344 - bcl for IDispatch is fucked up
		vLog("BCL recursionError:  %s", bc.c_str());
		return true;
	}

	// Added '<' for  Case 950: so we do not recurse templates like
	//	template <class x> class T : public T<x::Base>
	// but do not flag the following for recursion (it is nested, not recursive):
	//	std::pair<long, std::pair<long, long> >
	static const WTString lt_encoded_str(EncodeChar('<'));
	const WTString searchFor(::TokenGetField(bc, lt_encoded_str) + lt_encoded_str);
	if (!::VectorContainsPartial(mSymScopeList, searchFor))
		return false; // no match at all, safe

	// there was a match, but it is not necessarily recursive; need to iterate through list
	const size_t listLen = mSymScopeList.size();
	for (size_t idx = 0; idx < listLen; ++idx)
	{
		const WTString curItem = mSymScopeList[idx];
		int pos = curItem.FindNoCase(searchFor);
		if (-1 == pos)
			continue;

		if (bc.GetLength() > curItem.GetLength())
			continue;

		// this is a match - now see if is recursive
		if (0 != pos)
		{
			// may need to revisit this; for now, taking the easiest route of
			// maintaining the old behavior
			return true;
		}

		// made the recursion check less stringent for case=11842
		const WTString decodedCurItem(::DecodeScope(curItem));
		WTString decodedSearchFor(::DecodeScope(searchFor));
		if (decodedSearchFor[0] == ':')
			decodedSearchFor.SetAt(0, ' ');
		decodedSearchFor.ReplaceAll(":", ".");
		pos = decodedCurItem.FindNoCase(decodedSearchFor, pos + 1);
		if (-1 == pos)
			return true;
	}

	return false;
}

void BaseClassFinder::QualifyTemplateArgs(const WTString& key, WTString& originalDefaultTypes, WTString& workingType)
{
	const int openPos = originalDefaultTypes.Find(EncodeChar('<'));
	const int closePos = originalDefaultTypes.ReverseFind(EncodeChar('>'));
	if (-1 == openPos || -1 == closePos || closePos != (originalDefaultTypes.GetLength() - 2))
		return;

	WTString keyScope(StrGetSymScope(key));
	if (keyScope.IsEmpty() || keyScope[0] != DB_SEP_CHR)
		return;
	keyScope = keyScope.Mid(1);
	int firstSepPos = keyScope.Find(DB_SEP_STR);
	// support multiple scope depths later...
	if (firstSepPos != -1)
		keyScope = keyScope.Left(firstSepPos);
	if (keyScope.IsEmpty())
		return;

	std::vector<WTString> args;
	{
		WTString allArgs(originalDefaultTypes.Mid(openPos + 1, closePos - openPos - 1));
		DecodeScopeInPlace(allArgs);
		allArgs.ReplaceAll('.', DB_SEP_CHR);
		if (allArgs.IsEmpty())
			return;

		int commaPos = allArgs.Find(',');
		if (commaPos != -1)
		{
			do
			{
				args.emplace_back(allArgs.left_sv((uint32_t)commaPos));
				allArgs.MidInPlace(commaPos + 1);
				commaPos = allArgs.Find(',');
			} while (commaPos != -1);
		}
		args.emplace_back(std::move(allArgs));
	}

	const WTString scopeTest(keyScope + DB_SEP_STR);
	WTString tmpArg, newArgs;
	for (std::vector<WTString>::iterator it = args.begin(); it != args.end();)
	{
		WTString &curArg = *it;
		if (!curArg.IsEmpty() && curArg.Find(scopeTest) == -1)
		{
			bool didTrimL = false;
			bool didTrimR = false;
			bool didTrimR2 = false;
			bool didTrimRef = false;
			bool didTrimPtr = false;
			bool didTrimManagedPtr = false;
			if (curArg[0] == ' ')
			{
				curArg.TrimLeft();
				didTrimL = true;
			}

			// Fix to avoid ASSERTS in WTString's [] operator, when index is -1.
			// -------------------------------------------------------------------------
			// Added !curArg.IsEmpty() checks for cases when VA parses while user types
			// template base class such as : foo_base< int, bool, > and one arg is still
			// missing within already enclosed template argument list.

			if (curArg.ends_with(' '))
			{
				curArg.TrimRight();
				didTrimR = true;
			}
			if (curArg.ends_with('&'))
			{
				curArg.LeftInPlace(curArg.GetLength() - 1);
				didTrimRef = true;
			}
			if (curArg.ends_with('*'))
			{
				curArg.LeftInPlace(curArg.GetLength() - 1);
				didTrimPtr = true;
			}
			if (curArg.ends_with('^'))
			{
				curArg.LeftInPlace(curArg.GetLength() - 1);
				didTrimManagedPtr = true;
			}
			if (curArg.ends_with(' '))
			{
				curArg.TrimRight();
				didTrimR2 = true;
			}

			tmpArg = {DB_SEP_STR, scopeTest, curArg};
			DType* dat2 = m_mp->FindExact2(tmpArg);
			if (dat2)
			{
				if (dat2->IsMarkedForDetach())
					mUsedDetachedData = true;

				curArg = tmpArg.mid_sv(1);
			}

			if (didTrimL)
				curArg.prepend(' ');
			if (didTrimR2)
				curArg += ' ';
			if (didTrimPtr)
				curArg += '*';
			if (didTrimManagedPtr)
				curArg += '^';
			if (didTrimRef)
				curArg += '&';
			if (didTrimR)
				curArg += ' ';
		}

		curArg.ReplaceAll(DB_SEP_CHR, '.');
		EncodeScopeInPlace(curArg);
		newArgs += curArg;
		if (++it != args.end())
		{
			static const auto comma_enc = EncodeScope(",");
			newArgs += comma_enc;
		}
	}

	workingType = {originalDefaultTypes.left_sv(openPos + 1u), newArgs, originalDefaultTypes.mid_sv((uint32_t)closePos)};
	originalDefaultTypes = workingType;
	workingType.LeftInPlace(workingType.GetLength() - 1);
}

// invalidate cache that might be present due to an abandoned call to GetBaseClassList
// (don't need to check g_pGlobDic since if that was updated, then the call wasn't abandoned)
void BaseClassFinder::InvalidateCache(const WTString& cacheLookup)
{
	DType* data;
	// don't use GetCachedBclDtype() because we don't want to invalidate g_pGlobDic

	if (m_mp->FileType() == Src || m_mp->FileType() == UC)
	{
		data = m_mp->LocalHcbDictionary()->FindExact(cacheLookup, 0, false, CachedBaseclassList, false);
		if (data)
		{
			// mDef is present due to way the entry was created (not read from dfile)
			_ASSERTE(data->HasData());
			data->SetDef(kInvalidCache);
		}
	}
	else
	{
		data = m_mp->LDictionary()->FindExact(cacheLookup, 0, CachedBaseclassList, false);
		if (data)
		{
			// mDef is present due to way the entry was created (not read from dfile)
			_ASSERTE(data->HasData());
			data->SetDef(kInvalidCache);
		}
	}
}
