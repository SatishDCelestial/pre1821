#include "stdafxed.h"
#include "MethodsSorting.h"
#include "GetFileText.h"
#include <ranges>
#include <stack>
#include "StringUtils.h"
#include "VAParse.h"
#include "VaService.h"
#include "FILE.H"
#include "UnicodeHelper.h"
#include <iostream>
#include <algorithm>
#include <vector>
#include <stack>
#include <tuple>
#include "CommentSkipper.h"
#include "UndoContext.h"
#include "FreezeDisplay.h"
#include "PROJECT.H"
#include "DevShellService.h"
#include "SimpleDiff.h"

#ifdef _DEBUG
//#define DEBUG_STUFF
#endif

#pragma warning(disable : 4365)

enum class FindSymResult
{
	NotFound, 
	SingleMethod,
    Methods,
	Other
};

struct CodeChange
{
	ULONG oldPos;
	ULONG newPos;
	CodePart* part;
};

void ApplyChangesToString(std::vector<CodeChange> changes, const WTString& src, WTString& wtStr)
{
	_ASSERTE(&src != &wtStr);

	// first sort by original position from bottom to top...
	std::sort(changes.begin(), changes.end(), [](const CodeChange& ch1, const CodeChange& ch2) {
		return ch1.oldPos > ch2.oldPos;
	});

	// ... and delete old code from bottom to top....
	for (const CodeChange& ch : changes)
	{
		wtStr.remove(ch.oldPos, ch.part->GetLength());
	}

	// then sort by new position from top to bottom...
	std::sort(changes.begin(), changes.end(), [](const CodeChange& ch1, const CodeChange& ch2) {
		return ch1.newPos < ch2.newPos;
	});

	// ... and insert new code from top to bottom...
	for (const CodeChange& ch : changes)
	{
		wtStr.insert(ch.newPos, ch.part->GetCode(src).c_str());
	}
}

static FindSymResult FindSymbol(const FileLineMarker& mrk, const CStringW& symWide)
{
	auto *symStart = strstrWholeWord((LPCWSTR)mrk.mText, (LPCWSTR)symWide);

	if (symStart)
	{
		auto *next = symStart + symWide.GetLength();

		if (next[0] == L':' && next[1] == L':')
			return FindSymResult::SingleMethod;

		if (EndsWith(mrk.mText, L" methods"))
			return FindSymResult::Methods;

		return FindSymResult::Other;
	}

	return FindSymResult::NotFound;
}

#ifdef DEBUG_STUFF
static ULONG64 SumOfSignificantChars(const WTString & str)
{
	const auto *pStr = (const char8_t *)str.c_str();
	ULONG64 sum = 0;
	char32_t ch;

	while (pStr && *pStr)
	{
		pStr = UnicodeHelper::ReadUtf8(pStr, ch);

		if (!UnicodeHelper::IsWhiteSpace(ch))
			sum += ch;
	}

	return sum;
}
#endif // DEBUG_STUFF

static uint EditDistance(const CStringW& str1, const CStringW& str2)
{
	uint size1 = (uint)str1.GetLength();
	uint size2 = (uint)str2.GetLength();

	if (size1 == 0)
		return size2;

	if (size2 == 0)
		return size1;

	std::vector<std::vector<uint>> verif(size1 + 1, std::vector<uint>(size2 + 1, 0));

	for (uint i = 0; i <= size1; i++)
		verif[i][0] = i;

	for (uint j = 0; j <= size2; j++)
		verif[0][j] = j;

	for (uint i = 1; i <= size1; i++)
	{
		for (uint j = 1; j <= size2; j++)
		{
			uint cost = (str2[(int)(j - 1)] == str1[(int)(i - 1)]) ? 0u : 1u;
			verif[i][j] = std::min({verif[i - 1][j] + 1, verif[i][j - 1] + 1, verif[i - 1][j - 1] + cost});
		}
	}

	return verif[size1][size2];
}

