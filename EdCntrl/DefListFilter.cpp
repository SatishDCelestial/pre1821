#include "StdAfxEd.h"
#include "SymbolPositions.h"
#include "TokenW.h"
#include "log.h"
#include "project.h"
#include "DefListFilter.h"
#include "file.h"
#include "incToken.h"
#include "Registry.h"
#include "Mparse.h"
#include "FileTypes.h"
#include "RegKeys.h"
#include "StringUtils.h"
#include "ProjectInfo.h"
#include "Settings.h"
#include "SymbolRemover.h"
#include "ParseThrd.h"
#include "FileId.h"
#include "EDCNT.H"
#include "CommentSkipper.h"
#include "InferType.h"
#include "DBQuery.h"
#include "IntroduceVariable.h"
#include "ParenSkipper.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

const SymbolPosList& DefListFilter::Filter(const SymbolPosList& defList, bool constructor /*= false*/)
{
	FilterDupesAndHides(defList);

	// only filter externals if current file is not external
	if (mCurFile.IsEmpty() || GlobalProject->ContainsNonExternal(mCurFile))
		FilterExternalDefs();

	FilterNonActiveProjectDefs();

	if (JS == gTypingDevLang)
		FilterForPreferredDefs();

	if (IsCFile(gTypingDevLang))
		FilterSystemDefs();
	else if (CS == gTypingDevLang)
		FilterGeneratedNamespaces();

	bool selfIncluded = TouchUpDefList();

	// do this after touchUp since touchUp could remove some items
	FilterGeneratedSourceFiles();

	EdCntPtr ed(g_currentEdCnt);
	if (Psettings->mGotoOverloadResolutionMode != Settings::GORM_DISABLED &&
	    Is_C_CS_File(gTypingDevLang) /*Is_C_CS_File(gTypingDevLang)*/ && ed && (ed->GetSymDtypeType() == FUNC || constructor))
	{
		SymbolPosList save = mPosList;

		VirtualDefListGoto virtualizedList;
		virtualizedList.SetList(&mPosList);
		OverloadResolver resolver(virtualizedList);
		resolver.Resolve(OverloadResolver::CALL_SITE);

		if (mPosList.size() == 0)
		{
			mPosList = save;
		}
		else
		{
			if (Psettings->mGotoOverloadResolutionMode == Settings::GORM_USE_SEPARATOR)
			{
				bool addSeparator = true;
				for (SymbolPosList::const_iterator it = save.begin(); it != save.end(); ++it)
				{
					if (std::find(mPosList.begin(), mPosList.end(), *it) == mPosList.end())
					{
						if (addSeparator)
						{
							SymbolPositionInfo m;
							m.mDisplayText = "---";
							m.mFilenameLower = " "; // to avoid ASSERT
							m.mFileId = Src;        // to avoid ASSERT
							mPosList.Add(m);
							addSeparator = false;
						}
						mPosList.Add(*it);
					}
				}
			}
		}
	}

	const UINT kCurrentFileId = gFileIdManager->GetFileId(mCurFile);
	int closestDistance = INT_MAX;
	SymbolPosList::const_iterator closestIt;
	for (SymbolPosList::const_iterator it = mPosList.begin(); it != mPosList.end(); ++it)
	{
		SymbolPositionInfo curInfo(*it);

		if (kCurrentFileId == curInfo.mFileId && (curInfo.mLineNumber < kRemoveLn))
		{
			int distance = kRemoveLn - curInfo.mLineNumber;
			if (distance <= closestDistance)
			{
				closestDistance = distance;
				closestIt = it;
			}
		}
	}
	if (closestDistance != INT_MAX)
		mPosList.erase(closestIt);

	if (!selfIncluded)
		FilterDeclaration();

	return mPosList;
}

const SymbolPosList& DefListFilter::FilterPlatformIncludes(const SymbolPosList& defList)
{
	if (!IsCFile(gTypingDevLang))
		return defList;

	for (SymbolPosList::const_iterator it = defList.begin(); it != defList.end(); ++it)
	{
		const SymbolPositionInfo& curInfo(*it);
		mPosList.Add(curInfo);
	}

	FilterSystemDefs();
	return mPosList;
}

void DefListFilter::FilterDupesAndHides(const SymbolPosList& defList)
{
	// first pass - read sGotoList, populates tmpList:
	//   exclude HIDETHIS entries
	//   exclude duplicateBasenames
	std::set<UINT> filesToRemove;
	for (SymbolPosList::const_iterator it = defList.begin(); it != defList.end(); ++it)
	{
		Log("GotoDef6");
		const SymbolPositionInfo& curInfo(*it);
		if (mPosList.Contains(curInfo.mFileId, curInfo.mLineNumber))
			continue;

		// allow hashtags through despite being hidden, but not refs to hashtags
		if (curInfo.mType == vaHashtag)
		{
			if (V_REF & curInfo.mAttrs)
				continue;
		}
		else if (V_HIDEFROMUSER & curInfo.mAttrs)
			continue;

		_ASSERTE(-1 == curInfo.mDisplayText.Find(L"HIDETHIS"));

		// [case: 71724]
		if (!::IsFile(curInfo.mFilename))
		{
			// [case: 80181] remove parse info for file that no longer exists
			if (!curInfo.mFilename.IsEmpty())
				filesToRemove.insert(curInfo.mFileId);
			continue;
		}

		mPosList.Add(curInfo);
	}

	if (filesToRemove.size() && g_ParserThread)
		g_ParserThread->QueueParseWorkItem(new SymbolRemover(filesToRemove));
}

void DefListFilter::FilterNonActiveProjectDefs()
{
	if (!Psettings->mEnableProjectSymbolFilter)
		return;

	if (mCurFile.IsEmpty())
		return;

	// [case: 67966]
	bool doFilter = false;
	const ProjectVec projForActiveFile(GlobalProject->GetProjectForFile(mCurFile));
	if (!projForActiveFile.size())
		return;

	if (::ContainsPseudoProject(projForActiveFile))
	{
		// [case: 68530] pseudo projects break the filter logic
		// return without filtering
		return;
	}

	SymbolPosList::const_iterator it;
	for (it = mPosList.begin(); it != mPosList.end(); ++it)
	{
		const SymbolPositionInfo& curInfo(*it);
		const ProjectVec curItemProj(GlobalProject->GetProjectForFile(curInfo.mFileId));
		if (!curItemProj.size())
		{
			// no project found for curInfo
			// curInfo is either system sym or is external
			// return without filtering
			return;
		}

		if (::ContainsPseudoProject(curItemProj))
		{
			// [case: 68530] pseudo projects break the filter logic
			// return without filtering
			return;
		}

		if (::Intersects(curItemProj, projForActiveFile))
		{
			doFilter = true;

			// don't break due to checks made for subsequent posList items that
			// might require return before filter.
		}
	}

	if (!doFilter)
		return;

	for (it = mPosList.begin(); it != mPosList.end();)
	{
		const SymbolPositionInfo& curInfo(*it);
		ProjectVec curItemProj(GlobalProject->GetProjectForFile(curInfo.mFileId));
		if (!::Intersects(curItemProj, projForActiveFile))
			mPosList.erase(it++);
		else
			++it;
	}
}

void DefListFilter::FilterExternalDefs()
{
	// first build list of duplicate basenames
	std::vector<CStringW> duplicateBasenames;
	SymbolPosList::const_iterator it;
	for (it = mPosList.begin(); it != mPosList.end(); ++it)
	{
		const SymbolPositionInfo& curInfo(*it);
		const CStringW base(::Basename(curInfo.mFilenameLower));

		// does basename of curInfo exist in list with different path
		// if not, continue
		for (SymbolPosList::const_iterator it2 = mPosList.begin(); it2 != mPosList.end(); ++it2)
		{
			const SymbolPositionInfo& curInfo2(*it2);
			const CStringW base2(::Basename(curInfo2.mFilenameLower));
			if (base2 == base && curInfo.mFilenameLower != curInfo2.mFilenameLower)
			{
				// add basename to savList if not already there
				if (duplicateBasenames.end() != find(duplicateBasenames.begin(), duplicateBasenames.end(), base))
					continue;

				duplicateBasenames.push_back(base);
				break;
			}
		}
	}

	// now treat the duplicateBasenames - reads duplicateBasenames, modifies mPosList
	for (std::vector<CStringW>::const_iterator basenamesIterator = duplicateBasenames.begin();
	     basenamesIterator != duplicateBasenames.end(); ++basenamesIterator)
	{
		const CStringW curItem(*basenamesIterator);
		bool anyWithinProject = false;
		std::vector<CStringW> pathsToDelete;

		for (it = mPosList.begin(); it != mPosList.end(); ++it)
		{
			const SymbolPositionInfo& curInfo(*it);

			if (curItem == ::Basename(curInfo.mFilenameLower))
			{
				if (GlobalProject->ContainsNonExternal(curInfo.mFilenameLower))
					anyWithinProject = true;
				else
					pathsToDelete.push_back(curInfo.mFilenameLower);
			}
		}

		// keep externals if no internal results
		if (!anyWithinProject)
			continue;

		for (std::vector<CStringW>::const_iterator pathsIterator = pathsToDelete.begin();
		     pathsIterator != pathsToDelete.end(); ++pathsIterator)
		{
			for (SymbolPosList::iterator it2 = mPosList.begin(); it2 != mPosList.end();)
			{
				const SymbolPositionInfo& curInfo(*it2);
				if (curInfo.mFilenameLower == *pathsIterator)
					mPosList.erase(it2++);
				else
					++it2;
			}
		}
	}
}

