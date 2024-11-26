#include "stdafxed.h"
#include "BuildDate.h"
#include "VaAddinClient.h"
#include "LOG.H"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

// This file is always rebuilt in release configurations since
// it sets the date of the build when this file is compiled.
// This is required for license expiration to work correctly.

static int MonthNumber(const CString& name)
{
	constexpr int kMonths = 12;
	const char* months[kMonths] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

	for (int idx = 0; idx < kMonths;)
		if (name == months[idx++])
			return idx;

	for (int idx = 0; idx < kMonths;)
		if (!name.CompareNoCase(months[idx++]))
			return idx;

	vLog("ERROR: month name lookup failed: %s\n", (LPCSTR)name);
	_ASSERTE(!"no month name match");
	return 0;
}

CString GetBuildDate()
{
#if 1
	const char* kBuildDate = __DATE__;
	CString tmp(kBuildDate);
	tmp = tmp.Left(3);
	const int kBuildMon = MonthNumber(tmp);
	tmp = kBuildDate;
	tmp = tmp.Right(tmp.GetLength() - 4);
	int kBuildDay = 0, kBuildYear = 0;
	sscanf(tmp, "%d %d", &kBuildDay, &kBuildYear);
#else
	const int kBuildYear = 2011, kBuildMon = 10, kBuildDay = 2;
#endif

	CString date;
	CString__FormatA(date, "%04d.%02d.%02d", kBuildYear, kBuildMon, kBuildDay);
	return date;
}

void GetBuildDate(int& buildYear, int& buildMonth, int& buildDay)
{
	const CString buildDate(GetBuildDate());
	buildYear = buildMonth = buildDay = 0;
	::sscanf(buildDate, "%d.%d.%d", &buildYear, &buildMonth, &buildDay);
}

void VaAddinClient::GetBuildDate(int& buildYr, int& buildMon, int& buildDay)
{
	::GetBuildDate(buildYr, buildMon, buildDay);
}
