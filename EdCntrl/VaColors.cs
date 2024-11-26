
#if __cplusplus
#define internal
#else
namespace WholeTomatoSoftware.VisualAssist
{
#endif

	internal enum VAColors
	{
		// keep the VAColors list in sync with g_colors array (see colors.h).
		// These can be used as indexes into it as well as into pSettings->m_colors

		C_TypoError,
		C_ContextError,
		C_ColumnIndicator,
		C_Text,
		C_Keyword,
		C_MatchedBrace,
		C_MismatchedBrace,
		C_Type,
		C_Macro,
		C_Var,
		C_Function,
		C_Comment,  // used rtf copy and color printing
		C_Number,   // used rtf copy and color printing
		C_String,   // used rtf copy and color printing
		C_Operator, // used rtf copy and color printing
		C_Undefined,
		C_Reference,
		C_ReferenceAssign,
		C_FindResultsHighlight,
		C_HighlightCurrentLine,
		C_EnumMember,
		C_Namespace,
		C_NULL
	};

#if __cplusplus
#undef internal
#else
}
#endif
