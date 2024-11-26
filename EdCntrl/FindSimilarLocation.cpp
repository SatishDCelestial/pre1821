#include "StdAfxEd.h"
#include "FindSimilarLocation.h"
#include "WTString.h"
#include "EDCNT.H"
#include "VAParse.h"
#include "Settings.h"
#include "StringUtils.h"
#include "CommentSkipper.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

const int symbolsToConsider = FileOutlineFlags::ff_MethodsAndFunctions | FileOutlineFlags::ff_Globals |
                              FileOutlineFlags::ff_MembersAndVariables | FileOutlineFlags::ff_Enums |
                              FileOutlineFlags::ff_TypesAndClasses | FileOutlineFlags::ff_FwdDecl;

bool IsEndsWithOrBothEmpty(WTString str, WTString endsWith, bool freeFunc)
{
	if (freeFunc)
		return str == endsWith;

	int strLen = str.GetLength();
	int endsWithLen = endsWith.GetLength();
	if (strLen <= endsWithLen)
		return str == endsWith;
	if (endsWith == "")
		return true;

	return str.EndsWith(endsWith) &&
	       (str.ReverseFind(FSL_SEPARATOR_STR) == -1 || str[strLen - endsWithLen - 1] == FSL_SEPARATOR_CHAR);
}

bool IsSymbolToConsider(ULONG type)
{
	return /*type == CLASS ||*/ type == FUNC || type == VAR || type == C_ENUM || type == TYPE;
}

int FindSimilarLocation::WhereToPutImplementation(const WTString& targetBuf, const CStringW& targetFileName,
                                                  UnrealPostfixType unrealPostfixType /* = UnrealPostfixType::None */)
{
	if (!Psettings->mFindSimilarLocation)
		return -1;

	EdCntPtr ed(g_currentEdCnt);
	if (ed)
	{
		MultiParsePtr mparse = ed->GetParseDb();
		const uint kCurPos = ed->CurPos();
		const ULONG ln = TERROW(kCurPos);
		WTString sourceBuffer(ed->GetBuf(TRUE));

		LineMarkers sourceOutline;
		GetFileOutline(sourceBuffer, sourceOutline, mparse);

		LineMarkers targetOutline;
		GetFileOutline(targetBuf, targetOutline, mparse);

		std::vector<FlatItem> sourceFlatItems;
		// OutputDebugString("Declarations: \n");
		int index = -1;
		sResult qualification = GetQualificationViaOutline(ln, sourceOutline.Root());
		int prevComment = -1;
		BuildFlatDeclarationList(qualification, sourceFlatItems, sourceOutline.Root(), index, prevComment);
		// OutputDebugString("Implementations: \n");
		std::vector<FlatItem> targetFlatItems;
		int temp = -1;
		prevComment = -1;
		BuildFlatImplementationList(qualification, targetFlatItems, targetOutline.Root(), temp, prevComment);

		bool insertAfterOverloads = true;
		if (Psettings->mUnrealEngineCppSupport)
		{
			// [case: 111093] [case: 140912] treat *_Implementation and *_Validate methods as overloads to
			// avoid confusing FindSimilarLocationInTargetOutline when it tries to match sourceFlatItems to
			// targetFlatItems
			for (FlatItem& targetItem : targetFlatItems)
			{
				targetItem.FullName.Replace("_Implementation", "");
				targetItem.FullName.Replace("_Validate", "");
			}
			// [case: 111093] [case: 140912] ensure *_Implementation comes before *_Validation
			insertAfterOverloads = unrealPostfixType == UnrealPostfixType::Implementation ? false : true;
		}
		int similarLocation = FindSimilarLocationInTargetOutline(index, sourceFlatItems, targetFlatItems,
		                                                         ed->FileName() == targetFileName, insertAfterOverloads);
		if (similarLocation != -1)
			AmendLocationIfNeed(targetBuf, similarLocation); // case 88435

		return similarLocation;
	}

	return -1;
}

