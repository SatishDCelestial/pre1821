#pragma once

// [case: 111093] Values used when implementing UFunctions for which the implementation name differs from the
// definition by a postfix. Because sometimes implementing a single UFunction requires two calls to
// CreateImplementation, "ValidateFollowingImplementation" is used to signify to CreateImplementation that the
// previously implemented method should also be included when selecting the results for easy cut and paste. Also used
// by WhereToPutImplementation. Bit of a hack.
enum class UnrealPostfixType
{
	None,
	Implementation,
	Validate,
	ValidateFollowingImplementation
};
