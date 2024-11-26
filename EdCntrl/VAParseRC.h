//////////////////////////////////////////////////////////////////////////
// Parse RC files
// At this point we only for string tables and add them so IDS_NAME displays "value" in minihelp
// Not sure there is merit in parsing other items?

template <class VP> class LangWrapperRC : public VP
{
  public:
	using BASE = VP;
	using BASE::CurPos;
	using BASE::GetLangType;
	using BASE::State;

	LangWrapperRC(int fType) : VP(fType)
	{
	}

	BOOL m_inStringTable;
	virtual void OnError(LPCSTR errPos)
	{
		if (State().m_inComment) // Only underline spelling errors
			VP::OnError(errPos);
	}
	virtual void DoScope()
	{
		m_inStringTable = FALSE;
		// Don't show guesses
		VP::DoScope();
	}
	virtual void OnNextLine()
	{
		BASE::ClearLineState(CurPos());
		VP::OnNextLine();
	}
	virtual void OnDef()
	{
		if (m_inStringTable)
			VP::OnDef(); // Only add string tables
	}
	virtual void OnComment(char c, int altOffset = UINT_MAX)
	{
		if (m_inStringTable && c == '"')
		{
			// IDS_NAME "string text", add as  type DEFINE
			State().m_defType = DEFINE;
			State().m_defAttr |= V_PREFERREDDEFINITION;
			OnDef();
		}
		VP::OnComment(c, altOffset);
	}
	virtual void OnCSym()
	{
		if (CwIS("STRINGTABLE"))
			m_inStringTable = TRUE;
		else if (m_inStringTable && CwIS("END"))
			m_inStringTable = FALSE;
		VP::OnCSym();
	}
};
