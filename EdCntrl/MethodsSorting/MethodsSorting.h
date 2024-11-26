#pragma once

#include "Foo.h"
#include <vector>
#include "FileLineMarker.h"
#include "Mparse.h"
#include "MethodsSortingCodePart.h"
#include "WindowUtils.h"

class MethodsSorting
{
	WTString src_buffer;
	std::vector<int> src_line_ends;
	LineMarkers src_markers;
	std::vector<std::unique_ptr<CodePart>> src_parts; 
	//std::vector<CodePart*> src_sym_parts; // parts from refactored symbol
	std::unique_ptr<CodePart> src_root;
	uint src_file_id = 0;
	CStringW src_file_name;

	CStringW hdr_file_name;
	WTString hdr_buffer;
	LineMarkers hdr_markers;
	std::vector<std::unique_ptr<CodePart>> hdr_parts; 
	uint hdr_file_id = 0;
	WTString sym;
	CStringW symWide;
	UINT nextId = 0;

	CStringW statusStr;

	bool BuildHierarchy(WTString& buffer, uint fileId, int ftype, LineMarkers& markers, std::vector<std::unique_ptr<CodePart>>& parts, MultiParsePtr mparse);

	bool ResolveCorresponding();
	bool ResolveDependencies();

	bool Init(EdCntPtr ed, const DType* dt, CStringW& errorMessage);
	void Apply(StopWatch& total);

	static const DType* ResolveDType(EdCntPtr ed, const DType* dt);

  public:
	static bool CanSortMethods(EdCntPtr ed, const DType* dt);
	static bool SortMethods(EdCntPtr ed, const DType* dt);

	MethodsSorting() {}
};
