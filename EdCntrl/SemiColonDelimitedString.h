#pragma once

class SemiColonDelimitedString
{
  public:
	SemiColonDelimitedString() = default;
	SemiColonDelimitedString(LPCWSTR txt)
	{
		Load(CStringW(txt));
	}
	SemiColonDelimitedString(const CStringW& str)
	{
		Load(str);
	}

	bool HasMoreItems()
	{
		while (mOffset < mStrLen)
		{
			if (mStr[mOffset] == L';')
				++mOffset;
			else
				return true;
		}

		return false;
	}

	bool NextItem(CStringW& item)
	{
		if (!HasMoreItems())
		{
			item.Empty();
			return false;
		}

		int pos = mStrBackingStore.Find(L';', mOffset);
		_ASSERTE(pos != 0); // HasMoreItems() shouldn't leave us at a delimiter
		if (-1 == pos)
		{
			// read to end of str
			item = mStrBackingStore.Mid(mOffset);
			mOffset = mStrLen;
		}
		else
		{
			item = mStrBackingStore.Mid(mOffset, pos - mOffset);
			mOffset = pos + 1;
		}

		return true;
	}

	// more appropriate for long loops due to no memory management/heap contention
	bool NextItem(LPCWSTR& pItem, int& itemlen)
	{
		if (!HasMoreItems())
		{
			pItem = nullptr;
			itemlen = 0;
			return false;
		}

		LPCWSTR psz = ::wcschr(&mStr[mOffset], L';');
		_ASSERTE(psz != &mStr[mOffset]); // HasMoreItems() shouldn't leave us at a delimiter
		pItem = &mStr[mOffset];
		if (psz == nullptr /*-1 == pos*/)
		{
			// read to end of str
			itemlen = mStrLen - mOffset;
			mOffset = mStrLen;
		}
		else
		{
			int pos = int(psz - mStr);
			itemlen = pos - mOffset;
			mOffset = pos + 1;
		}

		return true;
	}

	int GetBaseLength() const
	{
		return mStrLen;
	}
	int GetRemainingLength() const
	{
		return mStrLen - mOffset;
	}
	void Reset()
	{
		mOffset = 0;
	}
	void Load(const CStringW& str)
	{
		Reset();
		mStrBackingStore = str;
		mStrLen = mStrBackingStore.GetLength();
		mStr = mStrBackingStore;
	}

  private:
	CStringW mStrBackingStore;
	LPCWSTR mStr = nullptr;
	int mStrLen = 0;
	int mOffset = 0;
};
