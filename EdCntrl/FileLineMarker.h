#pragma once

#include <functional>
#include <algorithm>
#include <memory>
#include "FileOutlineFlags.h"
#include "Foo.h"

template <class T> class TreeT
{
  public:
	class Node
	{
	  public:
		friend class TreeT<T>;

		Node(const Node &) = default;
		~Node() = default;

		T& Contents()
		{
			return mItem;
		}
		T& operator*(void)
		{
			return Contents();
		}

		Node& AddChild(const T& t)
		{
			mChildren.push_back(Node(t));
			return mChildren.back();
		}
		size_t GetChildCount() const
		{
			return mChildren.size();
		}
		Node& GetChild(size_t i)
		{
			return mChildren[i];
		}

		template <class SortAlgCls> void Sort(bool descend = false)
		{
			std::sort(mChildren.begin(), mChildren.end(), NodeSortAlgCls<SortAlgCls>());
			if (descend)
			{
				typename std::vector<Node>::iterator iter;
				for (iter = mChildren.begin(); iter != mChildren.end(); ++iter)
				{
					(*iter).Sort<SortAlgCls>(descend);
				}
			}
		}

	  private:
		Node()
		{
		}
		Node(const T& t) : mItem(t)
		{
		}

		void RemoveChildren()
		{
			mChildren.clear();
		}

		std::vector<Node> mChildren;
		T mItem;

		template <class SortAlgCls>
		struct NodeSortAlgCls /*: public std::binary_function<const Node&, const Node&, bool>*/
		{
			bool operator()(const Node& n1, const Node& n2) const
			{
				const T& t1 = n1.mItem;
				const T& t2 = n2.mItem;
				SortAlgCls a;
				return a(t1, t2);
			}
		};
	};

  public:
	TreeT() = default;
	TreeT(const T& root) : mRoot(root)
	{
	}
	TreeT(const TreeT<T> &) = default;
	~TreeT() = default;

	Node& Root()
	{
		return mRoot;
	}
	void Clear()
	{
		mRoot.RemoveChildren();
	} // note: doesn't clear root node contents (mItem)

  private:
	Node mRoot;
};

// struct used to record places in a file.
// Could be used for function defs, #region locations, #includes, etc.
struct FileLineMarker
{
	CStringW mText;       // display text or name
	WTString mSelectText; // optional text to select upon arrival in editor
	ULONG mStartLine;
	ULONG mStartCp;
	ULONG mEndLine;
	ULONG mEndCp;
	ULONG mGotoLine;    // goto this line on select
	ULONG mType;        // used to pick an icon
	ULONG mAttrs;       // used to pick an icon
	ULONG mDisplayFlag; // see FileOutlineFlags.h
	DType mClassData;
	bool mCanDrag;

	FileLineMarker()
	    : mStartLine(0), mStartCp(0), mEndLine(0), mEndCp(0), mGotoLine(0), mType(0), mAttrs(0), mDisplayFlag(0),
	      mCanDrag(true)
	{
	}

	FileLineMarker(const CStringW& txt, ULONG startLine, ULONG type = UNDEF, ULONG attrs = 0)
	    : mText(txt), mStartLine(startLine), mStartCp(0), mEndLine(ULONG(-1)), mEndCp(0), mGotoLine(startLine),
	      mType(type), mAttrs(attrs), mDisplayFlag(0), mCanDrag(true)
	{
		_ASSERTE((type & TYPEMASK) == type);
	}

	FileLineMarker(const CStringW& txt, const WTString& selectTxt, ULONG startLine, ULONG type = UNDEF, ULONG attrs = 0,
	               ULONG gotoLine = -1)
	    : mText(txt), mSelectText(selectTxt), mStartLine(startLine), mStartCp(0), mEndLine(ULONG(-1)), mEndCp(0),
	      mGotoLine(startLine), mType(type), mAttrs(attrs), mDisplayFlag(0), mCanDrag(true)
	{
		_ASSERTE((type & TYPEMASK) == type);
		if (gotoLine != ULONG(-1))
		{
			mGotoLine = gotoLine;
		}
	}

