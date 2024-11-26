#pragma once

#include <set>
#include "Mparse.h"
#include "DTypeDbScope.h"
#include "DBFile\DbTypeId.h"

class DTypeList;

namespace InheritanceDb
{
extern const WTString kInheritsFromStr;
extern const WTString kInheritedByStr;

void SolutionLoaded();
void Close();

// dbId is passed in from MultiParse and is parse-dependent
void StoreInheritanceData(DbOutData& inheritanceRecords, DbTypeId dbId, int fileType, int symType, uint slnHash);

// Purge doesn't know mp parse-dependent dbId
void PurgeInheritance(const CStringW& file, DTypeDbScope dbScp);
void PurgeInheritance(const std::set<UINT>& fileIds, DTypeDbScope dbScp);

// Readers don't know mp parse-dependent dbId
void GetInheritanceRecords(int inhType, const WTString& scope, DTypeList& records);
} // namespace InheritanceDb
