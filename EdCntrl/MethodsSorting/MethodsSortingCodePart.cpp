#include "stdafxed.h"
#include "MethodsSortingCodePart.h"
#include "SimpleDiff.h"
#include <memory>
#include "CommentSkipper.h"
#include "FileTypes.h"

#ifdef _DEBUG
// #define DEBUG_STUFF
#endif

#pragma warning(disable : 4365)

class CommentSkipperW : public CommentSkipper
{
  public:
	CommentSkipperW(int ftype = Src)
	    : CommentSkipper(ftype)
	{
	}

	bool IsCodeW(wchar_t ch)
	{
		if (ch <= 0xc0)
			return IsCode((TCHAR)ch);

		return IsCode(' ');
	}
};

static bool DirectiveTest(const CStringW& text, LPCWSTR startStr, LPCWSTR pragmaStr)
{
	bool hash = false;
	bool pragma = false;

	CommentSkipperW cs;

	for (LPCWSTR wstr = text; wstr && *wstr; wstr++)
	{
		if (!cs.IsCodeW(*wstr))
			continue;

		if (!wt_isspace(*wstr))
		{
			if (*wstr == '#')
			{
				hash = true;
				continue;
			}

			if (!hash)
				return false;

			if (!pragma)
			{
				if (StartsWith(wstr, startStr, TRUE, true))
					return true;

				if (StartsWith(wstr, L"pragma", TRUE, true))
				{
					pragma = true;
					wstr += 6;
					continue;
				}

				return false;
			}

			return StartsWith(wstr, pragmaStr, TRUE, true);
		}
	}

	return false;
}

 CodePart::CodePart(FileLineMarker* marker, CodePart* parent, int ftype, UINT id)
    : pMarker(marker), pParent(parent), pCorresponding(nullptr), pCorrespondingEnd(nullptr), mFtype(ftype), is_target_sym(false), id(id), codePos(marker->mStartCp)
{
	if (parent)
		parent->children.emplace_back(this);
}

CodePart::CodePart(ULONG startLine, ULONG startCp, ULONG endLine, ULONG endCp, ULONG type, ULONG attrs, ULONG displayFlags, CodePart* parent, int ftype, UINT id)
    : pMarker(nullptr), pParent(parent), pCorresponding(nullptr), pCorrespondingEnd(nullptr), mFtype(ftype), is_target_sym(false), id(id), codePos(startCp)
{
	CStringW patch(L"@PATCH");

	patch_marker = std::make_unique<FileLineMarker>(
	    patch, patch, startLine, startCp, endLine, endCp,
	    type, attrs, displayFlags, DType(), false);

	pMarker = patch_marker.get();

	if (parent)
		parent->children.emplace_back(this);
}