	FileLineMarker(const CStringW& txt, ULONG startLine, ULONG startCp, ULONG endLine, ULONG endCp, ULONG type,
	               ULONG attrs, ULONG displayFlag, const DType& classData, bool canDrag)
	    : mText(txt), mStartLine(startLine), mStartCp(startCp), mEndLine(endLine), mEndCp(endCp), mGotoLine(startLine),
	      mType(type), mAttrs(attrs), mDisplayFlag(displayFlag), mClassData(classData), mCanDrag(canDrag)
	{
		_ASSERTE((type & TYPEMASK) == type);
		_ASSERTE(mEndLine >= mStartLine);
	}

	FileLineMarker(const CStringW& txt, const WTString& selectTxt, ULONG startLine, ULONG startCp, ULONG endLine,
	               ULONG endCp, ULONG type, ULONG attrs, ULONG displayFlag, const DType& classData, bool canDrag)
	    : mText(txt), mSelectText(selectTxt), mStartLine(startLine), mStartCp(startCp), mEndLine(endLine),
	      mEndCp(endCp), mGotoLine(startLine), mType(type), mAttrs(attrs), mDisplayFlag(displayFlag),
	      mClassData(classData), mCanDrag(canDrag)
	{
		_ASSERTE((type & TYPEMASK) == type);
		_ASSERTE(mEndLine >= mStartLine);
	}

	FileLineMarker(const FileLineMarker& in)
	    : mText(in.mText), mSelectText(in.mSelectText), mStartLine(in.mStartLine), mStartCp(in.mStartCp),
	      mEndLine(in.mEndLine), mEndCp(in.mEndCp), mGotoLine(in.mGotoLine), mType(in.mType), mAttrs(in.mAttrs),
	      mDisplayFlag(in.mDisplayFlag), mClassData(in.mClassData), mCanDrag(in.mCanDrag)
	{
		_ASSERTE(mEndLine >= mStartLine);
	}
	FileLineMarker& operator=(const FileLineMarker &) = default;

	bool operator==(const FileLineMarker& rhs) const
	{
		if (mStartLine != rhs.mStartLine)
			return false;
		if (mStartCp != rhs.mStartCp)
			return false;
		if (mEndLine != rhs.mEndLine)
			return false;
		if (mEndCp != rhs.mEndCp)
			return false;
		if (mGotoLine != rhs.mGotoLine)
			return false;
		if (mType != rhs.mType)
			return false;
		if (mAttrs != rhs.mAttrs)
			return false;
		if (mDisplayFlag != rhs.mDisplayFlag)
			return false;
		if (mCanDrag != rhs.mCanDrag)
			return false;
		if (mText != rhs.mText)
			return false;
		if (mSelectText != rhs.mSelectText)
			return false;
		if (!(mClassData == rhs.mClassData))
			return false;

		return true;
	}

	bool operator!=(const FileLineMarker& rhs) const
	{
		return !(*this == rhs);
	}

	int GetIconIdx() const;
};

// LineMarkerPath
// ----------------------------------------------------------------------------
// list of FileLineMarkers that contain a line.
// populated by LineMarkers::CreateMarkerPath
//
class LineMarkerPath : public std::vector<FileLineMarker>
{
  public:
	LineMarkerPath()
	{
	}
	~LineMarkerPath()
	{
	}

	bool HasBlockDisplayType(FileOutlineFlags::DisplayFlag typ)
	{
		for (iterator it = begin(); it != end(); ++it)
		{
			FileLineMarker& mkr = *it;
			FileOutlineFlags::DisplayFlag displayFlag = (FileOutlineFlags::DisplayFlag)mkr.mDisplayFlag;
			if (displayFlag == typ)
				return true;
		}

		return false;
	}

	bool IsLineAtBlockStart(ULONG ln)
	{
		reverse_iterator it = rbegin();
		if (it != rend())
		{
			FileLineMarker& mkr = *it;
			if (mkr.mStartLine == (int)ln)
				return true;
		}

		return false;
	}
};

// LineMarkers
// ----------------------------------------------------------------------------
// for ease of forward declarations
//
class LineMarkers : public TreeT<FileLineMarker>
{
  public:
	LineMarkers() = default;
	LineMarkers(const LineMarkers &) = default;
	~LineMarkers() = default;

