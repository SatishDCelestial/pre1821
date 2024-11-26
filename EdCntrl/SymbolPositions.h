#pragma once

#include "FileList.h"
#include "WTString.h"

struct SymbolPositionInfo : public FileInfo
{
	SymbolPositionInfo() : FileInfo(), mLineNumber(0), mType(0), mAttrs(0), mDbFlags(0)
	{
	}

	SymbolPositionInfo(const SymbolPositionInfo& rhs)
	    : FileInfo(rhs), mLineNumber(rhs.mLineNumber), mDisplayText(rhs.mDisplayText), mType(rhs.mType),
	      mAttrs(rhs.mAttrs), mDbFlags(rhs.mDbFlags), mDef(rhs.mDef)
	{
	}

	SymbolPositionInfo(const CStringW& file, int line, CStringW displayTxt, UINT type, UINT attrs, UINT dbFlags,
	                   const WTString& def)
	    : FileInfo(file), mLineNumber(line), mDisplayText(displayTxt), mType(type), mAttrs(attrs), mDbFlags(dbFlags),
	      mDef(def)
	{
	}

	bool operator==(const SymbolPositionInfo& rhs) const
	{
		if (mLineNumber != rhs.mLineNumber)
			return false;
		if (mType != rhs.mType)
			return false;
		if (mAttrs != rhs.mAttrs)
			return false;
		if (mDisplayText != rhs.mDisplayText)
			return false;
		return FileInfo::operator==(rhs);
	}

	int mLineNumber;
	CStringW mDisplayText;
	UINT mType;
	UINT mAttrs;
	UINT mDbFlags;
	WTString mDef;
};

class SymbolPosList : public FileListT<SymbolPositionInfo>
{
  public:
	SymbolPosList() : FileListT<SymbolPositionInfo>()
	{
	}

	bool Contains(UINT fileId, int lineNumber) const
	{
		for (const_iterator it = begin(); it != end(); ++it)
		{
			if ((*it).mFileId == fileId && (*it).mLineNumber == lineNumber)
				return true;
		}
		return false;
	}

	void Sort()
	{
		sort([](const SymbolPositionInfo& p1, const SymbolPositionInfo& p2) -> bool {
			if (p1.mFileId == p2.mFileId)
				return p1.mLineNumber < p2.mLineNumber;
			return p1.mFilenameLower.Compare(p2.mFilenameLower) < 0;
		});
	}
};