int FindSimilarLocation::WhereToPutDeclaration(const WTString& sourceBuf, uint curPos, const WTString& targetBuf)
{
	if (!Psettings->mFindSimilarLocation)
		return -1;

	EdCntPtr ed(g_currentEdCnt);
	if (ed)
	{
		MultiParsePtr mparse = ed->GetParseDb();
		const ULONG ln = TERROW(curPos);

		LineMarkers sourceOutline;
		GetFileOutline(sourceBuf, sourceOutline, mparse);

		LineMarkers targetOutline;
		GetFileOutline(targetBuf, targetOutline, mparse);

		std::vector<FlatItem> sourceFlatItems;
		// OutputDebugString("Declarations: \n");
		sResult qualification = GetQualificationViaOutline(ln, sourceOutline.Root());
		int index = -1;
		int prevComment = -1;
		BuildFlatImplementationList(qualification, sourceFlatItems, sourceOutline.Root(), index, prevComment);
		// OutputDebugString("Implementations: \n");
		std::vector<FlatItem> targetFlatItems;
		int temp = -1;
		prevComment = -1;
		BuildFlatDeclarationList(qualification, targetFlatItems, targetOutline.Root(), temp, prevComment);

		int similarLocation = FindSimilarLocationInTargetOutline(index, sourceFlatItems, targetFlatItems, false);
		if (similarLocation != -1)
			AmendLocationIfNeed(targetBuf, similarLocation); // case 88435

		return similarLocation;
	}

	return -1;
}

void FindSimilarLocation::BuildFlatImplementationList(sResult qualification, std::vector<FlatItem>& flatItems,
                                                      LineMarkers::Node& node, int& index, int& prevCommentStartLine,
                                                      WTString actQualification_ns /*= WTString()*/)
{
	for (size_t idx = 0; idx < node.GetChildCount(); ++idx)
	{
		LineMarkers::Node& ch = node.GetChild(idx);
		FileLineMarker& mkr = *ch;
		FileOutlineFlags::DisplayFlag displayFlag = static_cast<FileOutlineFlags::DisplayFlag>(mkr.mDisplayFlag);
		if (IsSymbolToConsider(mkr.mType) && !(displayFlag & FileOutlineFlags::ff_MethodsPseudoGroup))
		{ // method groups have a FUNC type
			// if (displayFlag & symbolsToConsider) {
			WTString fullName = mkr.mText;
			fullName = TokenGetField(fullName, "(");
			fullName.ReplaceAll("::", FSL_SEPARATOR_STR);
			fullName.ReplaceAll(".", FSL_SEPARATOR_STR);
			fullName = TokenGetField(fullName, "="); // for variable declarations
			fullName = TokenGetField(fullName, ";"); // for variable declarations
			fullName.TrimRight();
			WTString actQualification;
			int separator = fullName.ReverseFind(FSL_SEPARATOR_CHAR);
			if (separator != -1) // if there is no FSL_SEPARATOR_CHAR, "Qualification" stays empty
				actQualification = fullName.Left(separator);
			WTString actQualification_w_ns = actQualification_ns;
			if (actQualification_ns.GetLength() && actQualification.GetLength())
				actQualification_w_ns += FSL_SEPARATOR_STR;
			actQualification_w_ns += actQualification;

			if (actQualification_w_ns == qualification.Qualification_w_ns || fullName == "catch")
			{ // the scope is bad for catch. case 93234, VAAutoTest:CreateImplementationOrder2_03
				WTString name;
				if (separator != -1)
					name = fullName.Right(fullName.GetLength() - separator - 1);
				else
					name = fullName;
				if (name == qualification.Name)
					index = (int)flatItems.size();
				FlatItem flat(fullName, actQualification, actQualification_ns,
				              prevCommentStartLine >= 0 ? prevCommentStartLine : (int)mkr.mStartLine, (int)mkr.mEndLine,
				              separator == -1 ? iFreeFunction : iNone);
				flatItems.push_back(flat);
				// OutputDebugString(flat.FullName + " *** " + flat.Qualification + "\n");
			}
		}

		if (!(mkr.mDisplayFlag & (FileOutlineFlags::ff_MethodsPseudoGroup | FileOutlineFlags::ff_GlobalsPseudoGroup)))
			prevCommentStartLine = (displayFlag & FileOutlineFlags::ff_Comments) ? (int)mkr.mStartLine : -1;

		if (mkr.mType != CLASS)
		{
			WTString newQ = actQualification_ns;
			if (mkr.mType == NAMESPACE)
			{
				if (newQ != "")
					newQ += FSL_SEPARATOR_STR;
				WTString ns;
				WTString text = mkr.mText;
				CommentSkipper cs(Src);
				for (int i = 0; i < text.GetLength(); i++)
				{
					TCHAR c = text[i];
					if (cs.IsCode(c) && c != '/')
						ns += c;
				}
				ns.Trim();
				newQ += ns;
			}

			BuildFlatImplementationList(qualification, flatItems, ch, index, prevCommentStartLine, newQ);
		}
	}
}