	void CreateMarkerPath(Node& node, ULONG ln, LineMarkerPath& path, bool includePseudoGroups = false,
	                      int prevStartLine = -1)
	{
		for (size_t idx = 0; idx < node.GetChildCount(); ++idx)
		{
			Node& ch = node.GetChild(idx);
			FileLineMarker& mkr = *ch;
			FileOutlineFlags::DisplayFlag displayFlag = (FileOutlineFlags::DisplayFlag)mkr.mDisplayFlag;
			if (mkr.mEndLine < ln)
			{
				if (prevStartLine != -1)
					prevStartLine = (displayFlag & FileOutlineFlags::ff_Comments) ? (int)mkr.mStartLine : -2;

				continue;
			}

			if (mkr.mStartLine > ln)
				return;

			if (mkr.mStartLine == ln)
			{
			}
			else if (mkr.mEndLine == ln && FileOutlineFlags::ff_Preprocessor == displayFlag)
			{
				// for preproc markers, when endLine == ln, only add nodes for
				// #else or #elif because they are required for #endif
				if (-1 == mkr.mText.Find(L"#el"))
					continue;
				else
					; // haven't hit this condition, not sure what that means...
			}
			else if (FileOutlineFlags::ff_Preprocessor == displayFlag && -1 != mkr.mText.Find(L"endif"))
			{
				// #endif node contains blank lines that follow.
				// treat as if #endif nodes are only 1 line.
				// And since #endif is a child node, also need to remove its parent.
				_ASSERTE((FileOutlineFlags::DisplayFlag)path.back().mDisplayFlag == FileOutlineFlags::ff_Preprocessor);
				path.pop_back();
				return;
			}

			if (includePseudoGroups || !(displayFlag & FileOutlineFlags::ff_PseudoGroups))
			{
				path.push_back(mkr);
				if (prevStartLine >= 0)
					path[path.size() - 1].mStartLine = (ULONG)prevStartLine;
			}

			if (prevStartLine != -1)
				prevStartLine = (displayFlag & FileOutlineFlags::ff_Comments) ? (int)mkr.mStartLine : -2;

			CreateMarkerPath(ch, ln, path, includePseudoGroups, prevStartLine);
			return;
		}
	}

	void CreateMarkerPath(ULONG ln, LineMarkerPath& path, bool includePseudoGroups = false,
	                      bool attachCommentBefore = false)
	{
		CreateMarkerPath(Root(), ln, path, includePseudoGroups, attachCommentBefore ? -2 : -1);
	}

	Node* FindIncludesBlock(Node& node, ULONG insertAtLine)
	{
		for (size_t idx = 0; idx < node.GetChildCount(); ++idx)
		{
			Node& ch = node.GetChild(idx);
			FileLineMarker& mkr = *ch;

			// If this node is an includes block and contains the line, return it
			if (mkr.mText == L"#includes" && mkr.mStartLine <= insertAtLine && mkr.mEndLine >= insertAtLine)
			{
				return &ch;
			}

			// Otherwise, recurse into children
			Node* result = FindIncludesBlock(ch, insertAtLine);
			if (result != nullptr)
			{
				return result;
			}
		}

		// If no includes block is found in this node or its children, return null
		return nullptr;
	}

	Node* FindIncludesBlock(ULONG insertAtLine)
	{
		return FindIncludesBlock(Root(), insertAtLine);
	}

