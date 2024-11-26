#pragma once

#include "WTString.h"

class SpellBlockInfo
{
  public:
	WTString spellWord;
	int m_p1;
	int m_p2;
	void Init()
	{
		spellWord.Empty();
		m_p1 = m_p2 = 0;
	}
};

extern SpellBlockInfo s_SpellBlockInfo;
