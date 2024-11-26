#pragma once

#include "WTString.h"
#include "ParseWorkItem.h"
#include "log.h"
#include "fdictionary.h"
#include <set>
#include "Mparse.h"
#include "DTypeDbScope.h"

class SymbolRemover : public ParseWorkItem
{
	CStringW mFile;
	std::set<UINT> mFilesToRemove;

  public:
	SymbolRemover(const CStringW& file) : ParseWorkItem("SymbolRemover-Single"), mFile(file)
	{
	}

	SymbolRemover(std::set<UINT>& filesToRemove) : ParseWorkItem("SymbolRemover-Multi")
	{
		mFilesToRemove.swap(filesToRemove);
	}

	virtual void DoParseWork()
	{
		try
		{
			if (mFilesToRemove.size())
			{
#ifndef RAD_STUDIO
				MultiParsePtr mp = MultiParse::Create(CS);
				mp->RemoveAllDefs(mFilesToRemove, DTypeDbScope::dbSolution);
#endif
			}
			else
			{
				MultiParsePtr mp = MultiParse::Create(GetFileTypeByExtension(mFile));
				mp->RemoveAllDefs(mFile, DTypeDbScope::dbSolution);
			}
		}
		catch (...)
		{
			VALOGEXCEPTION("SR::DPW");
			vLog("ERROR: Exception caught in SymbolRemover\n");
			_ASSERTE(!"Exception caught in SymbolRemover");
		}
	}

	virtual WTString GetJobName() const
	{
		return mJobName + WTString(" ") + (mFile.IsEmpty() ? "(multiple files)" : (LPCTSTR)CString(mFile));
	}
};
