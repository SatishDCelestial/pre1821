#pragma once

#include "FileLineMarker.h"
#include "DebugStream.h"

struct CodePart
{
	enum StripMask
	{
		StripMask_None = 0x0,
		StripMask_ToEssentials = 0x1,
		StripMask_RemoveScopes = 0x2
	};

	CStringW methodName;			// initialized when requested in GetMethodName
	FileLineMarker* pMarker;		// marker in the original version of file
	CodePart* pParent;				// part which this is nested inside, or nullptr for parts without scope
	CodePart* pCorresponding;		// corresponding part in header file, for scopes first in order by header position
	CodePart* pCorrespondingEnd;	// optional, in case of scope this is the last in order by header position
	int mFtype;						// type of the source file
	int is_target_sym;				// true if this part's def starts with name of the refactoring symbol (class being sorted)
	UINT id;						// unique ID usable for debugging (breakpoint if ID equals some specific value)
	ULONG codePos;					// default position (after extending part this will point to original def)

	std::unordered_map<StripMask, CStringW> defsCache;  // cached defs with level of stripping
	std::vector<CodePart*> children;                  // children are nested in this
	std::unordered_set<const CodePart*> dependencies; // parts which should be higher than this

	std::unique_ptr<FileLineMarker> patch_marker;	  // used in special cases for making fixes

	// default constructor for creation part from the file marker
	CodePart(FileLineMarker* marker, CodePart* parent, int ftype, UINT id);

	// this one also creates a fake marker, should be used only for making patch or extra item like group
	CodePart(ULONG startLine, ULONG startCp, ULONG endLine, ULONG endCp, ULONG type, ULONG attrs, ULONG displayFlags,
	                CodePart* parent, int ftype, UINT id);

	// fixes children when there are any errors like overlaps or skipped parts of code
	// also merges preceding comments to the block and other modifications before dependencies resolution
	int FixChildren(const WTString& buffer, std::vector<std::unique_ptr<CodePart>>* storage, const CodePart* headerComment, UINT& nextId);

	// start relative to parent
	ULONG GetLocalStart(bool actual) const;

	// top-most valid parent
	const CodePart* TopParent() const;

	// returns true when this part is target (the subject of movement) 
	// or contains such target in some children
	bool ContainsTargetSym() const;

	// if this has corresponding it returns actual,
	// otherwise looks in children and returns top most corresponding
	bool GetTopCorresponding(CodePart*& pPart) const;

	// if this has corresponding it returns actual, 
	// otherwise looks in children and returns bottom most corresponding
	bool GetBottomCorresponding(CodePart*& pPart) const;

	// returns true if there happened any movement in the part
	bool ContainsChanges() const;

	// sorts children and children in all children
	void SortChildren(std::function<bool(CodePart* lpart, CodePart* rpart)> cmp);

	// compare corresponding of this to corresponding of other
	// when 0 is returned, there is missing corresponding in either of parts
	int CompareCorresponding(const CodePart* other);

	// true when this depends on part
	bool IsDependantOf(const CodePart* part) const;

	// true when part depends on this
	bool IsDependencyOf(const CodePart* part) const;

	// true when this is within the parts
	bool IsInList(const std::vector<CodePart*>& parts) const;

	// this is the buffer length of part
	ULONG GetLength() const;

	// start relative to buffer
	ULONG GetStart(bool actual) const; // actual is calculated

	// takes the definition of the part with specified level of stripping
	CStringW GetDef(StripMask mask);

	// builds and returns current state of code as a text
	WTString GetCode(const WTString& buffer, bool debug = false) const;

	// returns the name of method, if this part is method
	CStringW GetMethodName(); // not const as it is caching

	bool OverlapsLine(ULONG line);

	bool HasDisplayFlags(ULONG bits) const;
	bool IsMethodOrFunc() const;
	bool IsNamespace() const;
	bool IsPreprocessor() const;
	bool IsStartDirective() const;
	bool IsEndDirective() const;
	bool IsTypeOrClass() const;
	bool IsComment() const;

	void ForEachChild(const std::function<bool(CodePart*)>& predicate);

	static bool IsLessDefault(const CodePart* a, const CodePart* b);
	static bool IsLessByHeader(const CodePart* a, const CodePart* b);
};

