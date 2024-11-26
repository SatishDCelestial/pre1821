#ifndef PopulateTypes_h__
#define PopulateTypes_h__

// PopulateType is used as an argument to EdCnt::CmEditExpand and ListBoxLoader::Populate
// and controls the behavior of the completion listbox

enum PopulateType
{
	PopulateType_Default = 0,           // 0 - regular typing
	PopulateType_TabForcePopup,         // 1
	PopulateType_FixCase,               // 2
	PopulateType_AutoMemberList,        // 3
	PopulateType_ForcedMemberListNoTab, // 4 - no auto tab insert
	PopulateType_AutocompleteNoTab,     // 5 - no auto tab insert
	PopulateType_ExpSelectNoTab,        // 6 - no auto tab insert
	PopulateType_Last = PopulateType_ExpSelectNoTab,
	PopulateType_Count
};

#endif // PopulateTypes_h__