static CStringW& GetOppositeFile(const CStringW& fileName)
{
	// cache the opposite file so we don't need lookup twice

	static UINT latestFileId = 0;
	static CStringW oppositeFile;

	UINT fid = gFileIdManager ? gFileIdManager->GetFileId(fileName) : 0;

	if (fid && latestFileId == fid)
	{
		return oppositeFile;
	}

	latestFileId = fid;

	oppositeFile.Empty();

	CStringW baseName = ::GetBaseNameNoExt(fileName);

	FileList matches;
	::GetBestFileMatch(fileName, matches);

	for (const auto& fileinfo : matches)
	{
		if (fileinfo.mFilename.IsEmpty())
			continue;

		CStringW curBaseName = ::GetBaseNameNoExt(fileinfo.mFilename);
		if (0 == baseName.CompareNoCase(curBaseName))
		{
			oppositeFile = fileinfo.mFilename;
			return oppositeFile;
		}
	}

	for (const auto& fileinfo : matches)
	{
		if (fileinfo.mFilename.IsEmpty())
			continue;

		oppositeFile = fileinfo.mFilename;
		break;
	}

	return oppositeFile;
}

const ULONG WANTED_FLAGS = FileOutlineFlags::ff_MethodsAndFunctions |
                           FileOutlineFlags::ff_Namespaces |
                           FileOutlineFlags::ff_Preprocessor |
                           FileOutlineFlags::ff_TypesAndClasses;

static bool DisplayFlagsAreCompatible(const CodePart* part1, const CodePart* part2)
{
	return (part1->pMarker->mDisplayFlag & WANTED_FLAGS) &
	       (part2->pMarker->mDisplayFlag & WANTED_FLAGS);
}


static CodePart* FindBestCandidate(std::vector<std::unique_ptr<CodePart>>& parts, CodePart* target, std::function<bool(const CDiff&)> diffFunc = nullptr)
{
	// items sorted by edit distances
	std::map<int, std::vector<CodePart*>> items;

	CStringW targetMethName = target->GetMethodName();
	CStringW targetDef = target->GetDef(CodePart::StripMask_None);

	for (const auto & part : parts)
	{		
		if (!DisplayFlagsAreCompatible(target, part.get()))
			continue;

		if (!targetMethName.IsEmpty())
		{
			CStringW curMethName = part->GetMethodName();
			if (targetMethName.Compare(curMethName) != 0)
			{
				continue;
			}
		}

		CStringW curSym = part->GetDef(CodePart::StripMask_None);

		int editDist = EditDistance(targetDef, curSym);

		if (editDist == 0)
			return part.get();

		items[editDist].emplace_back(part.get());
	}

	if (!items.empty())
	{
		size_t minDist = UINT_MAX;
		CodePart* minDistPart = nullptr;

		constexpr auto getMask = [](int i) {
			CodePart::StripMask mask = CodePart::StripMask_None;

			if (i > 0)
			{
				mask = (CodePart::StripMask)(mask | CodePart::StripMask_ToEssentials);

				if (i > 1)
					mask = (CodePart::StripMask)(mask | CodePart::StripMask_RemoveScopes);
			}

			return mask;
		};

		for (int i = 0; i < 3; i++)
		{
			CodePart::StripMask mask = getMask(i);

			// GetDef result is being cached in the part
			targetDef = target->GetDef(mask);

			for (const auto& kvp : items)
			{
				for (auto* part : kvp.second)
				{
					// GetDef is cached in the part
					CStringW curSym = part->GetDef(mask);

					// this pass allows only exact match
					if (curSym.Compare(targetDef) == 0)
						return part;

					if (!diffFunc)
					{
						uint editDist = EditDistance(targetDef, curSym);
						if (editDist < minDist)
						{
							minDist = editDist;
							minDistPart = part;
						}
					}
				}
			}
		}

		if (diffFunc)
		{
			minDist = UINT_MAX;
			minDistPart = nullptr;

			// defs are now cached, so this is much faster pass than previous
			for (int i = 0; i < 3; i++)
			{
				CodePart::StripMask mask = getMask(i);

				// GetDef is cached in the part
				targetDef = target->GetDef(mask);

				// Items go from lowest edit distance to highest
				for (const auto& kvp : items)
				{
					for (auto* part : kvp.second)
					{
						// GetDef result is cached in the part
						CStringW curSym = part->GetDef(mask);

						// this pass uses diff using longest common sequence
						CDiff diff(targetDef, curSym, false);

						if (diffFunc(diff))
							return part;

						size_t editDist = diff.EditDistance();
						if (editDist < minDist)
						{
							minDist = editDist;
							minDistPart = part;
						}
					}
				}
			}
		}

		// just return one with lowest edit distance

		if (minDistPart)
			return minDistPart;

		return items.begin()->second.front();
	}

	return nullptr;
}


