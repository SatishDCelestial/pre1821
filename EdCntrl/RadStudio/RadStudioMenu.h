#pragma once
#if defined(RAD_STUDIO) || defined(VA_CPPUNIT)

#include <functional>

enum class FeatureMenuId : int
{
	// Menu identifiers
	vaFindSymbol = 0,
	vaFindReferences = 1,
	vaFindReferencesInUnit = 2,
	vaRename = 3,

	vaGoToRelated = 4,
	vaAddInclude = 5,
	vaCreateDeclaration = 6,
	vaCreateImplementation = 7,
	vaCreateAllImplementations = 8,
	vaCreateFromUsage = 9,
	vaGotoDeclaration = 10,
	vaGotoImplementation = 11,
	vaGoToMember = 12,
	vaRebuildDB = 13,

	Count
};

struct IVaRSMenu
{
	std::wstring caption;

	IVaRSMenu(LPCWSTR strCaption)
	    : caption(strCaption)
	{
	}

	virtual ~IVaRSMenu(){};

	virtual void Execute() = 0;
	virtual void UpdateState(bool& visible, bool& enabled, bool& checked) = 0;
	virtual LPCWSTR UpdateCaption(bool & additive) = 0;
	int GetId() const;
};

class VaRSMenuManager
{
	static int NextMenuId();

  public:
	static int GetMenuId(const IVaRSMenu* pMenu);
	static IVaRSMenu* FindMenu(int id);
	static IVaRSMenu* FindMenu(std::function<bool(const IVaRSMenu*)> predicate);
	static int AddMainMenu(IVaRSMenu* newMenu, const IVaRSMenu* insertAfter = nullptr, const IVaRSMenu* parent = nullptr);
	static int AddLocalMenu(IVaRSMenu* newMenu, const IVaRSMenu* insertAfter = nullptr, const IVaRSMenu* parent = nullptr);

	static BOOL AddFeatureMenu(FeatureMenuId menuId, IVaRSMenu* newMenu);
	static IVaRSMenu* GetFeatureMenu(FeatureMenuId menuId);

	static void Cleanup();
	static void InitMenus();
};

#endif