bool DefListFilter::TouchUpDefList()
{
	bool selfIncluded = false;

	IncludeDirs idtok;
	CStringW idirs = GlobalProject->GetProjAdditionalDirs();
	const WTString searchLimitToProject =
	    (const char*)GetRegValue(HKEY_CURRENT_USER, ID_RK_APP, "GoToSearchLimitToProject");
	BOOL limitToProject = FALSE;
	if (searchLimitToProject != "No")
	{
		// iterate over sGotoList and build string of filepaths
		// for ease of search since we need to iterate over the project dirs too
		CStringW filepathsFromCurrentDefList;
		for (SymbolPosList::const_iterator it = mPosList.begin(); it != mPosList.end(); ++it)
		{
			const CStringW curFile = (*it).mFilenameLower;
			filepathsFromCurrentDefList += curFile + L';';
		}

		// check to see if all entries are constrained to project dirs
		TokenW t = idirs;
		while (t.more() > 3)
		{
			CStringW dir = t.read(L";");
			dir.MakeLower();
			if (dir.GetLength() > 3 && -1 != filepathsFromCurrentDefList.Find(dir))
			{
				limitToProject = TRUE;
				break;
			}
		}
	}

	const UINT kCurrentFileId = gFileIdManager->GetFileId(mCurFile);
	CStringW CommonPath; // used to create shorter display names in menu
	int lcompath = 0;
	SymbolPosList tmpList, firstItemsList;
	tmpList.swap(mPosList);
	for (SymbolPosList::const_iterator it = tmpList.begin(); it != tmpList.end(); ++it)
	{
		Log("GotoDef6");
		SymbolPositionInfo curInfo(*it);
		if (kCurrentFileId == curInfo.mFileId &&
		    (kCurrentFileLn == curInfo.mLineNumber || kCurrentFileNextLn == curInfo.mLineNumber))
		{
			// [case: 118894]
			// if there's only one item in the list and it is a VAR, do not exclude it; to fix goto
			// on expression bodied methods that typically are on same line as FUNC declaration.
			// Logic restricted to VAR so that go to on CLASS and FUNC don't move caret to open
			// brace, typically on next line.
			selfIncluded = true;
			if (tmpList.size() > 1 || (1 == tmpList.size() && curInfo.mType != VAR))
				continue;
		}

		// adjust display text before adding to finalList
		TokenW tln = curInfo.mDisplayText;
		CStringW curFile = tln.read(L"\f");
		int delimPos = curFile.ReverseFind(L':');
		if (delimPos > 3)
			curFile = curFile.Left(delimPos);

		const CStringW fpath = Path(curFile);
		bool fromOtherProject = false;
		if (limitToProject && !GlobalProject->Contains(curFile))
		{
			if (!::ContainsIW(idirs, fpath))
				fromOtherProject = true;
		}

		if (!CommonPath.GetLength() && !fromOtherProject)
		{
			CStringW base(::Path(mCurFile));
			if (::ShouldIgnoreFile(mCurFile, false))
			{
				// [case: 36934] don't show anything relative to the temp dir - use solution dir instead
				if (!GlobalProject->SolutionFile().IsEmpty())
					base = ::Path(GlobalProject->SolutionFile());
			}

			CommonPath = base + L"\\";
			int i;
			for (i = 0; i < CommonPath.GetLength() && towlower(CommonPath[i]) == towlower(curInfo.mDisplayText[i]); i++)
				;
			if (i < 5)
				CommonPath = fpath + L"\\";
		}

		int lastpath = 0;
		for (int i = 0; i < CommonPath.GetLength() && towlower(CommonPath[i]) == towlower(curInfo.mDisplayText[i]); i++)
		{
			if ((curInfo.mDisplayText[i] == L'/' || curInfo.mDisplayText[i] == L'\\'))
				lastpath = i;
		}

		if (!fromOtherProject)
			curInfo.mDisplayText = curInfo.mDisplayText.Mid(lastpath ? lastpath + 1 : 0);
		curInfo.mDisplayText = ::DecodeScope(curInfo.mDisplayText);
		curInfo.mDisplayText.Replace(L"\f", L" ");
		if (-1 != curInfo.mDisplayText.Find(L"{") || curInfo.mAttrs & V_IMPLEMENTATION || // [case: 62542]
		    GOTODEF == curInfo.mType)
		{
			if (lastpath > lcompath)
				lcompath = lastpath;
			firstItemsList.Add(curInfo); // put implementations at the top of list
		}
		else
			mPosList.Add(curInfo);
	}

	// [case: 62542] sort mPosList and firstItemsList by file and line number
	mPosList.Sort();
	firstItemsList.Sort();

	// now add implementations to top
	mPosList.AddHead(firstItemsList);

	return selfIncluded;
}

void DefListFilter::FilterForPreferredDefs()
{
	bool containsAnyItemsWithPreferredStatus = false;

	SymbolPosList::iterator it;
	for (it = mPosList.begin(); it != mPosList.end(); ++it)
	{
		const SymbolPositionInfo& curInfo(*it);
		if (curInfo.mAttrs & V_PREFERREDDEFINITION)
		{
			containsAnyItemsWithPreferredStatus = true;
			break;
		}
	}

	if (!containsAnyItemsWithPreferredStatus)
		return;

	for (it = mPosList.begin(); it != mPosList.end();)
	{
		const SymbolPositionInfo& curInfo(*it);
		if (!(curInfo.mAttrs & V_PREFERREDDEFINITION))
			mPosList.erase(it++);
		else
			++it;
	}
}

void DefListFilter::FilterSystemDefs()
{
	// see also implementation of DTypeList::FilterNonActiveSystemDefs
	SymbolPosList::iterator it;
	SymbolPosList externalSysItems, sysItems;

	// split sys items into two buckets
	for (it = mPosList.begin(); it != mPosList.end(); ++it)
	{
		SymbolPositionInfo& curItem = *it;
		if (!(curItem.mDbFlags & (VA_DB_Cpp | VA_DB_Net) || curItem.mAttrs & V_SYSLIB) || (curItem.mAttrs & V_MANAGED))
			continue;

		const CStringW itemFile(gFileIdManager->GetFile(curItem.mFileId));
		if (IncludeDirs::IsSystemFile(itemFile))
			sysItems.Add(curItem);
		else
			externalSysItems.Add(curItem);
	}

	if (externalSysItems.empty() && sysItems.empty())
		return; // easy, no sys items - let all pass

	if (externalSysItems.empty())
		return; // have only sysdb items that are appropriate - let all pass

	if (sysItems.empty())
	{
		if (externalSysItems.size() != mPosList.size())
		{
			// [case: 110173]
			// we have a mix of sysdb items that are not current (external) and
			// solution items.
			// global va stdafx.h can fall into this category, so check for and remove
			// va stdafx items from mPosList.
			for (it = externalSysItems.begin(); it != externalSysItems.end(); ++it)
			{
				if (it->mAttrs & V_VA_STDAFX)
					mPosList.remove(*it);
			}
		}
		else
		{
			// have only sysdb items that are not current - let all pass, except for va stdafx
			for (it = externalSysItems.begin(); it != externalSysItems.end(); ++it)
			{
				if (it->mAttrs & V_VA_STDAFX)
				{
					if (mPosList.size() > 1)
						mPosList.remove(*it);
				}
			}
		}

		return;
	}

	// we have a mix of sysdb items, so need to filter the list of items
	// defined in files that don't come from the current set of include paths.

	// Remove items from mPosList that are in externalSysItems
	for (it = externalSysItems.begin(); it != externalSysItems.end(); ++it)
	{
		// [case: 65910]
		// it this assert fires, then something leaked into the list that sean didn't expect
		_ASSERTE(it->mAttrs & V_VA_STDAFX);
		mPosList.remove(*it);
	}
}