int CodePart::FixChildren(const WTString& buffer, std::vector<std::unique_ptr<CodePart>>* storage, const CodePart* headerComment, UINT& nextId)
{
	int count = 0;
	bool doFixes = storage != nullptr;

	for (size_t i = 0; i < children.size(); i++)
	{
		if (!children[i]->pMarker)
			continue;

		if (storage != nullptr)
		{
			// merge preceding comments with blocks

			while (i + 1 < children.size() && children[i]->IsComment() && children[i] != headerComment)
			{
				FileLineMarker* src = children[i]->pMarker;
				FileLineMarker* dst = children[i + 1]->pMarker;

				dst->mStartCp = std::min(src->mStartCp, dst->mStartCp);
				dst->mStartLine = std::min(src->mStartLine, dst->mStartLine);
				dst->mEndCp = std::max(src->mEndCp, dst->mEndCp);
				dst->mEndLine = std::max(src->mEndLine, dst->mEndLine);

				children[i]->pMarker = nullptr;
				children.erase(children.begin() + i);

				// next item takes index i
			}
		}

		bool invalid = false;

		if (i + 1 < children.size() &&
		    children[i]->pMarker->mEndCp != children[i + 1]->pMarker->mStartCp)
		{
			invalid = true;

			// ASSERT(!"mEndCp != next->mStartCp");

			if (doFixes)
			{
				bool is_comment = true;

				CommentSkipper cs(Src);
				for (ULONG x = children[i]->pMarker->mEndCp;
				     x < children[i + 1]->pMarker->mStartCp; x++)
				{
					if (cs.IsCode(buffer[(int)x]))
					{
						is_comment = false;
						break;
					}
				}

				if (is_comment)
					children[i + 1]->pMarker->mStartCp = children[i]->pMarker->mEndCp;
				else
				{
					const auto* pm = children[i]->pMarker;

					ULONG sl = children[i]->pMarker->mEndLine;
					ULONG sp = children[i]->pMarker->mEndCp;

					ULONG el = children[i + 1]->pMarker->mStartLine;
					ULONG ep = children[i + 1]->pMarker->mStartCp;

					storage->push_back(std::make_unique<CodePart>(
					    sl, sp, el, ep,
					    pm->mType, pm->mAttrs, pm->mDisplayFlag,
					    pParent, Src, ++nextId));

					WTString patch = buffer.Mid((size_t)sp, (size_t)(ep - sp));
					storage->back()->pMarker->mText = TokenGetField(patch, "\r\n").Wide();
				}
			}
		}

		if (children[i]->pMarker->mStartCp < pMarker->mStartCp)
		{
			invalid = true;

			// ASSERT(!"child->mStartCp < mStartCp");

			if (doFixes)
			{
				children[i]->pMarker->mStartCp = pMarker->mStartCp;
			}
		}

		if (children[i]->pMarker->mEndCp > pMarker->mEndCp)
		{
			invalid = true;

			// this is happening very often, child ends out of the parent
			// now we need to find out which one is correct

			// ASSERT(!"child->mEndCp > mEndCp");

			if (doFixes)
			{
				bool childExceeds = children[i]->pMarker->mEndCp > (ULONG)buffer.GetLength();
				bool thisExceeds = pMarker->mEndCp > (ULONG)buffer.GetLength();

				if (childExceeds && !thisExceeds)
				{
					children[i]->pMarker->mEndCp = pMarker->mEndCp;
					children[i]->pMarker->mEndLine = pMarker->mEndLine;
				}
				else if (childExceeds && thisExceeds)
				{
					pMarker->mEndCp = (ULONG)buffer.GetLength();
					children[i]->pMarker->mEndCp = (ULONG)buffer.GetLength();

					// count lines to the end of file
					long endLine = LineReader::CountLines(
					                   buffer.c_str(), children[i]->pMarker->mStartCp) +
					               children[i]->pMarker->mStartLine;

					pMarker->mEndLine = (ULONG)endLine;
					children[i]->pMarker->mEndLine = (ULONG)endLine;
				}
				else
				{
					children[i]->pMarker->mEndCp = pMarker->mEndCp;
				}
			}
		}

		if (invalid)
			count++;

		count += children[i]->FixChildren(buffer, storage, headerComment, nextId);
	}

	return count;
}

ULONG CodePart::GetLocalStart(bool actual) const
{
	if (!pParent)
	{
		_ASSERT(pMarker->mStartCp == 0);
		return pMarker->mStartCp;
	}

	if (!actual)
		return pMarker->mStartCp - pParent->pMarker->mStartCp;

	bool countOffset = false;         // start counting offset from this
	ULONG offset = 0;                 // position offset of this from beginning of siblings
	ULONG siblings_start = ULONG_MAX; // start parent's children (including this)

	for (int i = (int)pParent->children.size() - 1; i >= 0; i--)
	{
		const CodePart* part = pParent->children[(size_t)i];

		if (countOffset)
			offset += part->GetLength();

		if (!countOffset && part == this)
			countOffset = true;

		siblings_start = std::min(siblings_start, part->pMarker->mStartCp);
	}

	// make the minimum child offset local
	siblings_start -= pParent->pMarker->mStartCp;

	return siblings_start + offset;
}

