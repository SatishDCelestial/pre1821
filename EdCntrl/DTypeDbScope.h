#pragma once

// different than DbTypeId
// This enum hides DbTypeId_ExternalOther, DbTypeId_SolutionSystemCpp.
// And hides the difference between DbTypeId_Cpp and DbTypeId_Net.
enum class DTypeDbScope
{
	dbLocal = 0x0001,
	dbSolution = 0x0002,
	dbSystem = 0x0010,
	dbSystemIfNoSln = 0x1000 | dbSolution,
	dbSlnAndSys = dbSolution | dbSystem
};