void DefListFilter::FilterGeneratedNamespaces()
{
	// [case: 67133]
	// built on the assumption that our pseudo-generated namespaces do not
	// have {...} and actual namespaces do have {...} in the def
	bool hasAnyNonGeneratedNs = false;
	SymbolPosList::iterator it;
	for (it = mPosList.begin(); it != mPosList.end(); ++it)
	{
		const SymbolPositionInfo& curInfo(*it);
		if (NAMESPACE == curInfo.mType)
		{
			if (-1 != curInfo.mDisplayText.Find(L'{'))
			{
				hasAnyNonGeneratedNs = true;
				break;
			}
		}
		else
		{
			return;
		}
	}

	if (!hasAnyNonGeneratedNs)
		return;

	for (it = mPosList.begin(); it != mPosList.end();)
	{
		const SymbolPositionInfo& curInfo(*it);
		if (-1 == curInfo.mDisplayText.Find(L'{'))
			mPosList.erase(it++);
		else
			++it;
	}
}

void DefListFilter::FilterGeneratedSourceFiles()
{
	// [case: 115343]
	if (!Psettings->mFilterGeneratedSourceFiles)
		return;

	SymbolPosList generatedSource;

	// iterate over mPosList and build generatedSource
	for (const auto& cur : mPosList)
	{
		const CStringW& file(cur.mFilenameLower);
		CStringW fileBase(::GetBaseName(file));
		int fType = ::GetFileType(fileBase);

		if (IsCFile(fType))
		{
			// #filterGeneratedSourceLiterals
			if (-1 != fileBase.Find(L".generated."))
				generatedSource.Add(cur);
			else if (Src == fType && 0 == fileBase.Find(L"moc_"))
				generatedSource.Add(cur); // Qt moc_*.cpp
			else if (Src == fType && -1 != fileBase.Find(L".gen."))
				generatedSource.Add(cur); // UE *.gen.cpp
		}
	}

	if (generatedSource.empty())
		return;

	if (generatedSource.size() == mPosList.size())
	{
		// do not filter if the result set would become empty
		return;
	}

	// for each item in generated source, remove from mPosList
	for (const auto& cur : generatedSource)
		mPosList.remove(cur);
}

void DefListFilter::FilterDeclaration()
{
	if (!Psettings->mEnableJumpToImpl)
		return;

	if (!Psettings->mEnableFilterWithOverloads || Psettings->mGotoOverloadResolutionMode == Settings::GORM_DISABLED)
		if (mPosList.size() != 2)
			return;

	SymbolPosList tmpList = mPosList;

	SymbolPosList::const_iterator it;
	int implCount = 0;
	for (it = tmpList.begin(); it != tmpList.end(); ++it)
	{
		const SymbolPositionInfo& curInfo(*it);
		if (curInfo.mAttrs & V_IMPLEMENTATION)
			implCount++;
	}

	if (implCount == 0)
		return;

	for (it = tmpList.begin(); it != tmpList.end();)
	{
		const SymbolPositionInfo& curInfo(*it);
		if (!(curInfo.mAttrs & V_IMPLEMENTATION) && curInfo.mDisplayText != "---")
			tmpList.erase(it++);
		else
			++it;
	}

	if (tmpList.size())
	{
		auto it3 = tmpList.begin();
		const SymbolPositionInfo& curInfo(*it3);
		if (curInfo.mDisplayText == "---")
			return;

		auto it2 = tmpList.end();
		it2--;
		const SymbolPositionInfo& curInfo2(*it2);
		if (curInfo2.mDisplayText == "---")
			tmpList.erase(it2);
	}

	mPosList = tmpList;
}

// ns:classname becomes ns::classname in C++ and ns.classname in C#
void ReplaceWithRealQualifiers(WTString& sc)
{
	for (int i = 0; i < sc.GetLength() - 1; i++)
	{
		if (sc[i] == ':' && sc[i + 1] != ':' && (i == 0 || sc[i - 1] != ':'))
		{
			if (IsCFile(gTypingDevLang))
			{
				if (i == 0 || sc[i - 1] == '\f')
					sc.ReplaceAt(i, 1, "");
				else
					sc.ReplaceAt(i, 1, "::");
			}
			else
			{
				if (i == 0 || sc[i - 1] == '\f')
					sc.ReplaceAt(i, 1, "");
				else
					sc.ReplaceAt(i, 1, ".");
			}
		}
	}
}

extern WTString GetInnerScopeName(WTString fullScope);
extern WTString GetReducedScope(const WTString& scope);
WTString UniformizeWhitespaces(WTString str)
{
	str = DecodeTemplates(str); // [case: 114785]

	for (int i = 0; i < str.GetLength(); i++)
	{
		TCHAR c = str[i];
		if (IsWSorContinuation(c))
		{
			if (c == '\t')
				str.ReplaceAt(i, 1, " ");
			if (i == 0 || i == str.GetLength() - 1 || !ISCSYM(str[i - 1]) || !ISCSYM(str[i + 1]))
			{
				str.ReplaceAt(i, 1, "");
				i--;
				continue;
			}
		}
	}

	return str;
}

