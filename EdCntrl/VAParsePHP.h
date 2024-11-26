
template <class VP> class LangWrapperPHP : public LangWrapperJS<VP>
{
  public:
	using BASE = VP;
	using BASE::CurPos;
	using BASE::IsDef;
	using BASE::State;
	using BASE::VPS_ASSIGNMENT;
	using BASE::VPS_BEGLINE;

	LangWrapperPHP(int fType) : LangWrapperJS<VP>(fType)
	{
		// Basic support for PHP
		// Allow '$' char in the variables
		SetDollarValid(true);
	}
	virtual int GetLangType()
	{
		return HTML;
	} // Using HTML parser to parse out script
	virtual void OnCSym()
	{
		if (State().m_parseState == VPS_BEGLINE && StartsWith(CurPos(), "global"))
			State().m_parseState = VPS_ASSIGNMENT; // don't define as local VAR
		__super::OnCSym();
	}
};