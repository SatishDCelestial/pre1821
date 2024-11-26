#include "stdafxed.h"
#include "Edcnt.h"
#include "VACompletionSet.h"
#include "VACompletionSetEx.h"
#include "WPF_ViewManager.h"

BOOL gHasMefExpansion = FALSE;

bool ShowVACompletionSet(intptr_t pIvsCompletionSet)
{
	if (g_currentEdCnt && Psettings && Psettings->m_enableVA && WPF_ViewManager::Get() &&
	    WPF_ViewManager::Get()->HasAggregateFocus())
	{
		CComQIPtr<IVsCompletionSet> theirSet{(IUnknown*)pIvsCompletionSet};
		if (theirSet)
		{
			return g_CompletionSetEx->SetIvsCompletionSet(theirSet);
		}
	}

	return false;
}