void OverloadResolver::Resolve(ReferenceType referenceType)
{
	if (List.Size() == 0)
		return;

	// collect params list at caret
	WTString curParamList;
	bool constFunc = false;
	if (!GetParamListFromSource(curParamList, constFunc, referenceType))
		return; // we couldn't resolve the parameter list

	// is the caret on a reference (call-site)
	EdCntPtr ed(g_currentEdCnt);
	MultiParsePtr mp = ed->GetParseDb();
	bool notDef = !mp->m_isDef;
	bool isRef = notDef && !mp->m_isMethodDefinition;
	if (!isRef && notDef)
	{ // is it a call as part as an initialization in a definition? e.g. more complex forms of something like "int a =
	  // call();", see AST TestResolvingOverloadsGotoCS03
		CommentSkipper cs(ed->m_ftype);
		const WTString buf = ed->GetBuf();
		int pos = ed->GetBufIndex(buf, (long)ed->CurPos());
		for (int i = pos; i >= 0; i--)
		{
			TCHAR c = buf[i];
			if (cs.IsCodeBackward(buf, i))
			{
				if (ISCSYM(c))
					continue;
				if (c == '.' || c == ':')
					continue;
				if (IsWSorContinuation(c))
					continue;

				if (c == '=')
					isRef = true;

				break;
			}
		}
	}
	if (isRef)
	{
		if (mp->m_baseClass != "")
		{ // if the ref is inside a free function than the called is not const
			// if the ref is inside a method, we need to check if the method is a const
			constFunc = IsCaretInsideConstMethod();
		}
	}

	// are we dealing with C# named arguments?
	bool namedArgs = false;
	if (gTypingDevLang == CS && AreThereNamedArgs(curParamList))
		namedArgs = true;

	const DType dt(ed->GetSymDtype());
	WTString curName = dt.Sym();
	curName = TokenGetField(curName, "\xe");

	// populate DefinitionParameterLists
	List.ResetIterator();
	while (List.NotEnd())
	{
		const WTString& def = List.GetDef();
		WTString params;
		bool constFunc2 = false;
		if (!GetParamListFromDef(def, curName, params, constFunc2,
		                         List.GetFileSpecificEdCnt() ? List.GetFileSpecificEdCnt()->m_ftype : Src))
			DefinitionParameterLists.push_back("");
		else
			DefinitionParameterLists.push_back(params);

		List.Iterate();
	}

	// we iterate through the definitions until the resulting list is non-empty in C# AND named args. otherwise, this
	// only runs once and breaks at the end.
	for (DefinitionIndex = 0; DefinitionIndex < DefinitionParameterLists.size(); DefinitionIndex++)
	{
		// extract types of params at caret
		CurTypes = ExtractTypes(curParamList, isRef, ed);
		if (CurTypes.Empty)
			continue;
		CurTypes.Ref = isRef;
		CurTypes.HighPrecisionMode = isRef; // distinguishing by the number of non-default parameters if possible
		CurTypes.ConstFunc = constFunc;

		// first pass: extract types and collect them. we discover default params and decide about high precision mode.
		std::vector<TypeListComparer> typesArray;
		TypeListComparer masterParamList(this);
		List.ResetIterator();
		while (List.NotEnd())
		{
			const WTString& def = List.GetDef();
			WTString params;
			bool constFunc2 = false;
			if (!GetParamListFromDef(def, curName, params, constFunc2,
			                         List.GetFileSpecificEdCnt() ? List.GetFileSpecificEdCnt()->m_ftype : Src))
			{
				List.Iterate();
				TypeListComparer c(this);
				c.Ignore = true;
				c.ConstFunc = constFunc2;
				typesArray.push_back(c);
				continue;
			}
			TypeListComparer extractedTypes = ExtractTypes(params, false, List.GetFileSpecificEdCnt());
			extractedTypes.ConstFunc = constFunc2;
			typesArray.push_back(extractedTypes);

			// Is the number of parameters at the call site (ref) intersects only one signature considering the number
			// of parameters and default parameters? The marsterParamList is to store the first match by parameter count
			// - all other matches must either have a different parameter count or be equivalent with same parameter
			// count (implementation for declaration or the other way around or same parameters in base classes)
			// Otherwise, the match is not unique, thus we're using the traditional type of matching by types instead of
			// high precision mode which only considers the number of parameters.
			if (isRef)
			{
				size_t added = typesArray.size() - 1;
				TypeListComparer& addedTypes = typesArray[added];
				if (CurTypes.size() >= addedTypes.NonDefaultParams &&
				    (addedTypes.Ellipsis || CurTypes.size() <= addedTypes.size()))
				{
					if (masterParamList.size() == 0)
					{
						masterParamList = typesArray[added];
					}
					else
					{
						for (uint i = 0; i < typesArray.size(); i++)
						{
							if ((masterParamList.size() >= typesArray[i].NonDefaultParams &&
							     (typesArray[i].Ellipsis || masterParamList.size() <= typesArray[i].size())) ||
							    (typesArray[i].size() >= masterParamList.NonDefaultParams &&
							     (masterParamList.Ellipsis || typesArray[i].size() <= masterParamList.size())))
							{
								if (typesArray[i].ConstFunc != masterParamList.ConstFunc)
								{
									CurTypes.HighPrecisionMode = false;
									goto out;
								}
								for (uint j = 0; j < typesArray[i].NonDefaultParams; j++)
								{
									if (typesArray[i][j] != masterParamList[j] ||
									    (gTypingDevLang == CS &&
									     typesArray[i].size() != masterParamList.size())) // case 101287
									{
										CurTypes.HighPrecisionMode = false;
										goto out;
									}
								}
							}
						}
					}
				}
			out:;
			}

			List.Iterate();
		}

		// second pass: pair declarations that has default params with the appropriate implementations.
		// Although implementations don't directly indicate default params, I want to find them
		// when goto is triggered on a reference that has less params specified than the implementation itself.
		uint counter = 0;
		for (List.ResetIterator(); List.NotEnd(); List.Iterate(), ++counter)
		{
			if (typesArray[counter].Ignore)
				continue;

			const TypeListComparer& types = typesArray[counter];
			if (types.DefaultParams)
			{
				for (uint i = 0; i < typesArray.size(); i++)
				{
					if (i == counter)
						continue;
					TypeListComparer& arrItem = typesArray[i];
					if (types == arrItem)
					{
						arrItem.DefaultParams = true;
						arrItem.NonDefaultParams = types.NonDefaultParams;
					}
				}
			}
		}

		// third pass: resolve exact overloads
		counter = 0;
		List.ResetIterator();
		while (List.NotEnd())
		{
			if (typesArray[counter].Ignore)
			{
				counter++;
				List.Iterate();
				continue;
			}
			if (CurTypes == typesArray[counter])
			{
				typesArray[counter].IteratorCounter = List.counter;
				List.Iterate();
			}
			else
			{
				typesArray[counter].IteratorCounter = -1;
				List.EraseCurrentAndIterate();
			}
			counter++;
		}

		// fourth pass: lightweight solution to delete potentially unwanted items in the light of the full results
		int DeleteMeCounterP1 = 0;
		int DeleteMeCounterP2 = 0;
		int NoDeleteMeCounter = 0;
		counter = 0;
		List.ResetIterator();
		for (uint i = 0; i < typesArray.size(); i++)
		{
			const TypeListComparer& typeIterator = typesArray[i];
			if (typeIterator.IteratorCounter == -1)
				continue;

			if (typeIterator.GetDeleteMePriority() > 0)
			{
				// find the corresponding list item
				while ((int)counter < typeIterator.IteratorCounter && List.NotEnd())
				{
					List.Iterate();
					counter++;
				}
			}

			switch (typeIterator.GetDeleteMePriority())
			{
			case 1:
				DeleteMeCounterP1++;
				break;
			case 2:
				DeleteMeCounterP2++;
				break;
			default:
				NoDeleteMeCounter++;
				break;
			}
		}

		counter = 0;
		List.ResetIterator();
		int variety = 0;
		if (NoDeleteMeCounter)
			variety++;
		if (DeleteMeCounterP1)
			variety++;
		if (DeleteMeCounterP2)
			variety++;
		if (variety >= 2)
		{ // if we found both flagged and unflagged results, then delete the flagged ones. if we found only flagged
		  // ones, delete the highest priority ones
			for (uint i = 0; i < typesArray.size(); i++)
			{
				const TypeListComparer& typeIterator = typesArray[i];
				if ((DeleteMeCounterP1 && DeleteMeCounterP2 && typeIterator.GetDeleteMePriority() == 2) ||
				    (((!!DeleteMeCounterP1) != (!!DeleteMeCounterP2)) && typeIterator.GetDeleteMePriority() > 0))
				{
					// find the corresponding list item
					while ((int)counter < typeIterator.IteratorCounter && List.NotEnd())
					{
						List.Iterate();
						counter++;
					}
					if (List.NotEnd())
					{
						List.EraseCurrentAndIterate();
						counter++;
					}
				}
			}
		}

		// fifth pass: if there are non-ellipsis exact matches then delete all the ellipses
		List.ResetIterator();
		bool ellipsesEradicated = false;
		while (List.NotEnd())
		{
			WTString paramList;
			bool constFunc2 = false;
			if (!GetParamListFromDef(List.GetDef(), curName, paramList, constFunc2,
			                         List.GetFileSpecificEdCnt() ? List.GetFileSpecificEdCnt()->m_ftype : Src))
			{
				List.Iterate();
				continue;
			}
			if (GetNrOfEllipsisParams(paramList, List.GetFileSpecificEdCnt()) == -1)
			{ // we found a non-ellipsis, exact match. eradicate all ellipses from the list.
				List.ResetIterator();
				while (List.NotEnd())
				{
					constFunc2 = false;
					if (!GetParamListFromDef(List.GetDef(), curName, paramList, constFunc2,
					                         List.GetFileSpecificEdCnt() ? List.GetFileSpecificEdCnt()->m_ftype : Src))
					{
						List.Iterate();
						continue;
					}
					if (GetNrOfEllipsisParams(paramList, List.GetFileSpecificEdCnt()) != -1)
						List.EraseCurrentAndIterate();
					else
						List.Iterate();
				}
				ellipsesEradicated = true;
				break;
			}
			List.Iterate();
		}

		// sixth pass: if all overloads contain the ellipsis, then keep the overloads with the maximum nr. of
		// parameters, because it is the closest match
		std::pair<int, int> minMax = {INT_MAX, -INT_MAX};
		if (!ellipsesEradicated)
		{
			List.ResetIterator();
			while (List.NotEnd())
			{
				WTString paramList;
				bool constFunc2 = false;
				if (!GetParamListFromDef(List.GetDef(), curName, paramList, constFunc2,
				                         List.GetFileSpecificEdCnt() ? List.GetFileSpecificEdCnt()->m_ftype : Src))
				{
					List.Iterate();
					continue;
				}
				int params = GetNrOfEllipsisParams(paramList, List.GetFileSpecificEdCnt());
				if (params < minMax.first)
					minMax.first = params;
				if (params > minMax.second)
					minMax.second = params;
				List.Iterate();
			}

			if (minMax.second > minMax.first)
			{
				List.ResetIterator();
				while (List.NotEnd())
				{
					WTString paramList;
					bool constFunc3 = false;
					if (!GetParamListFromDef(List.GetDef(), curName, paramList, constFunc3,
					                         List.GetFileSpecificEdCnt() ? List.GetFileSpecificEdCnt()->m_ftype : Src))
					{
						List.Iterate();
						continue;
					}
					int params = GetNrOfEllipsisParams(paramList, List.GetFileSpecificEdCnt());
					if (params < minMax.second)
						List.EraseCurrentAndIterate();
					else
						List.Iterate();
				}
			}
		}

		if (List.Size() || !namedArgs) // this breaks in C++ immediately or in C# without named args
			break;
	}
}

