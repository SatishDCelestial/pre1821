#pragma once

class RecursionClass
{
	static int m_count;

  public:
	RecursionClass(int maxSz = 10)
	{
		m_count++;
		static int max = 20;
		if (m_count > max)
			max = m_count;
	}

	~RecursionClass()
	{
		m_count--;
	}
};

#ifdef _DEBUG
#define ASSERT_LOOP(n) RecursionClass recClass(n);
#else
#define ASSERT_LOOP(n)
#endif