bool FindSimilarLocation::BuildFlatDeclarationList(sResult qualification, std::vector<FlatItem>& flatItems,
                                                   LineMarkers::Node& node, int& index, int& prevCommentStartLine,
                                                   eLabel label /*= iNone*/,
                                                   WTString actQualification_w_ns /*= WTString()*/,
                                                   WTString actQualification_wo_ns /*= WTString()*/)
{
	for (size_t idx = 0; idx < node.GetChildCount(); ++idx)
	{
		LineMarkers::Node& ch = node.GetChild(idx);
		FileLineMarker& marker = *ch;
		FileOutlineFlags::DisplayFlag displayFlag = static_cast<FileOutlineFlags::DisplayFlag>(marker.mDisplayFlag);
		WTString currentName = marker.mText;
		currentName = TokenGetField(currentName, "(");
		currentName = TokenGetField(currentName, ":"); // for things like "VAScopeInfo : public DefFromLineParse"
		currentName = TokenGetField(currentName, ";"); // for variable declarations
		currentName.ReplaceAll(" final", "");
		currentName.TrimRight();
		if (IsSymbolToConsider(marker.mType) &&
		    IsEndsWithOrBothEmpty(actQualification_w_ns, qualification.Qualification_w_ns, false))
		{
			if (currentName == qualification.Name)
				index = (int)flatItems.size();
			WTString fullName;
			if (actQualification_wo_ns == "")
				fullName = currentName;
			else
				fullName = actQualification_wo_ns + FSL_SEPARATOR_STR + currentName;
			FlatItem flat(fullName, actQualification_wo_ns, "" /*actQualification_w_ns*/,
			              prevCommentStartLine >= 0 ? prevCommentStartLine : (int)marker.mStartLine,
			              (int)marker.mEndLine, label);
			flatItems.push_back(flat);
			// OutputDebugString(flat.FullName + " *** " + flat.Qualification + "\n");
		}

		WTString newQualification_w_ns =
		    GetExtendedQualification(actQualification_w_ns, marker, displayFlag, currentName, true);
		WTString newQualification_wo_ns =
		    GetExtendedQualification(actQualification_wo_ns, marker, displayFlag, currentName, false);

		eLabel newLabel = iNone;
		if (currentName == "public")
			newLabel = iPublic;
		if (currentName == "private")
			newLabel = iPrivate;
		if (currentName == "protected")
			newLabel = iProtected;
		if (currentName == "__published")
			newLabel = iPublished;

		if (!(marker.mDisplayFlag &
		      (FileOutlineFlags::ff_MethodsPseudoGroup | FileOutlineFlags::ff_GlobalsPseudoGroup)))
			prevCommentStartLine = (displayFlag & FileOutlineFlags::ff_Comments) ? (int)marker.mStartLine : -1;

		if (!BuildFlatDeclarationList(qualification, flatItems, ch, index, prevCommentStartLine, newLabel,
		                              newQualification_w_ns, newQualification_wo_ns))
			return false;
	}

	return true;
}