// returns -1 is there are no ellipsis at the end of the param list
int OverloadResolver::GetNrOfEllipsisParams(WTString paramList, EdCntPtr ed)
{
	int ftype = Src;
	if (ed)
		ftype = ed->m_ftype;

	paramList = TokenGetField(paramList, "\f");
	WTString strippedParamList;
	CommentSkipper cs(ftype);
	for (int i = 0; i < paramList.GetLength(); i++)
	{
		TCHAR c = paramList[i];
		if (!cs.IsComment(c) && c != '/')
			strippedParamList += c;
	}
	strippedParamList.ReplaceAll("{...}", "");
	if (strippedParamList.GetLength() && strippedParamList[strippedParamList.GetLength() - 1] == ')')
		strippedParamList = strippedParamList.Left(strippedParamList.GetLength() - 1);
	SeparateMergedEllipsisParam(strippedParamList);
	if (IsFunctionParameterPackAtEnd(strippedParamList))
	{
		int parens = 0;
		int braces = 0;
		int angleBrackets = 0;
		int params = 1;
		int squareBrackets = 0;
		for (int i = 0; i < strippedParamList.GetLength(); i++)
		{
			TCHAR c = strippedParamList[i];
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
			if (c == '{')
			{
				braces++;
				continue;
			}
			if (c == '}')
			{
				braces--;
				continue;
			}
			if (c == '<')
			{
				angleBrackets++;
				continue;
			}
			if (c == '>')
			{
				angleBrackets--;
				continue;
			}
			if (c == '[')
			{
				squareBrackets++;
				continue;
			}
			if (c == ']')
			{
				squareBrackets--;
				continue;
			}
			if (parens == 0 && braces == 0 && angleBrackets == 0 && c == ',')
				params++;
		}
		return params;
	}
	else
	{
		return -1;
	}
}

bool OverloadResolver::SeparateMergedEllipsisParam(WTString& strippedParamList)
{
	strippedParamList.TrimRight();
	if (strippedParamList.GetLength() >= 3 && strippedParamList.Right(3) == "...")
	{ // separate merged ellipsis param (e.g. void foo(int ID...))
		strippedParamList = strippedParamList.Left(strippedParamList.GetLength() - 3);
		strippedParamList.TrimRight();
		int length = strippedParamList.GetLength();
		if (length && strippedParamList[length - 1] != ',')
			strippedParamList += ",...";
		else
			strippedParamList += "...";
		return true;
	}

	return false;
}

bool OverloadResolver::GetParamListFromSource(WTString& res, bool& constFunc, ReferenceType referenceType)
{
	EdCntPtr ed(g_currentEdCnt);
	if (!ed)
		return false;
	// int pos = kCurrentPos;
	const WTString buf = ed->GetBuf();
	int pos = ed->GetBufIndex(buf, (long)ed->CurPos());

	if (buf[pos] == '{')
	{ // if the caret is on '{', after the param list, rewind the pos before the caret list
		CommentSkipper cs(ed->m_ftype);
		for (int i = pos - 1; i >= 0; i--)
		{
			if (cs.IsCodeBackward(buf, i))
			{
				if (buf[i] == '(')
				{
					pos = i;
					break;
				}
			}
		}
	}

	CommentSkipper cs(ed->m_ftype);
	ParenSkipper ps(ed->m_ftype);
	ps.checkInsideParens = false;
	int parens = 0;
	const int maxMethodSignatureLength = 4096;
	bool waiting = false;
	bool collectPreParamListStr = true;
	WTString preParamListStr;
	for (int i = pos; i < std::min(buf.GetLength(), pos + 1 + maxMethodSignatureLength); i++)
	{
		TCHAR c = buf[i];
		if (collectPreParamListStr)
			preParamListStr += c;
		if (!cs.IsComment(c) && c != '/')
		{
			// <()> is skipped, but (<>) is not because template parameters can be in the param list, but parens inside
			// <>s e.g. in "tmp<int()>()" should be skipped
			if (!ps.IsInside(buf, i))
				waiting = false;
			if (parens == 0 && ps.IsInside(buf, i))
				waiting = true;
			if (waiting)
				continue;

			if (c == ';')
			{
				switch (referenceType)
				{
				case OverloadResolver::CONSTRUCTOR: {
					int sb = 0; // square brackets
					for (int j = pos; j < i; j++)
					{
						TCHAR c3 = buf[j];
						if (c3 == '<')
						{
							sb++;
							continue;
						}
						if (c3 == '>')
						{
							sb--;
							continue;
						}
						if (sb == 0)
						{
							if (!IsWSorContinuation(c3) && !ISCSYM(c3) && c3 != '.' && c3 != ':' && c3 != '*' &&
							    c3 != '&' && c3 != '^' && c3 != '=')
								return false;
						}
					}

					if (::FindInCode(preParamListStr, '=', ed->m_ftype) != -1 &&
					    ::FindWholeWordInCode(preParamListStr, "new", ed->m_ftype, 0) ==
					        -1) // this is not a constructor call
						return false;

					res = "";
					return true; // parameterless constructor
				}
				default:
					return false;
				}
			}
			if (c == '(')
			{
				parens++;
				collectPreParamListStr = false;
				if (parens == 1)
					continue;
			}
			else if (c == ')')
			{
				parens--;
				if (parens == 0)
				{
					if (::FindInCode(preParamListStr, '=', ed->m_ftype) != -1 &&
					    ::FindWholeWordInCode(preParamListStr, "new", ed->m_ftype, 0) ==
					        -1) // this is not a constructor call
						return false;

					CommentSkipper cs2(ed->m_ftype);
					for (int j = i; j < buf.GetLength(); j++)
					{
						TCHAR c2 = buf[j];
						if (!cs2.IsCode(c2))
							continue;
						if (c2 == ';' || c2 == '{')
							break;
						if (j + 6 < buf.GetLength() && !ISCSYM(c2) && buf[j + 1] == 'c' && buf[j + 2] == 'o' &&
						    buf[j + 3] == 'n' && buf[j + 4] == 's' && buf[j + 5] == 't' && !ISCSYM(buf[j + 6]))
						{
							constFunc = true;
							break;
						}
						if (j + 6 < buf.GetLength() && !ISCSYM(c2) && buf[j + 1] == 'C' && buf[j + 2] == 'O' &&
						    buf[j + 3] == 'N' && buf[j + 4] == 'S' && buf[j + 5] == 'T' && !ISCSYM(buf[j + 6]))
						{
							constFunc = true;
							break;
						}
					}
					return true;
				}
				if (parens == -1) // STDMETHOD macro support
					parens = 0;
			}
			if (parens)
				res += c;
		}
	}

	return false;
}

bool OverloadResolver::GetParamListFromDef(WTString def, const WTString& curName, WTString& paramList, bool& constFunc,
                                           int fileType)
{
	def = TokenGetField(def, "\f");

	int namePos = def.find(curName.c_str());
	WTString params;
	if (namePos != -1)
	{
		int parenPos = def.find("(", namePos);
		int parens = 0;
		if (parenPos != -1)
		{
			for (int i = parenPos; i < def.GetLength(); i++)
			{
				TCHAR c = def[i];
				if (c == '(')
				{
					parens++;
					if (parens == 1)
						continue;
				}
				if (c == ')')
				{
					parens--;
					if (parens == 0)
					{
						int constPos = ::FindWholeWordInCode(def, "const", fileType, i);
						if (constPos != -1)
						{
							constFunc = true;
						}
						else
						{
							constPos = ::FindWholeWordInCode(def, "CONST", fileType, i);
							if (constPos != -1)
								constFunc = true;
						}

						break;
					}
				}
				params += c;
			}
		}
		else
		{
			return false;
		}
	}
	else
	{
		return false;
	}

	params.Trim();
	if (params == "void")
		params = "";
	paramList = params;
	return true;
}

void OverloadResolver::ReplaceItemInTypeList(TypeListComparer& types, uint index, const WTString& val)
{
	if (index >= types.size())
		types.resize(index + 1);

	types[index] = val;
}