void SaveFile(const WTString& test, WTString path)
{
	if (IsFile(path.Wide()))
		DeleteFile(path.c_str());

	HANDLE hFile = CreateFile(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
	if (hFile != INVALID_HANDLE_VALUE)
	{
		DWORD dwWritten; // number of bytes written to file
		WriteFile(hFile, (LPCVOID)test.c_str(), (DWORD)test.GetLength(), &dwWritten, 0);
		CloseHandle(hFile);
	}
}

static bool IsGroupMarker(const FileLineMarker& marker)
{
	if (marker.mText.IsEmpty())
		return false;

	if (marker.mText[marker.mText.GetLength() - 1] == 's')
	{
		if (marker.mText[0] == '#')
			return !marker.mText.Compare(L"#defines") ||
			       !marker.mText.Compare(L"#includes");

		return 
		    !marker.mText.Compare(L"Includes") ||
		    !marker.mText.Compare(L"Forward declarations") ||
		    !marker.mText.Compare(L"File scope variables") ||
		    EndsWith(marker.mText, L" methods");
	}

	return 0 == marker.mText.Compare(L"using");
}

bool MethodsSorting::BuildHierarchy(WTString& buffer, uint fileId, int ftype, LineMarkers& markers, std::vector<std::unique_ptr<CodePart>>& parts, MultiParsePtr mparse)
{
	if (!IsCFile(ftype))
		return false;

	// it could be tuple, but this is easier to understand
	struct node_item
	{
		CodePart* parent;
		LineMarkers::Node* node;
		node_item(CodePart* p, LineMarkers::Node* n)
		    : parent(p), node(n)
		{
		}
	};

	GetFileOutline(buffer, markers, mparse);

	std::stack<node_item> stack;
	stack.emplace(nullptr, &markers.Root());

	CodePart* root = nullptr;

	if (fileId == src_file_id)
	{
		src_root = std::make_unique<CodePart>(&markers.Root().Contents(), nullptr, ftype, ++nextId);
		root = src_root.get();
	}

	int itersLeft = std::max(5000, buffer.GetLength() / 5);

	// form the hierarchy
	while (!stack.empty())
	{
		if (--itersLeft <= 0)
			return false;

		node_item item = stack.top();
		stack.pop();
		
		auto& marker = item.node->Contents();
		CodePart* parent = root;

		if (!marker.mText.IsEmpty() && !IsGroupMarker(marker))
		{
			parts.push_back(std::make_unique<CodePart>((FileLineMarker*)&marker, item.parent, ftype, ++nextId));
			parent = parts.back().get();

			if (FindSymbol(marker, symWide) == FindSymResult::SingleMethod)
			{
				parts.back()->is_target_sym = true;
			}
		}
		else
		{
			parent = item.parent;

			if (parent == nullptr)
				parent = root;
		}

		for (size_t ch = 0; ch < item.node->GetChildCount(); ch++)
		{
			auto& child = item.node->GetChild(ch);
			stack.emplace(parent, &child);
		}
	}

	// following applied only for the source file
	if (fileId == src_file_id)
	{
		// be sure to have children sorted
		src_root->SortChildren(CodePart::IsLessDefault);

		// find the top comment, such as license or other type of header
		// if we find such comment, we don't move it, it must remain on place
		CodePart* firstComment = nullptr;
		src_root->ForEachChild([&](CodePart* ch) {
			if (ch->IsComment())
			{
				firstComment = ch;
				return false;
			}
			return true;
		});

		// if the first found comment isnt directly in root, ignore it
		if (firstComment->pParent != src_root.get())
			firstComment = nullptr;

		// merge comments to children and so on, fix lengths, extents... 
		src_root->FixChildren(src_buffer, &src_parts, firstComment, nextId);

		// cleanup after fix operation => erase invalidated parts
		for (long i = (long)src_parts.size() - 1; i >= 0; --i)
		{
			if (!src_parts[(size_t)i]->pMarker)
			{
				src_parts.erase(src_parts.begin() + (size_t)i);
			}
		}

#ifdef DEBUG_STUFF
		for (auto& up : src_parts)
		{
			WTString mid = src_buffer.Mid((size_t)up->pMarker->mStartCp, up->GetLength());
			WTString code = up->GetCode(src_buffer);

			auto cs1 = SumOfSignificantChars(mid);
			auto cs2 = SumOfSignificantChars(code);

			ASSERT(cs1 == cs2);
		}

		WTString test = src_root->GetCode(src_buffer, true);
		SaveFile(test, "e:\\VADBG.cpp");

#endif // DEBUG_STUFF
	}

	return true;
}

bool MethodsSorting::CanSortMethods(EdCntPtr ed, const DType* dt)
{
	dt = ResolveDType(ed, dt);

	if (!dt)
		return false;

	CStringW fileName = dt->FilePath();
	int fileType = GetFileTypeByExtension(fileName);

	if (!IsCFile(fileType))
		return false;

	CStringW oppositeFile(GetOppositeFile(fileName));

	if (oppositeFile.IsEmpty())
		return false;

	return true;
}

bool MethodsSorting::SortMethods(EdCntPtr ed, const DType* dt)
{
	if (!dt)
		dt = ResolveDType(ed, dt);

	if (dt)
	{
		CStringW errorMsg;
		MethodsSorting methSort;
		StopWatch total;
		if (methSort.Init(ed, dt, errorMsg))
		{
			methSort.Apply(total);
			return true;
		}	
	}

	return false;
}

bool MethodsSorting::Init(EdCntPtr ed, const DType* dt, CStringW& errorMessage)
{
	if (dt && ed && gFileIdManager)
	{
		CStringW fileName = dt->FilePath();
		int fileType = GetFileTypeByExtension(fileName);

		if (!IsCFile(fileType))
			return false;

		CStringW oppositeFile(GetOppositeFile(fileName));

		if (oppositeFile.IsEmpty())
		{
			errorMessage = L"Unable to identify corresponding header file.";
			return false;
		}

		{
			// init header file data

			auto mparse = MultiParse::Create(Header);
			hdr_file_name = fileType == Src ? oppositeFile : fileName;
			hdr_file_id = gFileIdManager->GetFileId(hdr_file_name);
			hdr_buffer = PkgGetFileTextW(hdr_file_name);
			BuildHierarchy(hdr_buffer, hdr_file_id, Header, hdr_markers, hdr_parts, mparse);
		}

		{
			// init source file data

			auto mparse = MultiParse::Create(Src);
			src_file_name = fileType == Src ? fileName : oppositeFile;
			src_file_id = gFileIdManager->GetFileId(src_file_name);
			src_buffer = PkgGetFileTextW(src_file_name);
			BuildHierarchy(src_buffer, src_file_id, Src, src_markers, src_parts, mparse);
		}

		if (!ResolveCorresponding())
		{
			errorMessage = L"Unable to identify corresponding symbols in header file.";
			return false;
		}

		if (!ResolveDependencies())
		{
			errorMessage = L"Failed to resolve dependencies.";
			return false;
		}

		return true;
	}

	errorMessage = L"Unsupported context.";
	return false;
}

static bool HandleDiff(const CDiff& diff)
{
	if (diff.EditDistance() == 0)
		return true;

	auto isDiffAllowed = [](const CDiff::Change& span) {
		auto trimmed = span.GetContent();
		trimmed.Trim();

		if (trimmed.IsEmpty())
			return true;

		return trimmed.Find(L"::") >= 0;
	};

	for (const auto& str : diff.m_inserts)
		if (!isDiffAllowed(str))
			return false;

	for (const auto& str : diff.m_removes)
		if (!isDiffAllowed(str))
			return false;

	if (diff.m_lcs.Compare(*diff.m_source) == 0)
		return true;

	if (diff.m_lcs.Compare(*diff.m_target) == 0)
		return true;

	return false;
}

bool MethodsSorting::ResolveCorresponding()
{
	StopWatch sw;

	int resolvedCount = 0;

	for (std::unique_ptr<CodePart>& srcPart : src_parts)
	{
		// only find corresponding for refactoring symbol methods
		if (!srcPart->is_target_sym)
			continue;

		auto* best_candidate = FindBestCandidate(hdr_parts, srcPart.get(), HandleDiff);

		if (best_candidate)
		{
			srcPart->pCorresponding = best_candidate;
			resolvedCount++;
		}
	}

	for (std::unique_ptr<CodePart>& srcPart : src_parts)
	{
		if (srcPart->ContainsTargetSym())
		{
			srcPart->is_target_sym = true;
			srcPart->GetTopCorresponding(srcPart->pCorresponding);
			srcPart->GetBottomCorresponding(srcPart->pCorrespondingEnd);
		}
	}

	statusStr.AppendFormat(L"ResolveCorresponding: %lld[ms]\r\n", sw.ElapsedMilliseconds());
	return resolvedCount > 0;
}

bool MethodsSorting::ResolveDependencies()
{
	// Anything except comments that is higher than this method and is not a method is considered to be a dependency. 
	// When moving this method upwards, we move all the dependencies to position before this method. 
	// Every dependency must be higher than any of its dependants.

	StopWatch sw;

	std::vector<CodePart*> sortedParts(src_parts.size(), nullptr);

	for (size_t i = 0; i < src_parts.size(); i++)
		sortedParts[i] = src_parts[i].get();

	std::sort(sortedParts.begin(), sortedParts.end(), CodePart::IsLessDefault);

	statusStr.AppendFormat(L"ResolveDependencies sort: %lld[ms]\r\n", sw.ElapsedMilliseconds());
	sw.Restart();

	//*****************************
	// dependencies gathering pass

	for (size_t i = 0; i < sortedParts.size(); i++)
	{
		auto* bottom = sortedParts[i];

		// if this is not symbol of refactoring class,
		// we don't need to know its dependencies
		// because we will not move it at all

		if (!bottom->is_target_sym)
			continue;

		for (int j = (int)i - 1; j >= 0; --j)
		{
			auto* top = sortedParts[(size_t)j];

			// if parent of bottom is under the top, break this loop
			if (bottom->pParent && (bottom->pParent->pMarker->mStartCp >= top->pMarker->mEndCp))
				break;

			if (top->pParent != bottom->pParent)
				continue;

			if (!top->is_target_sym)
			{
				bottom->dependencies.emplace(top);
			}
		}
	}

	statusStr.AppendFormat(L"ResolveDependencies pass 1: %lld[ms]\r\n", sw.ElapsedMilliseconds());
	sw.Restart();

	//*******************************
	// dependencies distribution pass

	for (CodePart* part1 : sortedParts)
	{
		if (!part1->is_target_sym)
			continue;

		for (CodePart* part2 : sortedParts)
		{
			if (part1 == part2)
				continue;

			if (part1->pParent != part2->pParent)
				continue;

			if (!part2->is_target_sym)
				continue;

			if (part1->CompareCorresponding(part2) < 0)
			{
				part1->dependencies.insert_range(part2->dependencies);
			}
		}
	}

	statusStr.AppendFormat(L"ResolveDependencies pass 2: %lld[ms]\r\n", sw.ElapsedMilliseconds());

	return true;
}

void MethodsSorting::Apply(StopWatch& total)
{
	StopWatch sw;
	src_root->SortChildren(CodePart::IsLessByHeader);

	statusStr.AppendFormat(L"Apply - SortChildren: %lld[ms]\r\n", sw.ElapsedMilliseconds());
	sw.Restart();

#ifdef DEBUG_STUFF
	auto checkSum = SumOfSignificantChars(src_buffer);
#endif // DEBUG_STUFF
	{
		std::vector<CodeChange> changes;
		for (auto* part : src_root->children)
		{
			if (part->ContainsChanges())
			{
				ULONG oldPos = part->GetStart(false);
				ULONG curPos = part->GetStart(true);
				changes.emplace_back(oldPos, curPos, part);
			}
		}

		statusStr.AppendFormat(L"Apply - Changes: %lld[ms]\r\n", sw.ElapsedMilliseconds());
		sw.Restart();

		if (changes.empty())
		{
			::WtMessageBox("Methods seem to be already sorted well.", "VA Sorting by header file");
		}
		else
		{
#ifdef DEBUG_STUFF
			WTString test = src_root->GetCode(src_buffer, true);
			SaveFile(test, "e:\\VADBG1.cpp");

			WTString srcCopy(src_buffer);
			ApplyChangesToString(changes, src_buffer, srcCopy);

			SaveFile(srcCopy, "e:\\VADBG2.cpp");

			auto checkSum2 = SumOfSignificantChars(srcCopy);

			if (checkSum != checkSum2)
			{
				::WtMessageBox("Checksum2 is invalid!", "VA Sort methods by header file");
			}
#endif // DEBUG_STUFF

			sw.Restart();

			EdCntPtr ed(g_currentEdCnt);
			ULONG initialFirstVisLine = ed->GetFirstVisibleLine();

			UndoContext undoContext("VA Sort methods by header file");

			std::unique_ptr<TerNoScroll> ns;
			if (gShellAttr->IsMsdev())
				ns = std::make_unique<TerNoScroll>(ed.get());

			FreezeDisplay _f;

			ed = DelayFileOpen(src_file_name);
			if (ed && gFileIdManager->GetFileId(ed->FileName()) == src_file_id)
			{
				_f.ReadOnlyCheck();

// 				CComPtr<IVsTextLines> textLines;
// 				if (ed->m_IVsTextView && SUCCEEDED(ed->m_IVsTextView->GetBuffer(&textLines)))
// 				{
// 					bool failed = false;
// 
// 					// first sort by original position from bottom to top...
// 					std::sort(changes.begin(), changes.end(), [](const CodeChange& ch1, const CodeChange& ch2) {
// 						return ch1.oldPos > ch2.oldPos;
// 					});
// 
// 					// ... and delete old code from bottom to top....
// 					for (const CodeChange& ch : changes)
// 					{
// 						TextSpan changed;
// 						long startLine, endLine;
// 						CharIndex startIndex, endIndex;
// 
// 						if (SUCCEEDED(textLines->GetLineIndexOfPosition(ch.oldPos, &startLine, &startIndex)) &&
// 							SUCCEEDED(textLines->GetLineIndexOfPosition(ch.oldPos + ch.part->GetLength(), &endLine, &endIndex)))
// 						{
// 							if (!SUCCEEDED(textLines->ReplaceLines(startLine, startIndex, endLine, endIndex, L"", 0, &changed)))
// 							{
// 								failed = true;
// 								break;
// 							}
// 						}
// 					}
// 
// 					if (!failed)
// 					{
// 						// then sort by new position from top to bottom...
// 						std::sort(changes.begin(), changes.end(), [](const CodeChange& ch1, const CodeChange& ch2) {
// 							return ch1.newPos < ch2.newPos;
// 						});
// 
// 						// ... and insert new code from top to bottom...
// 						for (const CodeChange& ch : changes)
// 						{
// 							TextSpan changed;
// 							long startLine;
// 							CharIndex startIndex;
// 							if (SUCCEEDED(textLines->GetLineIndexOfPosition(ch.newPos, &startLine, &startIndex)))
// 							{
// 								CStringW wstr;
// 								ch.part->GetCode(src_buffer).GetWide(wstr);
// 								if (!SUCCEEDED(textLines->ReplaceLines(startLine, startIndex, startLine, startIndex, wstr, wstr.GetLength(), &changed)))
// 								{
// 									failed = true;
// 									break;
// 								}
// 							}
// 						}
// 					}
// 				}
// 				else
				{
					WTString srcCopy(src_buffer);
					ApplyChangesToString(changes, src_buffer, srcCopy);
					ed->SetSel(0, src_buffer.GetLength());
					ed->ReplaceSel(srcCopy.c_str(), noFormat);
				}

				if (gShellAttr->IsDevenv())
				{
					ulong topPos = ed->LinePos(initialFirstVisLine + 1);
					if (-1 != topPos && gShellSvc)
					{
						ed->SetSel(topPos, topPos);
						gShellSvc->ScrollLineToTop();
					}
					_f.OffsetLine(1);
				}
				else
				{
					_ASSERTE(ns.get());
					_f.LeaveCaretHere();
					ns->OffsetLine(1);
				}
			}

			statusStr.AppendFormat(L"Apply - Edits in editor: %lld[ms]\r\n", sw.ElapsedMilliseconds());
			statusStr.Append(L"-----------------------------\r\n");
			statusStr.AppendFormat(L"Total time: %lld[ms]\r\n", total.ElapsedMilliseconds());

#ifdef DEBUG_STUFF
			::WtMessageBox(statusStr, L"Measures");
#endif
		}
	}

// 	std::vector<Line> src_lines;
// 	std::vector<Line> rslt_lines;
// 
// 	WTString target_buffer = src_root->GetCode(src_buffer);
// 
// 	LineReader::ReadLines(src_buffer, src_lines);
// 	LineReader::ReadLines(target_buffer, rslt_lines);
// 
// 	CLineDiffWT lineDiff(src_lines, rslt_lines, false);

// 	EdCntPtr ed(DelayFileOpen(src_file_name));
// 
// 	if (ed && gFileIdManager->GetFileId(ed->FileName()) == src_file_id)
// 	{
// 		FreezeDisplay _f;
// 		UndoContext undoContext("VA Sort methods by header file");
// 
// 		lineDiff.Apply([&](const CLineDiffWT::CodeChange& change) {
// 			if (change.IsRemove())
// 			{
// 				auto content = change.GetContent();
// 
// 				ed->SetSel(
// 				    content.front().sp,
// 				    content.back().sp + content.back().len);
// 				ed->Insert("");
// 			}
// 			else
// 			{
// 				auto content = change.GetContent();
// 
// 				WTString str;
// 				for (auto& line : content)
// 					str += line.GetText();
// 
// 				ed->SetPos(content.front().sp);
// 				ed->Insert(str.c_str());
// 			}
// 		});
// 	}
}

const DType* MethodsSorting::ResolveDType(EdCntPtr ed, const DType* dt)
{
	if (!ed || !gFileIdManager || !gVaService)
		return nullptr;

	if (!GlobalProject || GlobalProject->IsBusy())
		return nullptr;

	if (!dt)
	{
		MultiParsePtr mp(ed->GetParseDb());
		WTString ctx = ed->m_lastScope;
		while (ctx.GetLength())
		{
			dt = mp->FindExact(ctx);
			if (dt && dt->IsType() && dt->MaskedType() != NAMESPACE)
			{
				break;
			}
			ctx = StrGetSymScope(ctx);
		}
	}

	if (dt && (!dt->IsType() || dt->MaskedType() == NAMESPACE))
	{
		MultiParsePtr mp2(ed->GetParseDb());
		dt = mp2->FindExact(dt->Scope());
		if (!dt || !dt->IsType() || dt->MaskedType() == NAMESPACE)
		{
			return nullptr;
		}
	}

	return dt;
}