FindSimilarLocation::sResult FindSimilarLocation::GetQualificationViaOutline(
    ULONG ln, LineMarkers::Node& node, WTString actQualification_w_ns /*= WTString()*/,
    WTString actQualification_wo_ns /*= WTString()*/)
{
	for (size_t idx = 0; idx < node.GetChildCount(); ++idx)
	{
		LineMarkers::Node& ch = node.GetChild(idx);
		FileLineMarker& marker = *ch;
		FileOutlineFlags::DisplayFlag displayFlag = static_cast<FileOutlineFlags::DisplayFlag>(marker.mDisplayFlag);

		WTString currentName = marker.mText;
		currentName = TokenGetField(currentName, "(");
		currentName.ReplaceAll("::", FSL_SEPARATOR_STR);
		currentName.ReplaceAll(".", FSL_SEPARATOR_STR);
		currentName.ReplaceAll(" methods", "");
		currentName.ReplaceAll(" final", "");
		currentName = TokenGetField(currentName, ":"); // for things like "VAScopeInfo : public DefFromLineParse"
		currentName = TokenGetField(currentName, ";"); // for variable declarations
		currentName.TrimRight();

		if (IsSymbolToConsider(marker.mType) /*displayFlag & symbolsToConsider*/ &&
		    !(displayFlag & FileOutlineFlags::ff_MethodsPseudoGroup) && (int)ln >= marker.mStartLine &&
		    (int)ln <= marker.mEndLine - 1)
		{
			FileLineMarker& marker2 = *node;

			int separator = currentName.ReverseFind(FSL_SEPARATOR_CHAR);
			if (separator != -1)
				currentName = currentName.Right(currentName.GetLength() - separator - 1);

			return sResult(actQualification_w_ns, actQualification_wo_ns, currentName, (int)marker2.mStartLine,
			               (int)marker2.mEndLine - 1);
		}

		WTString newQualification_w_ns =
		    GetExtendedQualification(actQualification_w_ns, marker, displayFlag, currentName, true);
		WTString newQualification_wo_ns =
		    GetExtendedQualification(actQualification_wo_ns, marker, displayFlag, currentName, false);

		sResult res = GetQualificationViaOutline(ln, ch, newQualification_w_ns, newQualification_wo_ns);
		if (/*res.Qualification_w_ns.GetLength()*/ res.Name.GetLength()) // case 92499
			return res;
	}

	return sResult("", "", "", 0, 0);
}

WTString FindSimilarLocation::GetExtendedQualification(WTString& actQualification, FileLineMarker& marker,
                                                       FileOutlineFlags::DisplayFlag& displayFlag,
                                                       WTString& currentName, bool includeNamespaces)
{
	// VA Outline bug? namespace flag does not work
	WTString def = marker.mClassData.Def();
	def.TrimLeft();
	bool skipNamespace = false;
	if (!includeNamespaces && (marker.mText == "namespace" ||
	                           (def.GetLength() > 9 && def.Left(9) == "namespace" && IsWSorContinuation(def[9]))))
		skipNamespace = true;

	// we leave out namespaces from qualification deliberately, this is a design decision
	// ff_None is a VA outline bug? class in class results in ff_None
	if (!skipNamespace &&
	    (displayFlag == FileOutlineFlags::ff_None ||
	     displayFlag & (FileOutlineFlags::ff_TypesAndClasses | FileOutlineFlags::ff_MethodsPseudoGroup)))
	{
		if (actQualification.GetLength())
			return actQualification + FSL_SEPARATOR_STR + currentName;
		return currentName;
	}
	return actQualification;
}

