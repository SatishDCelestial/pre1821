#pragma once

// use CSpinCriticalSection for high contention resources that are blocked
// for short amounts of time to reduce context switches.
// not appropriate for use when file i/o is occurring.
class CSpinCriticalSection : public CCriticalSection
{
  public:
	CSpinCriticalSection() : CCriticalSection()
	{
		// Per https://msdn.microsoft.com/en-us/library/windows/desktop/ms686197%28v=vs.85%29.aspx :
		// You can improve performance significantly by choosing a small spin
		// count for a critical section of short duration.
		// The heap manager uses a spin count of roughly 4000 for its per-heap
		// critical sections.
		// This gives great performance and scalability in almost all
		// worst-case scenarios.
		::SetCriticalSectionSpinCount((CRITICAL_SECTION*)&m_sect, 4000);
	}
};
