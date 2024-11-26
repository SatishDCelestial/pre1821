#pragma once

#include <iostream>
#include <fstream>
#include <string>
#include "Directories.h"
#include "wt_stdlib.h"

template <class ItemType> class VASavedList : public std::list<ItemType>
{
	using BASE = std::list<ItemType>;

	const uint kMaxListSize = 60;
	CStringW m_filename;

  public:
	void SaveToFile();
	void LoadFromFile(const CStringW& filename);
	BOOL AddToTop(const ItemType& _Val)
	{
		if (BASE::begin() != BASE::end() && _Val == *BASE::begin())
		{
			// Already at top
			return FALSE;
		}

		// remove first duplicate of _Val
		for (typename BASE::iterator it = BASE::begin(); it != BASE::end(); it++)
		{
			ItemType dbgVal = *it;
			if (_Val == *it)
			{
				it = BASE::erase(it);
				break;
			}
		}
		BASE::push_front(_Val);

		// Limit to size
		while (BASE::size() > kMaxListSize)
			BASE::pop_back();

		return TRUE;
	}
};

// store MRU info for initial selection of item on load of MIF drop-down and OFIS dlg.
class VA_MRUs
{
	CStringW m_project;

  public:
	VASavedList<WTString> m_MIF;
	VASavedList<CStringW> m_FIS;

	VA_MRUs()
	{
	}

	void LoadProject(const CStringW& project);
	void SaveALL();
};

extern VA_MRUs* g_VA_MRUs;