const CodePart* CodePart::TopParent() const
{
	if (pParent)
		return pParent->TopParent();

	return this;
}

bool CodePart::ContainsTargetSym() const
{
	if (is_target_sym)
		return true;

	for (CodePart* ch : children)
		if (ch->ContainsTargetSym())
			return true;

	return false;
}

bool CodePart::GetTopCorresponding(CodePart*& pPart) const
{
	int changes = 0;

	if (pCorresponding)
	{
		if (!pPart || IsLessDefault(pCorresponding, pPart))
		{
			pPart = pCorresponding;
			changes++;
		}
	}
	else
	{
		for (CodePart* part : children)
			if (part->GetTopCorresponding(pPart))
				changes++;
	}

	return !!changes;
}

bool CodePart::GetBottomCorresponding(CodePart*& pPart) const
{
	int changes = 0;

	CodePart* endc = pCorrespondingEnd;
	if (!endc)
		endc = pCorresponding;

	if (endc)
	{
		if (!pPart || IsLessDefault(pPart, endc))
		{
			pPart = endc;
			changes++;
		}
	}
	else
	{
		for (CodePart* part : children)
			if (part->GetBottomCorresponding(pPart))
				changes++;
	}

	return !!changes;
}

bool CodePart::ContainsChanges() const
{
	ULONG oldPos = GetStart(false);
	ULONG curPos = GetStart(true);

	if (oldPos != curPos)
		return true;

	for (auto* part : children)
		if (part->ContainsChanges())
			return true;

	return false;
}

void CodePart::SortChildren(std::function<bool(CodePart* lpart, CodePart* rpart)> cmp)
{
	std::sort(children.begin(), children.end(), cmp);

	for (auto* p : children)
		p->SortChildren(cmp);
}

int CodePart::CompareCorresponding(const CodePart* other)
{
	auto* ac = pCorresponding;        // decl in header
	auto* bc = other->pCorresponding; // decl in header

	if (ac && bc)
	{
		// both 'a' and 'b' have decl in header,
		// thus we order them by position in header file

		if (ac->pMarker->mStartCp != bc->pMarker->mStartCp)
			return ac->pMarker->mStartCp < bc->pMarker->mStartCp ? -1 : 1;

		if (pCorrespondingEnd)
			ac = pCorrespondingEnd;

		if (other->pCorrespondingEnd)
			bc = other->pCorrespondingEnd;

		return ac->pMarker->mEndCp > bc->pMarker->mEndCp ? -1 : 1;
	}

	return 0;
}

bool CodePart::IsDependantOf(const CodePart* part) const
{
	if (dependencies.find(part) != dependencies.cend())
		return true;

	if (pParent)
		return pParent->IsDependantOf(part);

	return false;
}

bool CodePart::IsDependencyOf(const CodePart* part) const
{
	return part && part->IsDependantOf(this);
}

bool CodePart::IsInList(const std::vector<CodePart*>& parts) const
{
	for (auto part : parts)
		if (part == this)
			return true;

	return false;
}

ULONG CodePart::GetLength() const
{
	return pMarker->mEndCp - pMarker->mStartCp;
}

ULONG CodePart::GetStart(bool actual) const // actual is calculated
{
	if (!actual || !pParent)
		return pMarker->mStartCp;

	return pParent->GetStart(true) + GetLocalStart(true);
}

