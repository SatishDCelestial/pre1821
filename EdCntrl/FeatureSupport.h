#pragma once

enum FeatureType
{
	Feature_Outline,
	Feature_MiniHelp,
	Feature_HCB,
	Feature_Refactoring,
	Feature_Suggestions,
	Feature_CaseCorrect,
	Feature_SpellCheck,
	Feature_HoveringTips,
	Feature_ParamTips,
	Feature_FormatAfterPaste
};

BOOL IsFeatureSupported(FeatureType feature, int ftype = NULL);
BOOL SupportedFeatureDecoy_DontReallyUseMe(int feature, int ftype = NULL);
