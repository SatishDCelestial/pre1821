#pragma once

#include "WTString.h"

interface IVsTextView;

WTString GetFileText(const CStringW& file);
CStringW GetFileTextW(const CStringW& file);

CStringW PkgGetFileTextW(const CStringW filename);
CStringW PkgGetFileTextW(IVsTextView* edVsTextView);

enum FileModificationState
{
	fm_notFound = -1,
	fm_clean,
	fm_dirty
};
FileModificationState PkgGetFileModState(const CStringW& filename);