TypeListComparer OverloadResolver::ExtractTypes(const WTString& paramList, bool ref, EdCntPtr fileSpecificEd)
{
	WTString strippedParamList;
	CommentSkipper cs(fileSpecificEd ? fileSpecificEd->m_ftype : Src);
	for (int i = 0; i < paramList.GetLength(); i++)
	{
		TCHAR c = paramList[i];
		if (!cs.IsComment(c) && c != '/')
			strippedParamList += c;
	}
	TypeListComparer types(this);
	if (SeparateMergedEllipsisParam(strippedParamList))
		types.Ellipsis = true;
	else if (IsFunctionParameterPackAtEnd(strippedParamList))
		types.Ellipsis = true;

	cs.Reset();
	ParenSkipper ps(fileSpecificEd ? fileSpecificEd->m_ftype : Src);
	types.NonDefaultParams = 0;
	types.DefaultParams = false;
	for (int i = 0; i < strippedParamList.GetLength(); i++)
	{
		TCHAR c = strippedParamList[i];
		if (!cs.IsComment(c) && !ps.IsInside(strippedParamList, i))
		{
			if (c == ',')
			{
				bool def;
				auto typeAndLoc = GetTypeAndLocation(strippedParamList, i, ref, def, fileSpecificEd);
				if (typeAndLoc.second == -2)
				{ // if we cannot match up a parameter with the current definition we had chosen in Resolve(), than go
				  // to the next definition
					types.Empty = true;
					return types;
				}

				typeAndLoc.first = UniformizeWhitespaces(typeAndLoc.first);
				if (typeAndLoc.second == -1)
					types.push_back(typeAndLoc.first);
				else
					ReplaceItemInTypeList(types, (uint)typeAndLoc.second, typeAndLoc.first);

				if (!ref)
				{ // the two below param is non-relevant for refs (call-sites)
					if (!def)
						types.NonDefaultParams++;
					else
						types.DefaultParams = true;
				}
			}
		}
	}

	bool def;
	std::pair<WTString, int> typeAndLoc =
	    GetTypeAndLocation(strippedParamList, strippedParamList.GetLength(), ref, def, fileSpecificEd);
	if (typeAndLoc.second == -2)
	{ // if we cannot match up a parameter with the current definition we had chosen in Resolve(), than go to the next
	  // definition
		types.Empty = true;
		return types;
	}

	typeAndLoc.first = UniformizeWhitespaces(typeAndLoc.first);
	if (typeAndLoc.second == -1)
		types.push_back(typeAndLoc.first);
	else
		ReplaceItemInTypeList(types, (uint)typeAndLoc.second, typeAndLoc.first);
	if (!ref)
	{ // the two below param is non-relevant for refs (call-sites)
		if (!def)
			types.NonDefaultParams++;
		else
			types.DefaultParams = true;
	}

	return types;
}

std::pair<WTString, int> OverloadResolver::GetTypeAndLocation(const WTString& paramList, int charIndex, bool ref,
                                                              bool& defaultParam, EdCntPtr fileSpecificEd)
{
	// pull in the whole area between commas
	int areaBeg;
	int parens = 0;
	for (areaBeg = charIndex - 1; areaBeg > 0; areaBeg--)
	{
		if (paramList[areaBeg] == ')')
		{
			parens++;
			continue;
		}
		if (paramList[areaBeg] == '(')
		{
			parens--;
			continue;
		}
		if (parens == 0 && paramList[areaBeg] == ',')
		{
			areaBeg++;
			break;
		}
	}
	WTString areaString = paramList.Mid(areaBeg, charIndex - areaBeg);
	areaString.Trim();

	if (ref)
	{
		defaultParam = false;
		if (areaString.GetLength())
		{
			int loc = -1;
			int colonPos = areaString.find(':');
			if (colonPos != -1 && gTypingDevLang == CS)
			{
				int questionMarkPos = ::FindInCode(areaString, '?', fileSpecificEd ? fileSpecificEd->m_ftype : Src);
				if (questionMarkPos == -1 || questionMarkPos > colonPos)
				{
					WTString argName = areaString.Left(colonPos);
					int argPos = GetArgPosByName(argName, fileSpecificEd ? fileSpecificEd->m_ftype : Src);
					if (argPos == -1)
						return std::make_pair("", -2); // if we cannot match up a parameter with the current definition
						                               // we had chosen in Resolve(), than go to the next definition
					loc = argPos;
					areaString = areaString.Mid(colonPos + 1);
				}
			}
			EdCntPtr ed(g_currentEdCnt);
			MultiParsePtr mp = ed->GetParseDb();
			WTString bcl = mp->GetBaseClassList(mp->m_baseClass);
			InferType infer;
			WTString appendStar;
			if (areaString.Left(1) == "&")
			{
				areaString.ReplaceAt(0, 1, "");
				appendStar = "*";
			}

			if (areaString == "nullptr" || areaString == "null")
				return std::make_pair(areaString, loc);

			if (IsCFile(gTypingDevLang))
			{
				WTString macroInput = areaString;
				auto cached = MacroCache.find(macroInput);
				if (cached != MacroCache.end())
				{
					areaString = MacroCache[macroInput];
				}
				else
				{
					if (TypeListComparer::IsTextMacro(areaString))
						areaString = CurTypes.ExpandTypeDefOrMacro(
						    areaString, true); // this type of macro is not expanded correctly by VAParseExpandAllMacros
					else
						areaString = VAParseExpandAllMacros(mp, areaString);
					if (macroInput != areaString)
						MacroCache.emplace(macroInput, areaString);
				}
			}

			WTString typeName = infer.Infer(areaString, ed->m_lastScope, bcl, mp->FileType()) + appendStar;
			if (IsCFile(gTypingDevLang) && areaString == "0")
				typeName = "const int0";
			if (typeName != "auto")
			{
				if (!TypeListComparer::IsPrimitiveCType(typeName))
					QualifyTypeViaUsingNamespaces(typeName, mp.get());
				return std::make_pair(typeName, loc);
			}
			else
			{
				return std::make_pair(areaString, loc);
			}
		}
		return std::make_pair("", -1);
	}
	else
	{
		int startPos;
		int equalitySign = areaString.Find("=");
		if (equalitySign != -1)
		{
			startPos = equalitySign;
			defaultParam = true;
		}
		else
		{
			startPos = areaString.GetLength();
			if (IsFunctionParameterPackAtEnd(areaString))
				defaultParam = true;
			else
				defaultParam = false;
		}

		int varEnd;
		for (varEnd = startPos - 1; varEnd > 0; varEnd--)
		{
			TCHAR c = areaString[varEnd];
			if (!IsWSorContinuation(c))
			{
				varEnd++;
				break;
			}
		}

		int varBeg;
		for (varBeg = varEnd - 1; varBeg > 0; varBeg--)
		{
			TCHAR c = areaString[varBeg];
			if (!ISCSYM(c))
			{
				if (varBeg < varEnd)
					varBeg++;
				break;
			}
		}

		int typeBeg;
		for (typeBeg = varBeg - 1; typeBeg > 0; typeBeg--)
		{
			TCHAR c = areaString[typeBeg];
			if (c == ',')
			{
				typeBeg++;
				break;
			}
		}
		if (typeBeg < 0)
			typeBeg = 0;

		WTString typeStr;
		if (typeBeg == varBeg)
			typeStr = areaString.Mid(varBeg, varEnd - varBeg); // there is no parameter name
		else
			typeStr = areaString.Mid(typeBeg, varBeg - typeBeg);
		typeStr.Trim();

		if (fileSpecificEd)
		{
			MultiParsePtr fileSpecificMp = fileSpecificEd->GetParseDb();
			if (fileSpecificMp)
				QualifyTypeViaUsingNamespaces(typeStr, fileSpecificMp.get());
		}

		return std::make_pair(typeStr, -1);
	}
}

void OverloadResolver::QualifyTypeViaUsingNamespaces(WTString& typeName, MultiParse* mp)
{
	WTString sc = typeName;
	sc.ReplaceAll("(", "");
	sc.ReplaceAll(")", "");
	sc.ReplaceAll("*", "");
	sc.ReplaceAll("^", "");
	sc.ReplaceAll("&", "");
	sc.ReplaceAll("const", "", TRUE);
	sc.ReplaceAll("CONST", "", TRUE);
	sc.Trim();
	int scPos = typeName.Find(sc.c_str());
	int scLen = sc.GetLength();
	if (scPos != -1 && !TypeListComparer::IsPrimitiveCType(sc))
	{
		bool added = false;
		if (sc.GetLength() && sc[0] != ':')
		{
			sc = ':' + sc;
			added = true;
		}
		AdjustScopesForNamespaceUsings(mp, mp->GetBaseClassList(typeName), sc, sc);
		if (added && sc.GetLength() && sc[0] == ':')
			sc = sc.Mid(1);
		ReplaceWithRealQualifiers(sc);
		typeName.ReplaceAt(scPos, scLen, sc.c_str()); // replace type with the qualified version
	}
}

