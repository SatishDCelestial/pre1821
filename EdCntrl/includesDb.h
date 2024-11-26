#pragma once

#include <set>
#include "Mparse.h"
#include "DTypeDbScope.h"
#include "DBFile\DbTypeId.h"

class DTypeList;

namespace IncludesDb
{
void SolutionLoaded();
void Close();

// dbId is passed in from MultiParse and is parse-dependent
void StoreIncludeData(DbOutData& includesRecords, DbTypeId dbId, int symType);

// Purge doesn't know mp parse-dependent dbId
void PurgeIncludes(const CStringW& file, DTypeDbScope dbScp);
void PurgeIncludes(const std::set<UINT>& fileIds, DTypeDbScope dbScp);

// Readers don't know mp parse-dependent dbId
bool HasIncludedBys(const CStringW& file, DTypeDbScope dbScope);
bool HasIncludes(const CStringW& file, DTypeDbScope dbScope);
bool HasIncludes(UINT fileId, DTypeDbScope dbScp);
void GetIncludedBys(CStringW file, DTypeDbScope dbScp, DTypeList& fileIds);
void GetIncludes(UINT fileId, DTypeDbScope dbScp, DTypeList& fileIds);
void GetIncludes(CStringW file, DTypeDbScope dbScp, DTypeList& fileIds);
void IterateAllIncluded(DTypeDbScope dbScp, std::function<void(uint fileId)> fnc);
} // namespace IncludesDb
