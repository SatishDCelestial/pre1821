#include "InferType.h"

// Not much in here since the default parser supports C++ and C#
// Might break out c++ stuff from the default VAParse to add here?
template <class VP> class LangWrapperCPP : public VP
{
  public:
	using BASE = VP;
	using BASE::FileType;
	using BASE::m_deep;
	using BASE::m_mp;
	using BASE::State;

	LangWrapperCPP(int fType) : VP(fType)
	{
		g_doDBCase = TRUE;
		SetDollarValid(true);
	}

	virtual void DoScope()
	{
		VP::DoScope();

		// [case: 13607] Auto support to look up type of a var from assignment
		if (State().m_lwData && State().m_lwData->MaskedType() == LINQ_VAR)
			State(m_deep).m_lwData = InferTypeFromAutoVar(State(m_deep).m_lwData, m_mp, FileType());
	}

	virtual void OnAddSymDef(const WTString& symScope, const WTString& def, uint type, uint attrs)
	{
		if ((VAR == type || LINQ_VAR == type || Lambda_Type == type) && -1 != def.Find("auto"))
		{
			LPCSTR pAuto = strstrWholeWord(def, "auto", TRUE);
			if (pAuto)
			{
				LPCSTR pSym = strstrWholeWord(def, StrGetSym(symScope), TRUE);
				if (pSym && pAuto < pSym)
				{
					// [case: 13607] LINQ_VAR flag for C++0x "auto"
					VP::OnAddSymDef(symScope, def, LINQ_VAR, 0);
					return;
				}
			}
		}

		VP::OnAddSymDef(symScope, def, type, attrs);
	}

  protected:
	virtual BOOL ParsePropertyAsClassScope() const
	{
		return TRUE;
	}

	void ForIf_CreateScope(NewScopeKeyword keyword) override
	{
		BASE::ForIf_DoCreateScope();
	}

	BOOL ForIf_CloseScope(NewScopeKeyword keyword) override
	{
		ASSERT_ONCE(m_deep > 1);
		BOOL retval = m_deep > 1;
		BASE::DecDeep();
		return retval;
	}
};