CStringW CodePart::GetDef(StripMask mask)
{
	// try to find the def by the key
	auto found = defsCache.find(mask);
	if (found != defsCache.cend())
		return found->second;

	// not found, we need to prepare the def
	CStringW defStr = pMarker->mText;
	CodePart* parent = pParent;

	defStr.TrimRight(L';');

	if (!(mask & StripMask_RemoveScopes)) // don't add scope when we are about to remove it
	{
		// some defs need to have added scope
		while (parent)
		{
			if (parent->IsTypeOrClass() || parent->IsNamespace())
				defStr = parent->pMarker->mText + L"::" + defStr;

			parent = parent->pParent;
		}
	}

	if (mask)
	{
		// I am reusing WTString methods, so it is good to convert once

		WTString wtStr(defStr);

		if (mask & StripMask_RemoveScopes)
			wtStr = StripScopes(wtStr);

		if (mask & StripMask_ToEssentials)
			wtStr = StripDefToBareEssentials(wtStr, NULLSTR, IsMethodOrFunc() ? FUNC : VAR);

		wtStr.GetWide(defStr);
	}

	defsCache[mask] = defStr;
	return defStr;
}

WTString CodePart::GetCode(const WTString& buffer, bool debug /*= false*/) const
{
	WTString rslt;

#ifdef DEBUG
	if (debug)
		rslt += "/* \\/***\\/***\\/***\\/***\\/***\\/***\\/***\\/***\\/***\\/***\\/***\\/***\\/***\\/ */\r\n";
#endif // DEBUG

	if (children.empty())
	{
		rslt += buffer.substr(pMarker->mStartCp, pMarker->mEndCp - pMarker->mStartCp);

#ifdef DEBUG
		if (debug)
			rslt += "/* /\\***/\\***/\\***/\\***/\\***/\\***/\\***/\\***/\\***/\\***/\\***/\\***/\\***/\\ */\r\n";
#endif // DEBUG

		return rslt;
	}

	// calculate where children start and where they end, so we can attach preceding and succeeding parts
	ULONG minsp = ULONG_MAX;
	ULONG maxep = 0;
	for (const auto* p : children)
	{
		minsp = std::min(minsp, p->pMarker->mStartCp);
		maxep = std::max(maxep, p->pMarker->mEndCp);
	}

	rslt += buffer.substr(pMarker->mStartCp, minsp - pMarker->mStartCp);

	for (const auto* p : children)
		rslt += p->GetCode(buffer, debug);

	rslt += buffer.substr(maxep, pMarker->mEndCp - maxep);

#ifdef DEBUG
	if (debug)
		rslt += "/* /\\***/\\***/\\***/\\***/\\***/\\***/\\***/\\***/\\***/\\***/\\***/\\***/\\***/\\ */\r\n";
#endif // DEBUG

	return rslt;
}

CStringW CodePart::GetMethodName()
{
	if (IsMethodOrFunc())
	{
		if (!methodName.IsEmpty())
			return methodName;

		CStringW localTmp;
		CStringW* str = &pMarker->mText;

		// remove comments if there are any
		int openComment = str->Find(L"/*");
		if (openComment >= 0)
		{
			localTmp = *str;
			str = &localTmp;

			while (openComment >= 0)
			{
				int closeComment = str->Find(L"*/", openComment + 2);

				if (closeComment >= openComment + 2)
				{
					str->Delete(openComment, closeComment + 2 - openComment);
					openComment = str->Find(L"/*", openComment);
					continue;
				}

				break;
			}
		}

		// read the method name
		int symEnd = str->Find(L'(');
		if (symEnd >= 0)
		{
			for (; symEnd > 0; symEnd--)
				if (ISCSYM((*str)[symEnd - 1]))
					break;

			int symStart = symEnd - 1;
			for (; symStart > 0; symStart--)
				if (!ISCSYM((*str)[symStart - 1]))
					break;

			methodName = str->Mid(symStart, symEnd - symStart);
			return methodName;
		}
	}

	return {};
}

bool CodePart::OverlapsLine(ULONG line)
{
	return pMarker->mStartLine >= line &&
	       pMarker->mEndLine <= line;
}

bool CodePart::HasDisplayFlags(ULONG bits) const
{
	return pMarker->mDisplayFlag & bits;
}

bool CodePart::IsMethodOrFunc() const
{
	return HasDisplayFlags(FileOutlineFlags::ff_MethodsAndFunctions);
}

