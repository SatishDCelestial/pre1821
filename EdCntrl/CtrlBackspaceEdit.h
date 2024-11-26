#pragma once

#if defined(VISUAL_ASSIST_X)
#include "FontSettings.h"
#endif

template <class BASE = CEdit
#if defined(VISUAL_ASSIST_X)
          ,
          bool use_default_font = false
#endif // defined(VISUAL_ASSIST_X)
          >
class CtrlBackspaceEdit : public BASE
{
  public:
	CtrlBackspaceEdit() : BASE(), first(true)
	{
#if defined(VISUAL_ASSIST_X)
		if (use_default_font)
			BASE::m_font_type = VaFontType::None; // case: 142819 - use default font when not themed
#endif                                            // defined(VISUAL_ASSIST_X)
	}

  protected:
	virtual LRESULT WindowProc(UINT message, WPARAM wparam, LPARAM lparam)
	{
		if (first)
		{
			first = false;
			::SHAutoComplete(BASE::m_hWnd, SHACF_AUTOAPPEND_FORCE_OFF | SHACF_AUTOSUGGEST_FORCE_OFF);
		}

		return BASE::WindowProc(message, wparam, lparam);
	}

  private:
	bool first;
};
