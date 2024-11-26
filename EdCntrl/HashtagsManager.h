#pragma once

class WTString;
class DType;

class HashtagsManager
{
  public:
	enum IgnoreStatuses
	{
		NotIgnored = 0b00,   // 0
		IgnoredLocal = 0b01, // 1
		IgnoredGlobal = 0b10 // 2
	};

	HashtagsManager();
	void SolutionLoaded();

	bool IsHashtagIgnored(DType& dt);
	bool IsHashtagIgnored(const WTString& tag, UINT fileId);

	bool IgnoreTag(DType& dt, bool global);
	bool IgnoreTag(const WTString& tag, bool global);
	bool IgnoreFile(DType& dt, bool global);
	bool IgnoreDirectory(DType& dt, bool global);
	bool IgnoreProject(CStringW projectPath, bool global);

	bool CanClearIgnores(bool global);
	bool ClearIgnores(bool global);
	bool HasIgnores();

	bool IsTagIgnored(const WTString& tag, int* pIgnoredRuleLocation = NULL);
	bool IsFileIgnored(UINT fileId, int* pIgnoredRuleLocation = NULL);
	bool IsDirectoryIgnored(UINT fileId, int* pIgnoredRuleLocation = NULL);
	bool IsProjectIgnored(UINT fileId, int* pIgnoredRuleLocation = NULL);

	bool ClearIgnoreTag(DType& dt, bool global);
	bool ClearIgnoreTag(const WTString& tag, bool global);
	bool ClearIgnoreFile(DType& dt, bool global);
	bool ClearIgnoreDirectory(DType& dt, bool global);
	bool ClearIgnoreProject(CStringW projectPath, bool global);

	int GetIgnoredRuleLocation(CStringW rule, CStringW prefix);

  private:
	void Reload();
	void Unload();
	void LoadGlobalRules();
	void LoadSolutionRules();
	void LoadProjectRules();
	void CopyLegacyRulesToUserDir();
	void LoadRuleFilesInDirectory(CStringW directory);
	void LoadRuleFile(CStringW ruleFilePath);
	void AddRule(CStringW rule);
	bool SaveRule(CStringW rule, bool global);
	bool DeleteRuleFromFile(CStringW rule, bool global);

	CStringW GetGlobalRuleFileDir() const;
	CStringW GetDefaultGlobalRuleFilePath() const;
	CStringW GetUserSolutionRuleFileDir() const;
	CStringW GetSharedSolutionRuleFileDir() const;
	CStringW GetDefaultSolutionRuleFilePath() const;

	CCriticalSection mRuleLock;
	std::vector<UINT> mIgnoredFileIds;
	std::vector<CStringW> mIgnoredDirs;
	std::vector<WTString> mIgnoredTags;
	std::vector<CStringW> mIgnoredProjects;
};

extern std::shared_ptr<HashtagsManager> gHashtagManager;
