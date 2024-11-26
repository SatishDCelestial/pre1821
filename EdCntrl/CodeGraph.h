#pragma once

class DType;
class MultiParse;

namespace CodeGraphNS
{
class IReferenceNodesContainer
{
  public:
	virtual ~IReferenceNodesContainer() = default;

	virtual void SetLinesOfCode(DType* refScope, UINT nLines, int line) = 0;
	virtual void AddLink(DType* refScope, DType* pointsTo, UINT linkType, int lineNumber, MultiParse* mp) = 0;
	virtual void OnIncludeFile(CStringW file, int line) = 0;
};

LPCWSTR VAGraphCallback(LPCWSTR idStrW);

enum RefType
{
	// Non-sortable attributes
	External = 0x1,
	CalledInTry = 0x2,
	CalledInLoop = 0x4,
	CalledInIf = 0x8,
	EventCB = 0x10,
	Override = 0x20,
	OneToMany = 0x40,
	AttributeMask = 0xfffff00,

	// Sortable LinkTypes
	Ref = 0x100,
	RefdBy = 0x200,
	Mod = 0x400,
	Call = 0x800,
	CreatesNew = 0x1000,
	Member = 0x2000,
	IInterface = 0x4000,
	IInterfaceRev = 0x8000,
	Inherit = 0x10000,
	Includes = 0x20000,
	RecursiveCall = 0x40000,
	All = 0xfffff00
}; // See: WholeTomato.VAGraphNS.RefType
} // namespace CodeGraphNS

extern BOOL g_CodeGraphWithOutVA /*= FALSE*/;
