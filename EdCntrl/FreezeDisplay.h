#pragma once

#include "WTString.h"

class FreezeDisplay
{
	CStringW orgFile;
	uint orgPos;
	BOOL supressRedraw;

  public:
	FreezeDisplay(BOOL returnToOrgPos = TRUE, BOOL useStartOfSelection = FALSE);
	~FreezeDisplay();

	void LeaveCaretHere();
	void ReadOnlyCheck();
	void OffsetLine(int lines);
};