bool OverloadResolver::IsFunctionParameterPackAtEnd(const WTString& strippedParamList)
{
	int parens = 0;
	int squareBrackets = 0;
	int braces = 0;
	int angleBrackets = 0;
	int dotCounter = 0;
	for (int i = strippedParamList.GetLength() - 1; i >= 0; i--)
	{
		TCHAR c = strippedParamList[i];
		if (c == ')')
		{
			parens++;
			continue;
		}
		if (c == '(')
		{
			parens--;
			continue;
		}
		if (c == ']')
		{
			squareBrackets++;
			continue;
		}
		if (c == '[')
		{
			squareBrackets--;
			continue;
		}
		if (c == '}')
		{
			braces++;
			continue;
		}
		if (c == '{')
		{
			braces--;
			continue;
		}
		if (c == '>')
		{
			angleBrackets++;
			continue;
		}
		if (c == '<')
		{
			angleBrackets--;
			continue;
		}
		if (parens == 0 && squareBrackets == 0 && braces == 0 && angleBrackets == 0 && c == ',')
			return false;

		if (c == '.')
		{
			dotCounter++;
			if (dotCounter >= 3)
				return true;
		}
		else
		{
			dotCounter = 0;
		}
	}

	return false;
}

bool OverloadResolver::IsCaretInsideConstMethod()
{
	auto def = GetMethodNameAndDefWhichCaretIsInside();
	if (def.first != "" && def.second != "")
	{
		int namePos = def.second.find(def.first.c_str());
		if (namePos == -1)
			return false;

		int parenPos = def.second.find("(", namePos);
		if (parenPos == -1)
			return false;

		int correspondingParenPos = IntroduceVariable::FindCorrespondingParen(def.second, parenPos);
		if (correspondingParenPos == -1)
			return false;

		int constPos = def.second.find("const", correspondingParenPos);
		if (constPos == -1)
		{
			int CONSTPos = def.second.find("CONST", correspondingParenPos);
			if (CONSTPos == -1)
				return false;
		}

		return true;
	}

	return false;
}

// works with overloaded methods
std::pair<WTString, WTString> OverloadResolver::GetMethodNameAndDefWhichCaretIsInside()
{
	EdCntPtr ed(g_currentEdCnt);
	MultiParsePtr mp = ed->GetParseDb();
	WTString scope = ed->Scope();
	WTString cutScope = ed->Scope();
	WTString methodName = GetInnerScopeName(cutScope);
	WTString BCL = mp->m_baseClassList;
	DType* sym = mp->FindSym(&methodName, &scope, &BCL);
	while (sym && !sym->IsMethod())
	{ // cutting in-method stuff like "for", "if", etc. from the end of the scope
		WTString newScope = GetReducedScope(cutScope);
		if (newScope == cutScope)
			break;
		cutScope = newScope;
		methodName = GetInnerScopeName(cutScope);
		sym = mp->FindSym(&methodName, &scope, &BCL);
	}

	if (sym && sym->IsMethod())
	{ // must be func or method
		const uint kCurPos = ed->CurPos();
		const ULONG kUserAtLine = TERROW(kCurPos);
		WTString fileText(ed->GetBuf(TRUE));
		MultiParsePtr mparse = ed->GetParseDb();
		LineMarkers markers; // outline data
		GetFileOutline(fileText, markers, mparse);

		LineMarkerPath pathForUserLine; // path to caret
		markers.CreateMarkerPath(kUserAtLine, pathForUserLine, false, true);
		for (size_t i = 0; i < pathForUserLine.size(); i++)
		{
			if (pathForUserLine[i].mText.Find(methodName.Wide()) != -1)
			{
				return std::make_pair(methodName, pathForUserLine[i].mText);
				// return !!DelayFileOpen(mTargetFile, pathForUserLine[i].mStartLine).get();
			}
		}

		return std::make_pair(methodName, "");
	}

	return std::make_pair("", "");

	//	return ::GotoDeclPos(jumpScope, mTargetFile, type);
}

int OverloadResolver::GetArgPosByName(const WTString& argName, int ftype)
{
	WTString paramList = DefinitionParameterLists[DefinitionIndex];
	WTString strippedParamList;
	CommentSkipper cs(ftype);
	for (int i = 0; i < paramList.GetLength(); i++)
	{
		TCHAR c = paramList[i];
		if (!cs.IsComment(c) && c != '/')
			strippedParamList += c;
	}

	int parens = 0;
	int angleBrackets = 0;
	int paramCounter = 0;
	for (int i = 0; i < strippedParamList.GetLength(); i++)
	{
		TCHAR c = strippedParamList[i];
		if (c == '(')
			parens++;
		if (c == ')')
			parens--;
		if (c == '<')
			angleBrackets++;
		if (c == '>' && (i == 0 || strippedParamList[i - 1] != '-'))
			angleBrackets--;
		if (parens == 0 && angleBrackets == 0 && c == ',')
		{
			WTString name = GetParamName(strippedParamList, i);
			if (argName == name)
				return paramCounter;
			paramCounter++;
		}
	}

	WTString name = GetParamName(strippedParamList, strippedParamList.GetLength());
	if (argName == name)
		return paramCounter;

	return -1;
}

WTString OverloadResolver::GetParamName(const WTString& paramList, int pos)
{
	int areaBeg;
	int parens = 0;
	for (areaBeg = pos - 1; areaBeg > 0; areaBeg--)
	{
		if (paramList[areaBeg] == ')')
		{
			parens++;
			continue;
		}
		if (paramList[areaBeg] == '(')
		{
			parens--;
			continue;
		}
		if (parens == 0 && paramList[areaBeg] == ',')
		{
			areaBeg++;
			break;
		}
	}
	WTString areaString = paramList.Mid(areaBeg, pos - areaBeg);
	areaString.Trim();

	int startPos;
	int equalitySign = areaString.Find("=");
	if (equalitySign != -1)
		startPos = equalitySign;
	else
		startPos = areaString.GetLength();

	int varEnd;
	for (varEnd = startPos - 1; varEnd > 0; varEnd--)
	{
		TCHAR c = areaString[varEnd];
		if (!IsWSorContinuation(c))
		{
			varEnd++;
			break;
		}
	}

	int varBeg;
	for (varBeg = varEnd - 1; varBeg > 0; varBeg--)
	{
		TCHAR c = areaString[varBeg];
		if (!ISCSYM(c))
		{
			if (varBeg < varEnd)
				varBeg++;
			break;
		}
	}

	return areaString.Mid(varBeg, varEnd - varBeg);
}

bool OverloadResolver::AreThereNamedArgs(const WTString& curParamList)
{
	CommentSkipper cs(CS);
	int lastQuestionMarkPos = -1;
	int lastCommaPos = 0;
	for (int i = 0; i < curParamList.GetLength(); i++)
	{
		TCHAR c = curParamList[i];
		if (cs.IsCode(c))
		{
			if (c == '?')
			{
				lastQuestionMarkPos = i;
				continue;
			}
			if (c == ',')
			{
				lastCommaPos = i;
				continue;
			}

			if (lastQuestionMarkPos < lastCommaPos && c == ':')
				return true;
		}
	}

	return false;
}

WTString TypeListComparer::ExpandTypeDefOrMacro(const WTString& typdef, bool macro)
{
	std::pair<int, WTString> singleWord = GetIfSingleWord(typdef);
	bool typedefFound = false;
	if (!singleWord.second.IsEmpty() && !IsBasicType(singleWord.second))
	{
		if (!macro)
		{
			auto cached = TypeDefCache.find(singleWord.second);
			if (cached != TypeDefCache.end())
				return TypeDefCache[singleWord.second];
		}
		WTString res = ExpandTypeDefInner(typdef, singleWord.first, macro);
		if (!macro && res != typdef)
		{
			typedefFound = true;
			auto singleWord2 = GetIfSingleWord(res);
			if (!singleWord2.second.IsEmpty() && !IsBasicType(singleWord2.second))
				res = ExpandTypeDefInner(res, singleWord2.first, macro); // deliberately doing only 2 passes
		}

		if (typedefFound)
			TypeDefCache.emplace(singleWord.second, res);

		return res;
	}

	return typdef;
}