bool CodePart::IsNamespace() const
{
	return HasDisplayFlags(FileOutlineFlags::ff_Namespaces);
}

bool CodePart::IsPreprocessor() const
{
	return HasDisplayFlags(FileOutlineFlags::ff_Preprocessor);
}

bool CodePart::IsStartDirective() const
{
	if (!IsPreprocessor())
		return false;

	return DirectiveTest(pMarker->mText, L"if", L"region");
}

bool CodePart::IsEndDirective() const
{
	if (!IsPreprocessor())
		return false;

	return DirectiveTest(pMarker->mText, L"endif", L"endregion");
}

bool CodePart::IsTypeOrClass() const
{
	return HasDisplayFlags(FileOutlineFlags::ff_TypesAndClasses);
}

bool CodePart::IsComment() const
{
	return HasDisplayFlags(FileOutlineFlags::ff_Comments);
}

void CodePart::ForEachChild(const std::function<bool(CodePart*)>& predicate)
{
	if (predicate)
	{
		for (auto* ch : children)
		{
			if (!predicate(ch))
				return;

			ch->ForEachChild(predicate);
		}
	}
}

bool CodePart::IsLessDefault(const CodePart* a, const CodePart* b)
{
	if (a->pMarker->mStartCp != b->pMarker->mStartCp)
		return a->pMarker->mStartCp < b->pMarker->mStartCp;

	return a->pMarker->mEndCp > b->pMarker->mEndCp;
}

bool CodePart::IsLessByHeader(const CodePart* a, const CodePart* b)
{
	if (a->pParent == b->pParent && (a->is_target_sym || b->is_target_sym))
	{
		auto* ac = a->pCorresponding; // decl in header
		auto* bc = b->pCorresponding; // decl in header

		if (ac && bc)
		{
			// both 'a' and 'b' have decl in header,
			// thus we order them by position in header file

			if (ac->pMarker->mStartCp != bc->pMarker->mStartCp)
				return ac->pMarker->mStartCp < bc->pMarker->mStartCp;

			if (a->pCorrespondingEnd)
				ac = a->pCorrespondingEnd;

			if (b->pCorrespondingEnd)
				bc = b->pCorrespondingEnd;

			return ac->pMarker->mEndCp > bc->pMarker->mEndCp;
		}

		// 		// we need to distinguish here, otherwise we can get invalid compare operator exception
		// 		if (!(ac || a->is_target_sym) || !(bc || b->is_target_sym))
		// 		{
		// 			if (ac || a->is_target_sym)
		// 			{
		// 				// 'a' has decl in header while 'b' does not have
		// 				// thus 'a' is lower than 'b' if 'b' isn't dependency of 'a'
		// 				return !b->IsDependencyOf(a);
		// 			}
		//
		// 			else if (bc || b->is_target_sym)
		// 			{
		// 				// 'b' has decl in header while 'a' does not have
		// 				// thus 'a' is lower than 'b' if 'a' is dependency of 'b'
		// 				return a->IsDependencyOf(b);
		// 			}
		// 		}

		bool asym = ac || a->is_target_sym;
		bool bsym = bc || b->is_target_sym;

		if (!asym && bsym && a->IsDependencyOf(b))
			return true;

		if (asym && !bsym && b->IsDependencyOf(a))
			return false;

		// 		if (!ac && bc && a->IsDependencyOf(b))
		// 			return true;
		//
		// 		if (ac && !bc && b->IsDependencyOf(a))
		// 			return false;
		//
		// 		if (!a->is_target_sym && b->is_target_sym && a->IsDependencyOf(b))
		// 			return true;
		//
		// 		if (a->is_target_sym && !b->is_target_sym && b->IsDependencyOf(a))
		// 			return false;
	}

	if (a->pMarker->mStartCp != b->pMarker->mStartCp)
		return a->pMarker->mStartCp < b->pMarker->mStartCp;

	return a->pMarker->mEndCp > b->pMarker->mEndCp;
}


