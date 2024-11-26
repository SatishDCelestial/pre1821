#pragma once

// see VADatabase::DBIdFromDbFlags for mapping to sym type db flags
enum DbTypeId
{
	DbTypeId_Error = -1,
	DbTypeId_First = 0,
	DbTypeId_Net = 0,           // VA_DB_Net
	DbTypeId_Cpp,               // VA_DB_Cpp
	DbTypeId_ExternalOther,     // VA_DB_ExternalOther
	DbTypeId_Solution,          // VA_DB_Solution
	DbTypeId_LocalsParseAll,    // VA_DB_PARSEALL
	DbTypeId_SolutionSystemCpp, // VA_DB_Cpp | VA_DB_SolutionPrivateSystem
	                            // 	DbTypeId_SolutionSystemNet,	// VA_DB_Net | VA_DB_SolutionPrivateSystem
	DbTypeId_Count
};

extern const wchar_t* kDbIdDir[DbTypeId_Count];