bool FindSimilarLocation::FindSimilarInBlock(int& index, const std::vector<FlatItem>& source,
                                             const std::vector<FlatItem>& target, int& res, int from, int to,
                                             bool freeFunc)
{
	int maxDistance = std::max(index - from, to - index);
	for (int distance = 1; distance <= maxDistance; distance++)
	{
		// finding matching elements before
		int before = index - distance;
		if (before >= from)
		{
			WTString beforeName = source[(size_t)before].FullName;
			for (UINT j = 0; j < target.size(); j++)
			{
				WTString targetFullName = target[j].FullName;
				if (IsEndsWithOrBothEmpty(targetFullName, beforeName, freeFunc))
				{
					for (UINT k = j + 1; k < target.size(); k++)
					{ // find the last overload to put implementation / declaration after that
						if (beforeName == target[k].FullName)
							j++;
						else
							break;
					}

					// case 93234
					if (j + 1 < target.size())
					{
						if (target[j + 1].FullName == "catch")
							j++;
					}

					ImplNamespace = target[j].Qualification_impl_ns;
					res = (int)target[j].EndLine;
					return true;
				}
			}
		}

		// finding matching elements after
		int after = index + distance;
		if (after <= to)
		{
			WTString afterName = source[(size_t)after].FullName;
			for (UINT j = 0; j < target.size(); j++)
			{
				if (IsEndsWithOrBothEmpty(target[j].FullName, afterName, freeFunc))
				{
					ImplNamespace = target[j].Qualification_impl_ns;
					res = (int)target[j].StartLine;
					return true;
					// we put before the first overload of the method (if there are overloads)
				}
			}
		}
	}
	return false;
}

int FindSimilarLocation::FindSimilarLocationInTargetOutline(int index, const std::vector<FlatItem>& source,
                                                            const std::vector<FlatItem>& target, bool sameFile,
                                                            bool insertAfterOverloads)
{
	if (index < 0 || (unsigned int)index >= source.size())
		return -1;

	const FlatItem& indexSource = source[(size_t)index];
	// 	if (indexSource.FullName.Find('@') == -1 && sameFile)
	// 		return indexSource.EndLine;

	// we start from Index because we can find the same method name that we're going to create (with overloaded methods)
	if (index != -1)
	{
		for (UINT j = 0; j < target.size(); j++)
		{
			WTString sourceFullName = indexSource.FullName;
			if (sourceFullName == target[j].FullName)
			{
				if (insertAfterOverloads)
				{
					for (UINT k = j + 1; k < target.size(); k++)
					{ // find the last overload to put implementation / declaration after that
						WTString targetFullName = target[k].FullName;
						if (IsEndsWithOrBothEmpty(targetFullName, sourceFullName, false))
							j++;
						else
							break;
					}
				}
				ImplNamespace = target[j].Qualification_impl_ns;
				return int(insertAfterOverloads ? target[j].EndLine : target[j].StartLine);
			}
		}

		eLabel label = indexSource.Label;
		int firstItem = -1;
		for (int i = index; i >= 0; i--)
		{
			if (source[(size_t)i].Label != label)
				break;

			firstItem = i;
		}
		int lastItem = -1;
		for (int i = index; i < static_cast<int>(source.size()); i++)
		{
			if (source[(size_t)i].Label != label)
				break;

			lastItem = i;
		}

		// prefer same visibility label / group
		if (firstItem != lastItem)
		{
			int res;
			if (FindSimilarInBlock(index, source, target, res, firstItem, lastItem, label == iFreeFunction))
				return res;
		}

		// prefer same visibility label / group
		// increasing distance from Index
		int res;
		if (FindSimilarInBlock(index, source, target, res, 0, static_cast<int>(source.size()) - 1,
		                       label == iFreeFunction))
			return res;
	}

	return -1;
}

void FindSimilarLocation::AmendLocationIfNeed(const WTString& fileBuf, int& line)
{
	EdCntPtr ed(g_currentEdCnt);
	int offset = ed->GetBufIndex(fileBuf, ed->LineIndex(line));

	for (int i = offset; i < fileBuf.GetLength(); i++)
		if (!IsWSorContinuation(fileBuf[i]))
			return; // this isn't EOF

	for (int i = offset; i > 0; i--)
	{
		TCHAR c = fileBuf[i];
		if (c != 13 && c != 10 && c != 0)
		{
			offset = i;
			break;
		}
	}

	long l, c;
	ed->PosToLC(fileBuf, offset, l, c);
	line = l + 1;
}
