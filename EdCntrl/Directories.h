#pragma once

namespace VaDirs
{
// GetDllDir
// ----------------------------------------------------------------------------
// Returns directory where va dlls are installed
// typically c:\program files\visual assist\ 
	// Return value includes trailing slash.
//
const CStringW& GetDllDir();

// GetUserDir
// ----------------------------------------------------------------------------
// Returns user's va data directory
// typically c:\documents and settings\<username>\Application Data\VisualAssist\ 
	// This directory is where autotext, spell check dictionaries, etc. are stored.
// Return value includes trailing slash.
//
const CStringW& GetUserDir();

// GetUserLocalDir
// ----------------------------------------------------------------------------
// Returns users's va local data directory.
// typically c:\documents and settings\<username>\Local Settings\Application Data\VisualAssist\ 
	// This directory is the base dir which has sub-dirs for each IDE db.
// Return value includes trailing slash.
//
const CStringW& GetUserLocalDir();

// GetDbDir
// ----------------------------------------------------------------------------
// Returns the IDE specific user db directory.
// typically c:\documents and settings\<username>\Local Settings\Application Data\VisualAssist\[vc6_1|vs7_1|vs8_1]\ 
	// Return value includes trailing slash.
//
const CStringW& GetDbDir();

// GetHistoryDir
// ----------------------------------------------------------------------------
// Returns the IDE specific user history directory.
// typically c:\documents and settings\<username>\Local Settings\Application Data\VisualAssist\[vc6|vs7|vs8]\history\
	// Return value includes trailing slash.
//
const CStringW& GetHistoryDir();

// GetParserSeedFilepath
// ----------------------------------------------------------------------------
// Returns full path and file name for specified file name.
// Will look in user dir first and dll dir, second.
// Will return a string like:
// c:\documents and settings\<username>\Application Data\VisualAssist\misc\<basenameAndExt>
// or
// c:\program files\Visual Assist\misc\<basenameAndExt>
//
CStringW GetParserSeedFilepath(LPCWSTR basenameAndExt);

// RemoveAllDbDirs
// ----------------------------------------------------------------------------
// Removes all db dirs (pre- and post-1545).
//
void RemoveAllDbDirs();

// RemoveOldDbDirs
// ----------------------------------------------------------------------------
// Removes all pre-1545 db dirs.
//
void RemoveOldDbDirs();

// CheckForNewDb
// ----------------------------------------------------------------------------
// Checks version.dat file in db dir.
// If version in file does not match our version, then db is flagged for purge.
//
void CheckForNewDb();

// FlagForDbDirPurge
// ----------------------------------------------------------------------------
// Marks db as needing to be rebuilt.
//
void FlagForDbDirPurge();

// IsFlaggedForDbDirPurge
// ----------------------------------------------------------------------------
// Checks to see if db has been marked to be rebuilt.
//
bool IsFlaggedForDbDirPurge();

// PurgeDbDir
// ----------------------------------------------------------------------------
// Deletes symbol database so that it will be rebuilt.
//
void PurgeDbDir();

// PurgeProjectDbs
// ----------------------------------------------------------------------------
// Deletes project related data.
// Requires no loaded solution, no open files.
//
void PurgeProjectDbs();

// CleanDbTmpDirs
// ----------------------------------------------------------------------------
// Clears cache and history db dirs.
// Invoked by user via options dlg.
//
void CleanDbTmpDirs();

// UseAlternateDir
// ----------------------------------------------------------------------------
// Allows user to specify alternate directory for all of VA:
// dlls, user data and db dirs (old style everything out of one root dir).
// For internal use for side-by-side testing of different dlls/db.
// HKEY_CURRENT_USER\Software\Whole Tomato: "altDir"
//
void UseAlternateDir(const CStringW& altDir);

// LogDirs
// ----------------------------------------------------------------------------
// Log the dirs
//
void LogDirs();
} // namespace VaDirs