WTString TypeListComparer::ExpandTypeDefInner(const WTString& typdef, int startPos, bool macro)
{
	EdCntPtr ed(g_currentEdCnt);
	MultiParsePtr mp = ed->GetParseDb();

	int wordLen = typdef.GetLength() - startPos;
	for (int i = startPos; i < typdef.GetLength(); i++)
	{
		TCHAR c = typdef[i];
		if (!ISCSYM(c))
		{
			if (gTypingDevLang == CS && c == '.')
			{
				startPos = i + 1; // the word should be the part after the last . (symbol name)
				continue;
			}
			wordLen = i - startPos;
			break;
		}
	}

	if (wordLen == 0)
		return typdef;

	WTString word = typdef.Mid(startPos, wordLen);

	WTString scope;
	if (IsCFile(gTypingDevLang))
		scope = ":" + word;
	else
		scope = typdef;

	WTString bcl;
	DType* dt = mp->FindSym(&word, &scope, &bcl);
	if (dt == nullptr)
		dt = mp->FindAnySym(word);
	if (dt == nullptr)
		return typdef;
	uint maskedType = dt->MaskedType();
	WTString def = dt->Def();
	def.Trim();
	if (def == "")
	{
		dt = mp->FindAnySym(word);
		if (dt)
			def = dt->Def();
	}

	def = TokenGetField(def, "\f");
	if (def.GetLength())
	{
		if (!macro)
		{
			if (def.Left(7) == "typedef")
			{
				if (def.Right(1) == ")") // we don't try to resolve complex types
					return typdef;

				def.ReplaceAll("typedef", "", TRUE);
				def = GetWithouteLastWord(def);
				def.ReplaceAll("far", "", TRUE);
				def.ReplaceAll("near", "", TRUE);
				def.Trim();
				def = UniformizeWhitespaces(def);
				WTString res = typdef;
				res.ReplaceAt(startPos, wordLen, def.c_str());

				return res;
			}
			else
			{
				if (gTypingDevLang == CS && maskedType == TYPE) // C# alias via using directive (case 101367)
					return def;
			}
		}
		else if (def.Left(7) == "#define")
		{
			def.ReplaceAll("#define", "", TRUE);
			def.ReplaceAll(word.c_str(), "", TRUE);
			def.Trim();
			def = UniformizeWhitespaces(def);
			if (IsTextMacro(typdef))
			{
				return def;
			}
			else
			{
				WTString res = typdef;
				res.ReplaceAt(startPos, wordLen, def.c_str());

				return res;
			}
		}
	}

	return typdef;
}

std::pair<int, WTString> TypeListComparer::GetIfSingleWord(const WTString& type, bool acceptQualifier /*= false*/)
{
	int startPos = 0;
	int len = type.GetLength();
	int i;
	if (type.Left(5) == "const" || type.Left(5) == "CONST")
	{
		startPos = 5;
		len = type.GetLength();
	}
	else if (IsTextMacro(type))
	{
		startPos = 3;
		len = type.GetLength() - 1;
	}
	for (; startPos < len; startPos++)
		if (!IsWSorContinuation(type[startPos]))
			break;
	for (i = startPos; i < len; i++)
	{
		TCHAR c = type[i];
		if (!ISCSYM(c) &&
		    ((IsCFile(gTypingDevLang) && !acceptQualifier) || (c != '.' && (!acceptQualifier || c != ':'))))
		{
			for (int j = i; j < len; j++)
			{
				TCHAR c2 = type[j];
				if (c2 != '*' && c2 != '&' && c2 != '^' && !IsWSorContinuation(c2))
					return std::make_pair(-1, "");
			}
			break;
		}
	}

	return std::make_pair(startPos, type.Mid(startPos, i - startPos));
}

WTString TypeListComparer::GetWithouteLastWord(const WTString& def)
{
	for (int i = def.GetLength() - 1; i >= 0; i--)
	{
		if (!ISCSYM(def[i]))
			return def.Left(i + 1);
	}

	return def;
}

std::vector<WTString> TypeListComparer::GetTargetMethods(WTString typeName, TargetMethodsType tmt)
{
	std::vector<WTString> res;
	std::pair<int, WTString> posAndWord = GetIfSingleWord(typeName, true);
	if (posAndWord.first != -1)
		typeName = posAndWord.second.Mid(posAndWord.first);
	else
		return res;

	if (IsPrimitiveCType(typeName))
		return std::vector<WTString>();

	// try to get from cache
	switch (tmt)
	{
	case TypeListComparer::CAST_OPERATOR: {
		auto cached = CastCache.find(typeName);
		if (cached != CastCache.end())
			return CastCache[typeName];
		break;
	}
	case TypeListComparer::CAST_CONSTRUCTOR: {
		auto cached = ConstructorCache.find(typeName);
		if (cached != ConstructorCache.end())
			return ConstructorCache[typeName];
		break;
	}
	}

	EdCntPtr ed(g_currentEdCnt);
	MultiParsePtr mp = ed->GetParseDb();
	DBQuery query(mp);
	WTString enumSymScope;
	WTString bcl = mp->GetBaseClassList(typeName);
	WTString scope = ":" + typeName;
	WTString edScope = ed->Scope();
	query.FindAllSymbolsInScopeList(scope.c_str(), bcl.c_str());
	// if (query.Count() > 1000)
	//	return FALSE;

	// iterate and collect
	DTypeList list;
	WTString constructorParam;
	for (DType* dt = query.GetFirst(); dt; dt = query.GetNext())
	{
		if (dt->type() != FUNC)
			continue;
		dt->LoadStrs();
		// write the method that filters put non-cast operators
		WTString def = dt->Def();
		switch (tmt)
		{
		case CAST_OPERATOR: {
			if (IsCastOperator(def, ed->m_ftype))
				list.push_back(dt);
			break;
		}
		case CAST_CONSTRUCTOR: {
			if (IsCastConstructor(def, typeName, constructorParam, ed->m_ftype))
				list.push_back(dt);
			break;
		}
		}
	}

	list.FilterDupes();

	// extract the target type
	switch (tmt)
	{
	case TypeListComparer::CAST_OPERATOR: {
		for (auto dt : list)
		{
			WTString def = dt.Def();
			def.ReplaceAll("operator", "", TRUE);
			def.ReplaceAll("()", "");
			def.ReplaceAll("{...}", "");
			def.ReplaceAll("__forceinline", "");
			def.ReplaceAll("const", "");
			def.ReplaceAll("CONST", "");
			def.Trim();
			res.push_back(def);
		}

		CastCache.emplace(typeName, res);
		break;
	}
	case TypeListComparer::CAST_CONSTRUCTOR: {
		for (auto dt : list)
		{
			bool defParam;
			std::pair<WTString, int> typeAndLoc = Owner->GetTypeAndLocation(
			    constructorParam, constructorParam.GetLength(), false, defParam, Owner->List.GetFileSpecificEdCnt());
			if (typeAndLoc.first != "")
				res.push_back(typeAndLoc.first);
		}

		ConstructorCache.emplace(typeName, res);
		break;
	}
	}
	return res;
}

bool TypeListComparer::IsCastOperator(const WTString& def, int fileType)
{
	int opPos = ::FindWholeWordInCode(def, "operator", fileType, 0);
	if (opPos == -1)
		return false;

	for (int i = opPos + 8; i < def.GetLength(); i++)
	{
		if (IsWSorContinuation(def[i]))
			continue;
		if (ISCSYM(def[i]))
		{
			if (i + 6 < def.GetLength() && def[i] == 'd' && def[i + 1] == 'e' && def[i + 2] == 'l' &&
			    def[i + 3] == 'e' && def[i + 4] == 't' && def[i + 5] == 'e' && !ISCSYM(def[i + 6]))
				return false;
			if (i + 3 < def.GetLength() && def[i] == 'n' && def[i + 1] == 'e' && def[i + 2] == 'w' &&
			    !ISCSYM(def[i + 3]))
				return false;
			return true;
		}
		else
		{
			return false;
		}
	}

	return false;
}

bool TypeListComparer::IsCastConstructor(const WTString& def, const WTString& typeName, WTString& constructorParam,
                                         int ftype)
{
	WTString compactDef = UniformizeWhitespaces(def);
	WTString subStr = typeName + "(";
	int subPos = compactDef.find(subStr.c_str());
	if (subPos != -1)
	{ // this is a constructor. count the nr. of parameters
		WTString paramList;
		bool constFunc = false;
		OverloadResolver::GetParamListFromDef(def, typeName, paramList, constFunc, ftype);
		CommentSkipper cs(ftype);
		ParenSkipper ps(ftype);
		int parens = 0;
		for (int i = subPos + subStr.GetLength(); i < def.GetLength(); i++)
		{
			TCHAR c = def[i];
			if (cs.IsCode(c))
			{
				if (c == '(')
				{
					parens++;
					continue;
				}
				if (c == ')')
					break;
				if (parens)
					continue;
				constructorParam += c;
				if (!ps.IsInside(def, i) && c == ',') // more than one parameter - not a cast constructor
					return false;
			}
		}
		if (constructorParam.GetLength()) // empty constructor is not considered a casting constructor
			return true;
	}

	return false;
}