	std::optional<uint> IsSorted(Node& node, CStringW headerFile, CStringW pchFileName, uint from, uint to)
	{
		// Handle the 'to' parameter being INT_MAX
		to = (to < node.GetChildCount() - 1) ? to : (uint)node.GetChildCount() - 1;

		// count the number of lines without comment
		int count = 0;
		FileLineMarker* lastMarker = nullptr; // The last marker that was not a comment
		FileLineMarker* firstMarker = nullptr;
		uint secondIndex = 0;
		for (uint i = from; i <= to; ++i)
		{
			Node& child = node.GetChild(i);
			FileLineMarker& marker = *child;
			if (marker.mType == COMMENT)
				continue;
			CStringW processedText = GetIncludeFromFileNameLower(marker.mText);
			int len = pchFileName.GetLength();
			if (i == 0 && (processedText.Left(8) == "stdafx.h" || processedText.Left(5) == "pch.h" || (len > 0 && processedText.Left(len) == pchFileName)))
				continue; // treat it as it was a comment

			if (firstMarker == nullptr)
			{
				firstMarker = &marker;
			}
			else
			{
				if (secondIndex == 0)
					secondIndex = i;
			}
			lastMarker = &marker;
			++count;
		}
		if (secondIndex == 0)
			secondIndex = from;

		if (count == 0)
			return std::nullopt; // Fall back to the original algorithm

		auto getFileName = [](CStringW filePath) {
			int pos = filePath.ReverseFind('\\');
			int pos2 = filePath.ReverseFind('/');

			// Get the max position since we don't know if the path uses '\\' or '/'
			pos = max(pos, pos2);

			if (pos != -1)
			{
				filePath = filePath.Mid(pos + 1);
			}

			filePath.MakeLower();
			return filePath;
		};

		headerFile = getFileName(headerFile);

		// initialize to the first line
		uint newHeaderLine = firstMarker->mStartLine;

		// Check each pair of children to make sure they are sorted
		FileLineMarker* prevMarker = firstMarker;
		for (size_t idx = secondIndex /*from + 1*/; idx <= to; ++idx)
		{
			Node& currChild = node.GetChild(idx);

			FileLineMarker& currMarker = currChild.Contents();

			if (currMarker.mType == COMMENT)
				continue;

			CStringW prevText = prevMarker->mText;
			CStringW currText = currMarker.mText;

			prevText = GetIncludeFromFileNameLower(prevText);
			currText = GetIncludeFromFileNameLower(currText);

			// Check if the substrings are sorted
			if (prevText.Compare(currText) > 0)
			{
				// If a previous child's mText is greater than the current one's,
				// the children are not sorted
				return std::nullopt;
			}

			// Is the header file fit here, assuming the includes are sorted?
			// Will determine sortedness when we are done with the for,
			// but store the insertation point now,
			// so we don't need one more for to determine where to fit the item
			if (prevText.Compare(headerFile) < 0 && currText.Compare(headerFile) > 0)
			{
				newHeaderLine = currMarker.mStartLine;
			}

			prevMarker = &currMarker;
		}

		// If headerFile is the last put it after the last include's line
		if (lastMarker && GetIncludeFromFileNameLower(lastMarker->mText).Compare(GetIncludeFromFileNameLower(headerFile)) < 0)
		{
			newHeaderLine = lastMarker->mStartLine + 1;
		}

		// If all pairs of children are sorted, the includes are sorted
		return newHeaderLine;
	}

	std::optional<uint> AreSystemIncludesFirst(Node& node, CStringW& pchFileName)
	{
		enum class IncludeType
		{
			None,
			System,
			Local
		};

		IncludeType prevIncludeType = IncludeType::None;
		std::optional<int> firstLocalIncludeIdx;

		// Iterate over each child to make sure system includes come before local includes
		for (size_t idx = 0; idx < node.GetChildCount(); ++idx)
		{
			Node& child = node.GetChild(idx);
			const FileLineMarker& marker = *child;

			if (marker.mType == COMMENT)
				continue;

			CStringW processedText = GetIncludeFromFileNameLower(marker.mText);
			int len = pchFileName.GetLength();
			if (idx == 0 && (processedText.Left(8) == "stdafx.h" || processedText.Left(5) == "pch.h" || (len > 0 && processedText.Left(len) == pchFileName)))
				continue; // treat it as it was a comment

			// Check if this marker is an #include directive
			if (marker.mText.Find(L"#include") != -1)
			{
				// Determine whether this is a system or local include
				IncludeType thisIncludeType;
				if (marker.mText.Find('<') != -1 && marker.mText.Find('>') != -1)
				{
					thisIncludeType = IncludeType::System;
				}
				else if (marker.mText.Find('"') != -1)
				{
					thisIncludeType = IncludeType::Local;
					if (!firstLocalIncludeIdx)
					{
						firstLocalIncludeIdx = static_cast<int>(idx);
					}
				}
				else
				{
					// Skip this child if it's not a recognized include directive
					continue;
				}

				// If we've moved from system to local includes, we can't move back
				if (prevIncludeType == IncludeType::Local && thisIncludeType == IncludeType::System)
					return std::nullopt;
				// If the first is a local include, it's not a two-block configuration
				if (prevIncludeType == IncludeType::None && thisIncludeType == IncludeType::Local)
					return std::nullopt;

				prevIncludeType = thisIncludeType;
			}
		}

		// If all pairs of children are in the correct order, return the index of the first local include
		return firstLocalIncludeIdx;
	}

	int mModCookie = 0;
};

typedef std::shared_ptr<LineMarkers> LineMarkersPtr;
