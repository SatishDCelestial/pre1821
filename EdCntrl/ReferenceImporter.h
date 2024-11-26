#pragma once

#include "WTString.h"
#include "ParseWorkItem.h"
#include "log.h"
#include "import.h"

class ReferenceImporter : public ParseWorkItem
{
	CStringW mFile;

  public:
	ReferenceImporter(const CStringW file) : ParseWorkItem("ReferenceImporter"), mFile(file)
	{
	}

	virtual void DoParseWork()
	{
#if !defined(SEAN)
		try
#endif // !SEAN
		{
			if (!StopIt)
				NetImportDll(mFile);
		}
#if !defined(SEAN)
		catch (...)
		{
			VALOGEXCEPTION("RI::DPW");
			vLog("ERROR: Exception caught in ReferenceImporter\n");
			_ASSERTE(!"Exception caught in ReferenceImporter");
		}
#endif // !SEAN
	}

	virtual WTString GetJobName() const
	{
		return mJobName + " " + (LPCSTR)CString(mFile);
	}
};
