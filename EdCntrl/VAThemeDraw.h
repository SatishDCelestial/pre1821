#ifndef VAThemeDraw_h__
#define VAThemeDraw_h__

#pragma once

#include <afxwin.h>
#include <afxeditbrowsectrl.h>

// VA
#include "VaMessages.h"
#include "VADialog.h"
#include "VAThemeUtils.h"
#include "DimEditCtrl.h"
#include "ColorListControls.h"
#include "MenuXP\MenuXP.h"
#include "StringUtils.h"
#include "GenericComboBox.h"
#include "HTMLDialog\HtmlDialog.h"
#include "IdeSettings.h"
#include "CWndTextW.h"
#include "FontSettings.h"
#include "VAThemeUtils.h"

// STL
#include <memory>
#include <vector>
#include <set>
#include <xutility>
#include <sstream>

#if _MSC_VER < 1911
#define END_MESSAGE_MAP_INLINE END_MESSAGE_MAP
#else
// VS2017 15.3+
// I couldn't figure out why mismatched __pragma(warning( pop )) was happening...
#define END_MESSAGE_MAP_INLINE()                                                                                       \
	{                                                                                                                  \
		0, 0, 0, 0, AfxSig_end, (AFX_PMSG)0                                                                            \
	}                                                                                                                  \
	}                                                                                                                  \
	;                                                                                                                  \
	static const AFX_MSGMAP messageMap = {&TheBaseClass::GetThisMessageMap, &_messageEntries[0]};                      \
	return &messageMap;                                                                                                \
	}                                                                                                                  \
	PTM_WARNING_RESTORE

#endif

#define BEGIN_MESSAGE_MAP_INLINE(theClass, baseClass)                                                                  \
	PTM_WARNING_DISABLE                                                                                                \
	const AFX_MSGMAP* GetMessageMap() const                                                                            \
	{                                                                                                                  \
		return GetThisMessageMap();                                                                                    \
	}                                                                                                                  \
	static const AFX_MSGMAP* PASCAL GetThisMessageMap()                                                                \
	{                                                                                                                  \
		typedef theClass ThisClass;                                                                                    \
		typedef baseClass TheBaseClass;                                                                                \
		static const AFX_MSGMAP_ENTRY _messageEntries[] = {

#define NS_THEMEDRAW_BEGIN                                                                                             \
	namespace ThemeDraw                                                                                                \
	{
#define NS_THEMEDRAW_END }

NS_THEMEDRAW_BEGIN

template <typename TEventArgs> class EventList;

template <typename TEventArgs> class Event
{
	friend class EventList<TEventArgs>;

  protected:
	EventList<TEventArgs>* m_parent_list;

  public:
	typedef typename std::shared_ptr<Event<TEventArgs>> Ptr;

	// if event is not enabled, its invoke is not called
	bool Enabled = true;

	// remove this event from processing
	virtual void remove();

	// return 'true' to avoid subsequent events to be called
	virtual bool invoke(TEventArgs& args) = 0;

	virtual ~Event()
	{
	}
};

// Event list could also be used as single event,
// So if you have a group of
template <typename TEventArgs> class EventList : public Event<TEventArgs>
{
  public:
	typedef typename Event<TEventArgs> EventT;
	typedef typename EventT::Ptr EventPtrT;

  protected:
	std::set<EventPtrT> m_events;

  public:
	virtual ~EventList()
	{
	}

	operator bool() const
	{
		return !m_events.empty();
	}

	virtual bool invoke(TEventArgs& args)
	{
		if (!m_events.empty())
		{
			using sizeT = typename std::set<EventPtrT>::size_type;
			using iterT = typename std::set<EventPtrT>::const_iterator;

			std::vector<EventPtrT> events(m_events.size());
			std::copy(m_events.begin(), m_events.end(), events.begin());

			ThemeUtils::AutoFnc args_cleanup([&args]() { args.this_event = nullptr; });

			for (auto e : events)
			{
				if (e.get() && e->Enabled)
				{
					args.this_event = e.get();

					if (e->invoke(args))
						return true;
				}
			}
		}

		return false;
	}

	EventPtrT add(EventPtrT ptr)
	{
		_ASSERTE(ptr.get() != nullptr);

		ptr->m_parent_list = this;
		m_events.insert(ptr);
		return ptr;
	}

	void remove(EventPtrT ptr)
	{
		_ASSERTE(ptr.get() != nullptr);

		ptr->m_parent_list = nullptr;
		m_events.erase(ptr);
	}

	void remove(Event<TEventArgs>* ptr)
	{
		_ASSERTE(ptr != nullptr);

		ptr->m_parent_list = nullptr;
		m_events.erase(EventPtrT(ptr, [](EventT* p) {}));
	}
	using Event<TEventArgs>::remove;
};

template <typename TEventArgs> void Event<TEventArgs>::remove()
{
	if (m_parent_list)
		m_parent_list->remove(this);
}

template <class TSender, class TEventArgs> struct EventArgs
{
	TSender* sender = nullptr;
	Event<TEventArgs>* this_event = nullptr;
};

template <class WINDOW> struct WndEventArgs : public EventArgs<WINDOW, WndEventArgs<WINDOW>>
{
};

struct WndProcEventArgsBase
{
	CWnd* wnd;
	UINT msg;
	WPARAM wParam;
	LPARAM lParam;
	LRESULT result;
	bool handled;

	bool is_command(UINT notify_code);
	bool is_command(UINT notify_code, UINT& out_id_from);
	bool is_notify(UINT notify_code);
	bool is_notify(UINT notify_code, UINT& out_id_from);
};

template <class TSender>
struct WndProcEventArgs : public WndProcEventArgsBase, public EventArgs<TSender, WndProcEventArgs<TSender>>
{
};

struct IWndLifeTime
{
	virtual ~IWndLifeTime()
	{
	}
};

typedef std::shared_ptr<IWndLifeTime> IWndLifeTimeShrP;

class WndLifeTimeObjList
{
	std::set<IWndLifeTimeShrP> m_life_time_objs;

	std::set<IWndLifeTimeShrP>& LifeTimeObjects()
	{
		return m_life_time_objs;
	}

  public:
	void AddLifeTimeObject(IWndLifeTimeShrP objPtr)
	{
		m_life_time_objs.insert(objPtr);
	}

	void RemoveLifeTimeObject(IWndLifeTimeShrP objPtr)
	{
		m_life_time_objs.erase(objPtr);
	}

	virtual ~WndLifeTimeObjList()
	{
	}
};

class IDefaultColors
{
  public:
	IDefaultColors()
	{
		ResetDefaultColors();
	}
	virtual ~IDefaultColors()
	{
	}

	COLORREF crDefaultBG, crDefaultText, crDefaultGrayText;
	std::shared_ptr<CBrush> brBefaultBG;
	virtual void ResetDefaultColors();
};

template <typename LIFETIME_OBJ> class WndLifeTime : public IWndLifeTime
{
	LIFETIME_OBJ* m_obj;
	bool m_obj_owner;

  public:
	WndLifeTime(LIFETIME_OBJ* obj, bool is_owner = true) : m_obj(obj), m_obj_owner(is_owner)
	{
	}

	virtual ~WndLifeTime()
	{
		if (m_obj_owner)
			delete m_obj;

		m_obj = nullptr;
	}

	LIFETIME_OBJ* get()
	{
		return m_obj;
	}
};

class WndStyles
{
	DWORD m_old_style, m_old_ex_style;
	DWORD m_style_add = 0;
	DWORD m_ex_style_add = 0;
	DWORD m_style_remove = 0;
	DWORD m_ex_style_remove = 0;

  public:
	enum Action : unsigned char
	{
		a_none = 0x00,

		a_add_style = 0x01,
		a_add_ex_style = 0x02,
		a_remove_style = 0x04,
		a_remove_ex_style = 0x08,

		a_all = 0xff
	};

	WndStyles()
	{
	}
	virtual ~WndStyles()
	{
	}

	virtual void InitFromWnd(CWnd* wnd)
	{
		m_old_style = m_style_add = wnd->GetStyle();
		m_old_ex_style = m_ex_style_add = wnd->GetExStyle();
		m_style_remove = 0;
		m_ex_style_remove = 0;
	}

	virtual void ModifyStyle(CWnd* wnd, DWORD remove, DWORD add, UINT nFlags = 0)
	{
		wnd->ModifyStyle(remove, add, nFlags);
		m_style_remove |= add;
		m_style_add |= remove;
	}

	virtual void ModifyExStyle(CWnd* wnd, DWORD remove, DWORD add, UINT nFlags = 0)
	{
		wnd->ModifyStyleEx(remove, add, nFlags);
		m_ex_style_remove |= add;
		m_ex_style_add |= remove;
	}

	DWORD OldStyle() const
	{
		return m_old_style;
	}
	DWORD OldExStyle() const
	{
		return m_old_ex_style;
	}

	DWORD& AddStyleRef()
	{
		return m_style_add;
	}
	DWORD& AddExStyleRef()
	{
		return m_ex_style_add;
	}
	DWORD& RemoveStyleRef()
	{
		return m_style_remove;
	}
	DWORD& RemoveExStyleRef()
	{
		return m_ex_style_remove;
	}

	virtual void RevertChanges(CWnd* wnd, UINT nFlags = 0)
	{
		wnd->ModifyStyle(m_style_remove, m_style_add);
		wnd->ModifyStyleEx(m_ex_style_remove, m_ex_style_add, nFlags);
	}

	virtual void RevertToOld(CWnd* wnd, UINT nFlags = 0)
	{
		HWND hWnd = wnd->GetSafeHwnd();
		::SetWindowLong(hWnd, GWL_STYLE, (LONG)m_old_style);
		::SetWindowLong(hWnd, GWL_EXSTYLE, (LONG)m_old_ex_style);
	}
};

class ItemState;
struct IStateHandler
{
	virtual bool IsEnabled() const = 0;
	virtual void OnStateChanged(const ItemState& state, CWnd* wnd) = 0;
	virtual ~IStateHandler()
	{
	}
};

//////////////////////////////////////////////////////////////////////////
// controls state of control depending on window messages passed in
class ItemState
{
  public:
	enum state_flags
	{
		none = 0,
		focused = 0x01,
		client_mouse_over = 0x02,
		client_mouse_lb_down = 0x04,
		inactive = 0x08,
		show_prefix = 0x10,
		show_focus = 0x20,
		non_client_mouse_over = 0x40,
		non_client_mouse_lb_down = 0x80,

		custom_start = 0x100,

		client_mouse_mask = client_mouse_over | client_mouse_lb_down,
		non_client_mouse_mask = non_client_mouse_over | non_client_mouse_lb_down,

		mouse_over_mask = client_mouse_over | non_client_mouse_over,
		mouse_lb_down_mask = client_mouse_lb_down | non_client_mouse_lb_down,
		mouse_all_mask = mouse_over_mask | mouse_lb_down_mask,

		// could be extended to 0xffffffffffffffff
		max_value = 0xffffffff
	};

  private:
	bool m_show_accelerators_override = false;
	state_flags m_next_available = custom_start;
	state_flags m_prev_value = none;
	state_flags m_value = none;
	std::set<IStateHandler*> m_stateHandlers;
	CMapStringToPtr m_options;
	mutable CCriticalSection m_cs;
	DWORD m_active_mouse_tracking_flags = 0;
	bool m_extended_mouse_tracking = false;
	bool m_update_in_wm_setcursor = false;
	bool m_check_overlaps = true;

  public:
	// one can not copy ItemState
	ItemState(const ItemState&) = delete;
	ItemState& operator=(const ItemState&) = delete;

	ItemState()
	{
		BOOL bAlwaysAccelerators = FALSE;
		SystemParametersInfo(SPI_GETKEYBOARDCUES, 0, &bAlwaysAccelerators, 0);
		if (bAlwaysAccelerators)
		{
			m_show_accelerators_override = true;
			m_value = (state_flags)(m_value | show_prefix);
		}
	}

	virtual ~ItemState()
	{
	}

	void AddHandler(IStateHandler* sh)
	{
		m_stateHandlers.insert(sh);
	}
	void RemoveHandler(IStateHandler* sh)
	{
		m_stateHandlers.erase(sh);
	}
	void SetExtendedMouseTracking(bool val)
	{
		m_extended_mouse_tracking = val;
	}
	void SetUpdateInWM_SETCURSOR(bool val)
	{
		m_update_in_wm_setcursor = val;
	}
	void SetCheckOverlaps(bool val)
	{
		m_check_overlaps = val;
	}

	static bool IsFocused(state_flags value)
	{
		return (value & focused) == focused;
	}
	static bool IsMouseOverClient(state_flags value)
	{
		return (value & client_mouse_over) == client_mouse_over;
	}
	static bool IsMouseOverNonClient(state_flags value)
	{
		return (value & non_client_mouse_over) == non_client_mouse_over;
	}
	static bool IsMouseDownClient(state_flags value)
	{
		return (value & client_mouse_lb_down) == client_mouse_lb_down;
	}
	static bool IsMouseDownNonClient(state_flags value)
	{
		return (value & non_client_mouse_lb_down) == non_client_mouse_lb_down;
	}
	static bool IsMouseOver(state_flags value)
	{
		return IsMouseOverClient(value) || IsMouseOverNonClient(value);
	}
	static bool IsMouseDown(state_flags value)
	{
		return IsMouseDownClient(value) || IsMouseDownNonClient(value);
	}
	static bool IsInactive(state_flags value)
	{
		return (value & inactive) == inactive;
	}
	static bool IsPrefixVisible(state_flags value)
	{
		return (value & show_prefix) == show_prefix;
	}
	static bool IsFocusVisible(state_flags value)
	{
		return (value & show_focus) == show_focus;
	}
	static bool IsNormal(state_flags value)
	{
		return !IsFocused(value) && !IsMouseOver(value) && !IsInactive(value);
	}

	state_flags GetNextAvailableFlag()
	{
		if (m_next_available == none)
			return none;

		state_flags result = m_next_available;

		if (m_next_available == max_value)
			m_next_available = none;
		else
			m_next_available = (state_flags)(m_next_available * 2);

		return result;
	}

	void SetOption(LPCTSTR name, UINT_PTR opt = 1)
	{
		CSingleLock lock(&m_cs, TRUE);
		m_options.SetAt(name, (void*)opt);
	}

	void UnsetOption(LPCTSTR name)
	{
		CSingleLock lock(&m_cs, TRUE);
		m_options.RemoveKey(name);
	}

	bool HasOption(LPCTSTR name) const
	{
		CSingleLock lock(&m_cs, TRUE);
		void* ptr = nullptr;
		return TRUE == m_options.Lookup(name, ptr);
	}

	bool TryGetOption(LPCTSTR name, UINT_PTR& value_out) const
	{
		CSingleLock lock(&m_cs, TRUE);
		void* ptr = nullptr;
		if (m_options.Lookup(name, ptr))
		{
			value_out = (UINT_PTR)ptr;
			return true;
		}
		return false;
	}

	UINT_PTR GetOption(LPCTSTR name) const
	{
		UINT_PTR ptr = 0;
		TryGetOption(name, ptr);
		return ptr;
	}

	state_flags GetPreviousValue() const
	{
		return m_prev_value;
	}
	state_flags GetValue() const
	{
		return m_value;
	}
	bool HasFlag(state_flags val) const
	{
		return (m_value & val) == val;
	}

	void SetValue(state_flags val, CWnd* source)
	{
		m_prev_value = m_value;
		m_value = val;
		OnValueChanged(source);
	}

	void Add(state_flags val, CWnd* source)
	{
		SetValue((state_flags)(m_value | val), source);
	}
	void Remove(state_flags val, CWnd* source)
	{
		SetValue((state_flags)(m_value & ~val), source);
	}
	void Modify(state_flags remove, state_flags add, CWnd* source)
	{
		SetValue((state_flags)((m_value & ~remove) | add), source);
	}

	bool IsNormal() const
	{
		return IsNormal(m_value);
	}
	bool IsFocused() const
	{
		return IsFocused(m_value);
	}
	bool IsMouseOver() const
	{
		return IsMouseOver(m_value);
	}
	bool IsMouseOverClient() const
	{
		return IsMouseOverClient(m_value);
	}
	bool IsMouseOverNonClient() const
	{
		return IsMouseOverNonClient(m_value);
	}
	bool IsMouseDown() const
	{
		return IsMouseDown(m_value);
	}
	bool IsMouseDownClient() const
	{
		return IsMouseDownClient(m_value);
	}
	bool IsMouseDownNonClient() const
	{
		return IsMouseDownNonClient(m_value);
	}
	bool IsInactive() const
	{
		return IsInactive(m_value);
	}
	bool IsPrefixVisible() const
	{
		return IsPrefixVisible(m_value);
	}
	bool IsFocusVisible() const
	{
		return IsFocusVisible(m_value);
	}

	bool PrevWasNormal() const
	{
		return IsNormal(m_prev_value);
	}
	bool PrevWasFocused() const
	{
		return IsFocused(m_prev_value);
	}
	bool PrevWasMouseOver() const
	{
		return IsMouseOver(m_prev_value);
	}
	bool PrevWasMouseOverClient() const
	{
		return IsMouseOverClient(m_prev_value);
	}
	bool PrevWasMouseOverNonClient() const
	{
		return IsMouseOverNonClient(m_prev_value);
	}
	bool PrevWasMouseDown() const
	{
		return IsMouseDown(m_prev_value);
	}
	bool PrevWasMouseDownClient() const
	{
		return IsMouseDownClient(m_prev_value);
	}
	bool PrevWasMouseDownNonClient() const
	{
		return IsMouseDownNonClient(m_prev_value);
	}
	bool PrevWasInactive() const
	{
		return IsInactive(m_prev_value);
	}
	bool PrevWasPrefixVisible() const
	{
		return IsPrefixVisible(m_prev_value);
	}
	bool PrevWasFocusVisible() const
	{
		return IsFocusVisible(m_prev_value);
	}

	bool ChangedNormal() const
	{
		return IsNormal() != PrevWasNormal();
	}
	bool ChangedFocused() const
	{
		return IsFocused() != PrevWasFocused();
	}
	bool ChangedMouseOver() const
	{
		return IsMouseOver() != PrevWasMouseOver();
	}
	bool ChangedMouseOverClient() const
	{
		return IsMouseOverClient() != PrevWasMouseOverClient();
	}
	bool ChangedMouseOverNonClient() const
	{
		return IsMouseOverNonClient() != PrevWasMouseOverNonClient();
	}
	bool ChangedMouseDown() const
	{
		return IsMouseDown() != PrevWasMouseDown();
	}
	bool ChangedMouseDownClient() const
	{
		return IsMouseDownClient() != PrevWasMouseDownClient();
	}
	bool ChangedMouseDownNonClient() const
	{
		return IsMouseDownNonClient() != PrevWasMouseDownNonClient();
	}
	bool ChangedInactive() const
	{
		return IsInactive() != PrevWasInactive();
	}
	bool ChangedPrefixVisible() const
	{
		return IsPrefixVisible() != PrevWasPrefixVisible();
	}
	bool ChangedFocusVisible() const
	{
		return IsFocusVisible() != PrevWasFocusVisible();
	}

	void ToString(WTString& str) const;

	virtual void TrackMouseEvents(CWnd* wnd, DWORD mouse_tracking_flags)
	{
		// Begin tracking mouse events for this HWND so that we get WM_MOUSELEAVE
		// when the user moves the mouse outside this HWND's bounds.
		if (m_active_mouse_tracking_flags == 0 || mouse_tracking_flags & TME_CANCEL)
		{
			if (mouse_tracking_flags & TME_CANCEL)
			{
				// We're about to cancel active mouse tracking, so empty out the stored
				// state.
				m_active_mouse_tracking_flags = 0;
			}
			else
			{
				m_active_mouse_tracking_flags = mouse_tracking_flags;
			}

			TRACKMOUSEEVENT tme;
			tme.cbSize = sizeof(tme);
			tme.dwFlags = mouse_tracking_flags;
			tme.hwndTrack = wnd->GetSafeHwnd();
			tme.dwHoverTime = 0;
			TrackMouseEvent(&tme);
		}
		else if (mouse_tracking_flags != m_active_mouse_tracking_flags)
		{
			TrackMouseEvents(wnd, m_active_mouse_tracking_flags | TME_CANCEL);
			TrackMouseEvents(wnd, mouse_tracking_flags);
		}
	}

	virtual void UpdateMouseOverState(CWnd* wnd)
	{
		m_value = (state_flags)(m_value & ~mouse_over_mask);

		POINT pt;
		if (wnd && ::GetCursorPos(&pt))
		{
			//	GetWindowRect + PtInRect is incorrect as
			//	it does not handle state when controls
			//	do overlap. In such case MOUSELEAVE messages
			//	get called while mouse cursor is still over
			//	rectangle of current control, so mouse over
			//	state remains preserved which is incorrect.
			//
			//	**INCORRECT CODE**
			//	CRect wr;
			//	wnd->GetWindowRect(&wr);
			//	if (wr.PtInRect(pt))
			//
			//  So we must use ChildWindowFromPoint
			//  for proper mouse over handling

			CRect wr;
			wnd->GetWindowRect(&wr);
			if (wr.PtInRect(pt))
			{
				HWND parent = ::GetParent(wnd->m_hWnd);
				bool mouse_over_wnd = true;

				if (parent && m_check_overlaps)
				{
					// Use ChildWindowFromPoint due to possible inconsistency
					// in case of method WindowFromPoint which returns parent
					// window in some specific cases.

					CPoint client_pt = pt;
					::ScreenToClient(parent, &client_pt);

					HWND hWndRet = ::RealChildWindowFromPoint(parent, client_pt);
					if (hWndRet && hWndRet != parent)
						mouse_over_wnd = hWndRet == wnd->m_hWnd;
					else
					{
						// if any visible control overlaps current control 
						// and mouse pointer is over that overlapping control 
						// then current control is not under mouse 

						ThemeUtils::ForEachTopLevelChild(parent, wnd->m_hWnd, [&](HWND hWndChild) {
							// ignore current window
							if (wnd->m_hWnd == hWndChild)
								return true; // continue enumeration

							// ignore invisible windows
							if (::GetDlgCtrlID(hWndChild) != -1 && (::GetWindowLong(hWndChild, GWL_STYLE) & WS_VISIBLE))
							{
								// see if point hits the child window
								CRect rect;
								::GetWindowRect(hWndChild, rect);
								if (rect.PtInRect(pt))
								{
									mouse_over_wnd = false; // not under mouse
									return false; // stop enumeration
								}
							}

							return true; // continue enumeration
						});
					}
				}

				if (mouse_over_wnd)
				{
					CRect cr;
					wnd->GetClientRect(&cr);
					wnd->ClientToScreen(&cr);

					if (cr.PtInRect(pt))
						m_value = (state_flags)(m_value | client_mouse_over);
					else
						m_value = (state_flags)(m_value | non_client_mouse_over);
				}
			}
		}
	}

	virtual void UpdateMouseDownState(CWnd* wnd)
	{
		m_value = (state_flags)(m_value & ~mouse_lb_down_mask);

		if (IsMouseOver() && ::GetKeyState(VK_LBUTTON) & 0x8000)
		{
			if (IsMouseOverClient())
				m_value = (state_flags)(m_value | client_mouse_lb_down);
			else if (IsMouseOverNonClient())
				m_value = (state_flags)(m_value | non_client_mouse_lb_down);
		}
	}

	virtual void ApplyCurrentWindowState(CWnd* wnd)
	{
		m_prev_value = m_value;

		if (wnd)
		{
			if (CWnd::GetFocus() == wnd)
				m_value = (state_flags)(m_value | focused);

			UpdateMouseOverState(wnd);
			UpdateMouseDownState(wnd);

			if (!wnd->IsWindowEnabled())
				m_value = (state_flags)(m_value | inactive);

			LRESULT ui_state = wnd->SendMessage(WM_QUERYUISTATE);

			if (ui_state == 0)
				m_value = (state_flags)(m_value | show_prefix | show_focus);
			else
			{
				if (m_show_accelerators_override || (ui_state & UISF_HIDEACCEL) == 0)
					m_value = (state_flags)(m_value | show_prefix);

				if ((ui_state & UISF_HIDEFOCUS) == 0)
					m_value = (state_flags)(m_value | show_focus);
			}
		}

		if (m_prev_value != m_value)
			OnValueChanged(wnd);
	}

	virtual void OnValueChanged(CWnd* wnd) const
	{
		for (IStateHandler* handler : m_stateHandlers)
			if (handler && handler->IsEnabled())
				handler->OnStateChanged(*this, wnd);
	}

	virtual void OnMessage(UINT message, WPARAM wParam, LPARAM lParam, CWnd* wnd)
	{
		state_flags old_value = m_value;

		switch (message)
		{
		case WM_UPDATEUISTATE: {
			// 				if ((LRESULT)TH_TYPE_Check == wnd->SendMessage(WM_VA_THEMEDTYPE))
			// 					__debugbreak();

			DWORD action = LOWORD(wParam);
			DWORD state = HIWORD(wParam);

			if (action == UIS_CLEAR)
			{
				if (state & UISF_ACTIVE)
					m_value = (state_flags)(m_value | inactive);

				if (!m_show_accelerators_override && (state & UISF_HIDEACCEL))
					m_value = (state_flags)(m_value | show_prefix);

				if (state & UISF_HIDEFOCUS)
					m_value = (state_flags)(m_value | show_focus);
			}
			else
			{
				if (state & UISF_ACTIVE)
					m_value = (state_flags)(m_value & ~inactive);

				if (!m_show_accelerators_override && (state & UISF_HIDEACCEL))
					m_value = (state_flags)(m_value & ~show_prefix);

				if (state & UISF_HIDEFOCUS)
					m_value = (state_flags)(m_value & ~show_focus);
			}
		}
		break;

		case WM_ENABLE:
			if (wParam)
				m_value = (state_flags)(m_value & ~inactive);
			else
				m_value = (state_flags)(m_value | inactive);
			break;

		case WM_SETFOCUS:
			m_value = (state_flags)(m_value | focused);
			break;

		case WM_KILLFOCUS:
			m_value = (state_flags)(m_value & ~focused);
			break;

		case WM_SETCURSOR:
			if (m_update_in_wm_setcursor)
			{
				if ((HWND)wParam != wnd->m_hWnd)
					m_value = (state_flags)(m_value & ~mouse_over_mask);
				else
				{
					DWORD ht = LOWORD(lParam);

					if (ht <= HTNOWHERE)
						break;

					m_value = (state_flags)(m_value & ~mouse_over_mask);

					if (ht == HTCLIENT)
						m_value = (state_flags)(m_value | client_mouse_over);
					else
						m_value = (state_flags)(m_value | non_client_mouse_over);
				}
			}
			break;

		case WM_MOUSEMOVE:
			TrackMouseEvents(wnd, TME_LEAVE);

			m_value = (state_flags)(m_value & ~non_client_mouse_mask);
			m_value = (state_flags)(m_value | client_mouse_over);

			break;

		case WM_NCMOUSEMOVE:
			TrackMouseEvents(wnd, TME_NONCLIENT | TME_LEAVE);

			m_value = (state_flags)(m_value & ~client_mouse_mask);
			m_value = (state_flags)(m_value | non_client_mouse_over);

			break;

		case WM_MOUSELEAVE:
			if (m_extended_mouse_tracking)
			{
				UpdateMouseOverState(wnd);

				if (IsMouseOverNonClient())
					TrackMouseEvents(wnd, TME_NONCLIENT | TME_LEAVE);
				else
					m_active_mouse_tracking_flags = 0;
			}
			else
			{
				m_value = (state_flags)(m_value & ~mouse_all_mask);
				m_active_mouse_tracking_flags = 0;
			}

			break;

		case WM_NCMOUSELEAVE:
			if (m_extended_mouse_tracking)
			{
				UpdateMouseOverState(wnd);

				if (IsMouseOverClient())
					TrackMouseEvents(wnd, TME_LEAVE);
				else
					m_active_mouse_tracking_flags = 0;
			}
			else
			{
				m_value = (state_flags)(m_value & ~mouse_all_mask);
				m_active_mouse_tracking_flags = 0;
			}

			break;

		case WM_LBUTTONDOWN:
			m_value = (state_flags)(m_value & ~mouse_lb_down_mask);
			m_value = (state_flags)(m_value | client_mouse_lb_down);
			break;

		case WM_NCLBUTTONDOWN:
			m_value = (state_flags)(m_value & ~mouse_lb_down_mask);
			m_value = (state_flags)(m_value | non_client_mouse_lb_down);
			break;

		case WM_LBUTTONUP:
		case WM_NCLBUTTONUP:
			m_value = (state_flags)(m_value & ~mouse_lb_down_mask);
			break;
		}

		if (old_value != m_value)
		{
			m_prev_value = old_value;
			OnValueChanged(wnd);
		}
	}

	operator state_flags() const
	{
		return m_value;
	}
};

template <typename WINDOW> class ThemeRenderer;

template <typename WINDOW> class ThemeBehavior;

//////////////////////////////////////////////////////////////////////////
// holds all info about current theming
template <typename WINDOW> class ThemeContext
{
  public:
	typedef ThemeRenderer<WINDOW> ThemeRenderer_t;
	typedef std::shared_ptr<ThemeRenderer_t> RendererP;
	typedef std::shared_ptr<ThemeBehavior<WINDOW>> BehaviorP;
	typedef std::shared_ptr<ThemeUtils::CBrushMap> BrushMapP;
	typedef std::shared_ptr<ThemeUtils::CPenMap> PenMapP;
	typedef std::function<LRESULT(UINT msg, WPARAM wParam, LPARAM lParam)> WndProcFnc;

	ThemeContext(WINDOW* window) : Msg(nullptr), pWnd(window), Result(0), IsEnabled(false), CallDefault(false)
	{
	}

	virtual ~ThemeContext()
	{
	}

	virtual bool IsPaintMessage()
	{
		if (Msg == nullptr)
			return false;

		UINT msg = Msg->message;

		return msg == WM_PAINT || msg == WM_ERASEBKGND || msg == WM_NCPAINT || msg == WM_PRINT || msg == WM_PRINTCLIENT;
	}

	virtual void SetResult(LRESULT rslt, bool call_default = false)
	{
		Result = rslt;
		CallDefault = call_default;
	}

	ItemState State;
	RendererP Renderer;
	BehaviorP Behavior;
	BrushMapP BrushMap; // these maps are shared by default with parent window
	PenMapP PenMap;     // these maps are shared by default with parent window
	const MSG* Msg;
	WINDOW* pWnd;

	LRESULT Result;
	bool IsEnabled;
	bool CallDefault;
	WndProcFnc CallWndProc; // allows you to call base WndProc directly
};

//////////////////////////////////////////////////////////////////////////
// universal theme renderer - implement for each item
// each method can be overridden, is on you what you want and when
template <typename WINDOW> class ThemeRenderer : public IStateHandler
{
  protected:
	enum OptFlags : USHORT
	{
		f_AttachOnCreate = 0x0001,
		f_PreMessages = 0x0002,
		f_PostMessages = 0x0004,
		f_DiscardEraseBG = 0x0008,
		f_BorderInNCPaint = 0x0010,
		f_RedrawOnResize = 0x0020,
		f_DoubleBufferPre = 0x0040,
		f_DoubleBufferPost = 0x0080,
		f_DoubleBuffer = f_DoubleBufferPre | f_DoubleBufferPost,
	};

	USHORT m_options;
	IDpiHandler* m_dpiHandler;
	std::shared_ptr<IDefaultColors> m_default_colors;

	bool IsOpt(OptFlags flag) const
	{
		return (m_options & flag) == flag;
	}

  public:
	// if returns true, renderer is attached in PreSubclassWindow method
	virtual bool AttachOnCreate() const
	{
		return IsOpt(f_AttachOnCreate);
	}

	// if returns true, renderer will get invoked Post* methods
	virtual bool PreMessages() const
	{
		return IsOpt(f_PreMessages);
	}

	// if returns true, renderer will get invoked Pre* methods
	virtual bool PostMessages() const
	{
		return IsOpt(f_PostMessages);
	}

	// if returns true, WM_ERASEBKGND is discarded
	virtual bool DiscardEraseBG() const
	{
		return IsOpt(f_DiscardEraseBG);
	}

	// if returns true, *PaintBorder are invoked in WM_NCPAINT, else in WM_PAINT/WM_DRAWITEM
	virtual bool BorderInNCPaint() const
	{
		return IsOpt(f_BorderInNCPaint);
	}

	// if true, all painting (optionally except NC) is done in WM_PAINT
	virtual bool DoubleBufferPre() const
	{
		return IsOpt(f_DoubleBufferPre);
	}
	virtual bool DoubleBufferPost() const
	{
		return IsOpt(f_DoubleBufferPost);
	}
	virtual bool DoubleBuffer() const
	{
		return IsOpt(f_DoubleBuffer);
	}

	// one can not copy IThemeRenderer
	ThemeRenderer(const ThemeRenderer&) = delete;
	ThemeRenderer& operator=(const ThemeRenderer&) = delete;

	ThemeRenderer(BYTE flags = f_DiscardEraseBG | f_BorderInNCPaint)
	    : m_options(flags), m_dpiHandler(nullptr), m_default_colors(new IDefaultColors())
	{
	}

	virtual ~ThemeRenderer()
	{
	}

	virtual ThemeUtils::CBrushMap& BrushMap(ThemeContext<WINDOW>& context)
	{
		if (context.BrushMap)
			return *context.BrushMap;
		else
			return ThemeUtils::GDIBuffer_BrushMap();
	}

	virtual ThemeUtils::CPenMap& PenMap(ThemeContext<WINDOW>& context)
	{
		if (context.PenMap)
			return *context.PenMap;
		else
			return ThemeUtils::GDIBuffer_PenMap();
	}

	virtual void OnStateChanged(const ItemState& state, CWnd* wnd)
	{
		// 			WTString str;
		// 			state.ToString(str);
		// 			ThemeUtils::TraceFramePrint("ID: %d State: %s", wnd->GetDlgCtrlID(), (LPCTSTR)str);

		// default action, just redraw control :)
		wnd->RedrawWindow(NULL, NULL,
		                  RDW_INVALIDATE | RDW_VALIDATE | RDW_UPDATENOW | RDW_ERASENOW | RDW_ERASE | RDW_FRAME);
	}

	virtual void DrawItem(LPDRAWITEMSTRUCT disP, ThemeContext<WINDOW>& context)
	{
		if (disP)
		{
			context.Result = 0;
			CRect rect(disP->rcItem);
			CDC dc;
			dc.Attach(disP->hDC);
			int saved = dc.SaveDC();

			if (PreMessages())
			{
				if (DoubleBufferPre())
				{
					ThemeUtils::CMemDC memDC(&dc, &rect);
					PrePaintBackground(memDC, rect, context);
					if (!BorderInNCPaint())
						PrePaintBorder(memDC, rect, context);
					PrePaintForeground(memDC, rect, context);
				}
				else
				{
					PrePaintBackground(dc, rect, context);
					if (!BorderInNCPaint())
						PrePaintBorder(dc, rect, context);
					PrePaintForeground(dc, rect, context);
				}
			}

			if (PostMessages())
			{
				if (DoubleBufferPost())
				{
					ThemeUtils::CMemDC memDC(&dc, &rect);
					PostPaintBackground(memDC, rect, context);
					if (!BorderInNCPaint())
						PostPaintBorder(memDC, rect, context);
					PostPaintForeground(memDC, rect, context);
				}
				else
				{
					PostPaintBackground(dc, rect, context);
					if (!BorderInNCPaint())
						PostPaintBorder(dc, rect, context);
					PostPaintForeground(dc, rect, context);
				}
			}

			dc.RestoreDC(saved);
			dc.Detach();
		}
	}

	// if you do not want apply CTLCOLOR, do not override this method
	virtual HBRUSH HandleCtlColor(WINDOW* wnd, CDC* pDC, UINT nCtlColor, ThemeContext<WINDOW>& context)
	{
		return NULL;
	}

	// this is good only for some controls, such as static and so on
	virtual HBRUSH HandleDefaultCtlColor(CWnd* wnd, CDC* pDC, UINT nCtlColor)
	{
		if (m_default_colors.get())
		{
			if (wnd->IsWindowEnabled())
				pDC->SetTextColor(m_default_colors->crDefaultText);
			else
				pDC->SetTextColor(m_default_colors->crDefaultGrayText);

			pDC->SetBkColor(m_default_colors->crDefaultBG);
			return *m_default_colors->brBefaultBG;
		}
		return nullptr;
	}

	// enables implementation to modify styles etc.
	virtual void Attach(WINDOW* wnd, ThemeContext<WINDOW>& context)
	{
		if (m_default_colors.get())
			m_default_colors->ResetDefaultColors();

		m_dpiHandler = dynamic_cast<IDpiHandler*>(wnd); // safer than lookup, called once

		context.State.AddHandler(this);
	}

	virtual void Detach(WINDOW* wnd, ThemeContext<WINDOW>& context)
	{
		context.State.RemoveHandler(this);
		m_dpiHandler = nullptr;
	}

	// draw background or whole control (if not handled in CTLColor)
	virtual void PrePaintBackground(CDC& dc, CRect& rect, ThemeContext<WINDOW>& context)
	{
	}
	virtual void PostPaintBackground(CDC& dc, CRect& rect, ThemeContext<WINDOW>& context)
	{
	}

	// draw foreground or whole control (if not handled in CTLColor)
	virtual void PrePaintForeground(CDC& dc, CRect& rect, ThemeContext<WINDOW>& context)
	{
	}
	virtual void PostPaintForeground(CDC& dc, CRect& rect, ThemeContext<WINDOW>& context)
	{
	}

	// draw border
	virtual void PrePaintBorder(CDC& dc, CRect& rect, ThemeContext<WINDOW>& context)
	{
	}
	virtual void PostPaintBorder(CDC& dc, CRect& rect, ThemeContext<WINDOW>& context)
	{
	}

	// return true if you want get called Pre/Post PaintBackground
	virtual bool IsEraseBGMessage(UINT msg)
	{
		return msg == WM_ERASEBKGND;
	}

	// return true if you want get called Pre/Post PaintForeground
	virtual bool IsPaintMessage(UINT msg)
	{
		return msg == WM_PAINT || msg == WM_SETTEXT || msg == WM_ENABLE;
	}

	// return true if you want get called Pre/Post PaintBorder
	virtual bool IsNCPaintMessage(UINT msg)
	{
		return msg == WM_NCPAINT || msg == WM_SETTEXT || msg == WM_NCACTIVATE;
	}

	// overriding of this method may completely change the behavior of this renderer
	// it is invoked BEFORE WindowProc of sub-classed control
	virtual void WindowProcPre(UINT message, WPARAM wParam, LPARAM lParam, WINDOW* wnd, ThemeContext<WINDOW>& context)
	{
		if (IsEraseBGMessage(message))
		{
			if (DiscardEraseBG() || DoubleBufferPre())
				context.SetResult(TRUE, !context.IsPaintMessage());
			else
			{
				CRect rect;
				wnd->GetClientRect(&rect);
				CDC dc;
				dc.Attach((HDC)wParam);
				PrePaintBackground(dc, rect, context);
				dc.Detach();
			}
		}

		else if (IsPaintMessage(message))
		{
			CRect rect;
			wnd->GetClientRect(&rect);

			CPaintDC dc(wnd);

			if (DoubleBufferPre())
			{
				ThemeUtils::CMemDC memDC(&dc, &rect);
				PrePaintBackground(memDC, rect, context);
				if (!BorderInNCPaint())
					PrePaintBorder(memDC, rect, context);
				PrePaintForeground(memDC, rect, context);
			}
			else
			{
				if (DiscardEraseBG())
					PrePaintBackground(dc, rect, context);
				if (!BorderInNCPaint())
					PrePaintBorder(dc, rect, context);
				PrePaintForeground(dc, rect, context);
			}
		}

		else if (IsNCPaintMessage(message) && BorderInNCPaint())
		{
			CRect rcBorder;
			wnd->GetWindowRect(rcBorder);
			rcBorder.bottom = rcBorder.Height();
			rcBorder.right = rcBorder.Width();
			rcBorder.left = rcBorder.top = 0;

			CWindowDC dc(wnd);
			PrePaintBorder(dc, rcBorder, context);
		}
	}

	// overriding of this method may completely change the behavior of this renderer
	// it is invoked AFTER WindowProc of sub-classed control
	virtual void WindowProcPost(UINT message, WPARAM wParam, LPARAM lParam, WINDOW* wnd, ThemeContext<WINDOW>& context)
	{
		if (IsEraseBGMessage(message))
		{
			if (DiscardEraseBG() || DoubleBufferPost())
				context.SetResult(TRUE, !context.IsPaintMessage());
			else
			{
				CRect rect;
				wnd->GetClientRect(&rect);
				CDC dc;
				dc.Attach((HDC)wParam);
				PostPaintBackground(dc, rect, context);
				dc.Detach();
			}
		}

		else if (IsPaintMessage(message))
		{
			CRect rect;
			CPaintDC dc(wnd);
			wnd->GetClientRect(&rect);

			if (DoubleBufferPost())
			{
				ThemeUtils::CMemDC memDC(&dc, &rect);
				PostPaintBackground(memDC, rect, context);
				if (!BorderInNCPaint())
					PostPaintBorder(memDC, rect, context);
				PostPaintForeground(memDC, rect, context);
			}
			else
			{
				if (DiscardEraseBG())
					PostPaintBackground(dc, rect, context);
				if (!BorderInNCPaint())
					PostPaintBorder(dc, rect, context);
				PostPaintForeground(dc, rect, context);
			}
		}

		else if (IsNCPaintMessage(message) && BorderInNCPaint())
		{
			CWindowDC dc(wnd);
			CRect rcBorder;
			wnd->GetWindowRect(rcBorder);
			rcBorder.OffsetRect(-rcBorder.left, -rcBorder.top);
			PostPaintBorder(dc, rcBorder, context);
		}
	}
};

template <typename WINDOW> class ThemeBehavior : public IStateHandler
{
	// one can not copy IBehavior
	ThemeBehavior(const ThemeBehavior&) = delete;
	ThemeBehavior& operator=(const ThemeBehavior&) = delete;

  public:
	ThemeBehavior()
	{
	}
	virtual ~ThemeBehavior()
	{
	}

	virtual bool AttachOnCreate()
	{
		return false;
	}
	virtual void Attach(WINDOW* wnd, ThemeContext<WINDOW>& context)
	{
		context.State.AddHandler(this);
	}
	virtual void Detach(WINDOW* wnd, ThemeContext<WINDOW>& context)
	{
		context.State.RemoveHandler(this);
	}
	virtual void OnStateChanged(const ItemState& state, CWnd* wnd)
	{
	} // eliminate default redrawing
	virtual void OnMessagePre(UINT message, WPARAM wParam, LPARAM lParam, WINDOW* wnd, ThemeContext<WINDOW>& context){};
	virtual void OnMessagePost(UINT message, WPARAM wParam, LPARAM lParam, WINDOW* wnd,
	                           ThemeContext<WINDOW>& context){};
};

class VADlgIDE11Draw : public ThemeRenderer<CDialog>
{
  protected:
	COLORREF crBG;
	COLORREF crTxt;
	bool enabled;
	HWND size_box;
	bool size_box_visible_on_attach;

  public:
	VADlgIDE11Draw();
	virtual bool PreMessages() const
	{
		return enabled && __super::PreMessages();
	}
	virtual void PrePaintBackground(CDC& dc, CRect& rect, ThemeContext<CDialog>& context);
	virtual void Attach(CDialog* wnd, ThemeContext<CDialog>& context);
	virtual void Detach(CDialog* wnd, ThemeContext<CDialog>& context);

	virtual void PrePaintForeground(CDC& dc, CRect& rect, ThemeContext<CDialog>& context);
	virtual HBRUSH HandleCtlColor(CDialog* wnd, CDC* pDC, UINT nCtlColor, ThemeContext<CDialog>& context);

	virtual void WindowProcPre(UINT message, WPARAM wParam, LPARAM lParam, CDialog* wnd,
	                           ThemeContext<CDialog>& context);
	void ShowSizeBox(CDialog* wnd, bool show);

	virtual void OnStateChanged(const ItemState& state, CWnd* wnd);

	virtual bool IsEnabled() const
	{
		return enabled;
	}

	virtual bool IsEraseBGMessage(UINT msg)
	{
		return msg == WM_ERASEBKGND;
	}
	virtual bool IsPaintMessage(UINT msg)
	{
		return msg == WM_PAINT;
	}
	virtual bool IsNCPaintMessage(UINT msg)
	{
		return msg == WM_NCPAINT;
	}
};

class BtnIDE11Draw : public ThemeRenderer<CButton>
{
  protected:
#if defined(RAD_STUDIO)
	COLORREF colors[13];
#else
	COLORREF colors[12];
#endif
	COLORREF focus_border;
	CButton* button;
	WndStyles styles;
	bool modifyStyles;

  public:
	BtnIDE11Draw(bool modify_styles = false)
	    : ThemeRenderer<CButton>(f_DiscardEraseBG | f_BorderInNCPaint | f_PreMessages | f_PostMessages |
	                             f_DoubleBufferPre),
	      focus_border(0), button(0), modifyStyles(modify_styles)
	{
		memset(colors, 0, sizeof(colors));
	}

	virtual bool IsEnabled() const
	{
		return button != nullptr;
	}

	virtual void Attach(CButton* wnd, ThemeContext<CButton>& context);
	virtual void Detach(CButton* wnd, ThemeContext<CButton>& context);

	virtual bool PreMessages() const
	{
		return button != nullptr && __super::PreMessages();
	}
	virtual bool BorderInNCPaint() const
	{
		return false;
	}
	virtual void PrePaintBackground(CDC& dc, CRect& rect, ThemeContext<CButton>& context);
	virtual void WindowProcPost(UINT message, WPARAM wParam, LPARAM lParam, CButton* wnd,
	                            ThemeContext<CButton>& context);
};

class BtnIDE11DrawIcon : public BtnIDE11Draw
{
  public:
	BtnIDE11DrawIcon()
	    : BtnIDE11Draw()
	{
	}

	virtual void PrePaintBackground(CDC& dc, CRect& rect, ThemeContext<CButton>& context) override;
};

template <typename WINDOW> struct DefaultBehavior final : public ThemeBehavior<WINDOW>
{
	virtual ~DefaultBehavior()
	{
	}
	virtual bool IsEnabled() const
	{
		return false;
	}
	virtual void Attach(WINDOW* wnd, ThemeContext<WINDOW>& context){};
	virtual void Detach(WINDOW* wnd, ThemeContext<WINDOW>& context){};
	virtual void OnMessagePre(UINT message, WPARAM wParam, LPARAM lParam, WINDOW* wnd, ThemeContext<WINDOW>& context){};
	virtual void OnMessagePost(UINT message, WPARAM wParam, LPARAM lParam, WINDOW* wnd,
	                           ThemeContext<WINDOW>& context){};
};

class CheckBehavior : public ThemeBehavior<CButton>
{
  protected:
	int check_state;
	bool enabled;
	WndStyles styles;
	bool dont_click_on_focus;

  public:
	CheckBehavior() : check_state(BST_UNCHECKED), enabled(false), dont_click_on_focus(false)
	{
	}
	virtual ~CheckBehavior()
	{
	}

	virtual void OnMessagePre(UINT message, WPARAM wParam, LPARAM lParam, CButton* wnd, ThemeContext<CButton>& context);
	virtual void OnMessagePost(UINT message, WPARAM wParam, LPARAM lParam, CButton* wnd,
	                           ThemeContext<CButton>& context);

	virtual bool IsEnabled() const
	{
		return enabled;
	}

	virtual void Attach(CButton* wnd, ThemeContext<CButton>& context);
	virtual void Detach(CButton* wnd, ThemeContext<CButton>& context);
};

class RadioBehavior : public CheckBehavior
{
  public:
	RadioBehavior() : CheckBehavior()
	{
	}
	virtual ~RadioBehavior()
	{
	}

	bool IsAutoRadioButton(CWnd* wnd);
	virtual void OnMessagePre(UINT message, WPARAM wParam, LPARAM lParam, CButton* wnd, ThemeContext<CButton>& context);
	virtual void OnMessagePost(UINT message, WPARAM wParam, LPARAM lParam, CButton* wnd,
	                           ThemeContext<CButton>& context);
};

class CheckIDE11Draw : public ThemeRenderer<CButton>
{
  protected:
	COLORREF colors[18];
	CButton* button;
	WndStyles styles;
	ItemState::state_flags m_BM_SETSTATE;
	ItemState::state_flags m_BM_DONT_CLICK;
	std::shared_ptr<BtnIDE11Draw> btnDraw;

  public:
	CheckIDE11Draw()
	    : ThemeRenderer<CButton>(f_DiscardEraseBG | f_BorderInNCPaint | f_PreMessages | f_PostMessages |
	                             f_DoubleBufferPre),
	      button(0), m_BM_SETSTATE(ItemState::none), m_BM_DONT_CLICK(ItemState::none)
	{
		memset(colors, 0, sizeof(colors));
	}

	virtual void Attach(CButton* wnd, ThemeContext<CButton>& context);
	virtual void Detach(CButton* wnd, ThemeContext<CButton>& context);

	virtual bool IsEnabled() const
	{
		return button != nullptr;
	}

	virtual HBRUSH HandleCtlColor(CButton* wnd, CDC* pDC, UINT nCtlColor, ThemeContext<CButton>& context);
	virtual bool PreMessages() const
	{
		return button != nullptr && __super::PreMessages();
	}
	virtual void PrePaintBackground(CDC& dc, CRect& rect, ThemeContext<CButton>& context);

	void GetColors(COLORREF& bg, COLORREF& glyph, COLORREF& border, COLORREF& text, ThemeContext<CButton>& context);
	virtual void DrawCheck(CDC& dc, COLORREF bg, COLORREF border, COLORREF glyph, CRect& chbRect, int state);
	virtual void WindowProcPost(UINT message, WPARAM wParam, LPARAM lParam, CButton* wnd,
	                            ThemeContext<CButton>& context);
	virtual void WindowProcPre(UINT message, WPARAM wParam, LPARAM lParam, CButton* wnd,
	                           ThemeContext<CButton>& context);
};

class RadioIDE11Draw : public CheckIDE11Draw
{
	virtual void DrawCheck(CDC& dc, COLORREF bg, COLORREF border, COLORREF glyph, CRect& chbRect, int state);
};

class GroupIDE11Draw : public ThemeRenderer<CButton>
{
  protected:
	COLORREF colors[3];
	CButton* button;
	WndStyles styles;

  public:
	GroupIDE11Draw()
	    : ThemeRenderer<CButton>(f_DiscardEraseBG | f_PreMessages | f_PostMessages | f_DoubleBufferPre), button(nullptr)
	{
		memset(colors, 0, sizeof(colors));
	}

	virtual void Attach(CButton* wnd, ThemeContext<CButton>& context);
	virtual void Detach(CButton* wnd, ThemeContext<CButton>& context);

	virtual bool IsEnabled() const
	{
		return button != nullptr;
	}

	virtual bool PreMessages() const
	{
		return button != nullptr && __super::PreMessages();
	}
	virtual bool BorderInNCPaint() const
	{
		return false;
	}
	virtual void PrePaintForeground(CDC& dc, CRect& rect, ThemeContext<CButton>& context);
	virtual void PrePaintBackground(CDC& dc, CRect& rect, ThemeContext<CButton>& context)
	{
		PrePaintForeground(dc, rect, context);
	}

	virtual void WindowProcPre(UINT message, WPARAM wParam, LPARAM lParam, CButton* wnd,
	                           ThemeContext<CButton>& context);
	virtual void WindowProcPost(UINT message, WPARAM wParam, LPARAM lParam, CButton* wnd,
	                            ThemeContext<CButton>& context);
};

template <typename BASE = CWnd, bool static_gray = true> class CTL_IDE11Draw : public ThemeRenderer<BASE>
{
  protected:
	COLORREF colors[3];
	BASE* ctrl;

  public:
	CTL_IDE11Draw() : ThemeRenderer<BASE>(0), ctrl(nullptr)
	{
		memset(colors, 0, sizeof(colors));
	}

	virtual bool IsEnabled() const
	{
		return ctrl != nullptr;
	}

	virtual HBRUSH HandleCtlColor(BASE* wnd, CDC* pDC, UINT nCtlColor, ThemeContext<BASE>& context)
	{
		if (ctrl != nullptr)
		{
			if (nCtlColor == CTLCOLOR_STATIC && static_gray)
				pDC->SetTextColor(colors[2]);
			else
				pDC->SetTextColor(colors[1]);

			pDC->SetBkColor(colors[0]);
			return __super::BrushMap(context).GetHBRUSH(colors[0]);
		}

		return nullptr;
	}

	virtual void Attach(BASE* wnd, ThemeContext<BASE>& context)
	{
		__super::Attach(wnd, context);

		if (g_IdeSettings)
		{
			ctrl = wnd;

			const GUID& npc = ThemeCategory11::NewProjectDialog;

			// Label area BG
			colors[0] = g_IdeSettings->GetThemeColor(npc, L"BackgroundLowerRegion", FALSE);
			colors[1] = g_IdeSettings->GetEnvironmentColor(L"WindowText", FALSE);
			colors[2] = g_IdeSettings->GetEnvironmentColor(L"GrayText", FALSE);
		}
	}

	virtual void Detach(BASE* wnd, ThemeContext<BASE>& context)
	{
		ctrl = nullptr;
		__super::Detach(wnd, context);
	}
};

template <bool applyTheme = false, bool forceUnderline = false, bool arrowsNavigation = true>
class StaticLinkDraw : public ThemeRenderer<CStatic>
{
  protected:
	COLORREF colors[4];
	CStatic* ctrl = nullptr;
	CFontW underlineFont;
	HCURSOR hHandCursor = nullptr;

  public:
	StaticLinkDraw() : ThemeRenderer<CStatic>(f_PreMessages)
	{
		memset(colors, 0, sizeof(colors));

		if (!hHandCursor)
			hHandCursor = ::LoadCursor(nullptr, IDC_HAND);
	}

	~StaticLinkDraw()
	{
		if (hHandCursor)
		{
			::DestroyCursor(hHandCursor);
			hHandCursor = nullptr;
		}
	}

	bool IsEnabled() const override
	{
		return ctrl != nullptr;
	}

	void WindowProcPre(UINT message, WPARAM wParam, LPARAM lParam, CStatic* wnd,
	                   ThemeContext<CStatic>& context) override
	{
		// case: 142819 - update underline font
		if (message == WM_SETFONT)
		{
			underlineFont.DeleteObject();

			if (wnd && !(HFONT)underlineFont)
			{
				auto hFont = (HFONT)wParam;
				if (hFont)
				{
					LOGFONTW lf;
					if (::GetObjectW(hFont, sizeof(lf), &lf))
					{
						lf.lfUnderline = TRUE;
						underlineFont.Attach(::CreateFontIndirectW(&lf));
					}
				}
			}
		}

		if (message == WM_SETCURSOR && hHandCursor)
		{
			// Note: context.State already handled the message, we can trust it
			if (context.State.IsMouseOver())
			{
				SetCursor(hHandCursor);
				context.SetResult(TRUE, false);
			}
		}
		// generate STN_CLICKED on VK_SPACE and VK_RETURN
		else if (message == WM_GETDLGCODE && (wParam == VK_RETURN || wParam == VK_SPACE))
		{
			context.SetResult(DLGC_WANTALLKEYS | (LRESULT)context.CallWndProc(message, wParam, lParam), false);
		}
		else if (message == WM_KEYDOWN && (wParam == VK_SPACE || wParam == VK_RETURN))
		{
			auto parent = wnd->GetParent();
			if (parent)
				parent->SendMessage(WM_COMMAND, MAKEWPARAM(wnd->GetDlgCtrlID(), STN_CLICKED), (LPARAM)wnd->m_hWnd);
		}

		if (arrowsNavigation)
		{
			if (message == WM_GETDLGCODE && (wParam >= VK_LEFT && wParam <= VK_DOWN))
			{
				context.SetResult(DLGC_WANTARROWS | (LRESULT)context.CallWndProc(message, wParam, lParam), false);
			}
			else if (message == WM_KEYDOWN && (wParam >= VK_LEFT && wParam <= VK_DOWN))
			{
				if (wParam == VK_UP || wParam == VK_LEFT)
				{
					auto dlg = ThemeUtils::FindParentDialog(wnd);
					if (dlg)
					{
						dlg->PrevDlgCtrl();
						context.SetResult(TRUE, false);
					}
				}
				else if (wParam == VK_DOWN || wParam == VK_RIGHT)
				{
					auto dlg = ThemeUtils::FindParentDialog(wnd);
					if (dlg)
					{
						dlg->NextDlgCtrl();
						context.SetResult(TRUE, false);
					}
				}
			}
		}
	}

	HBRUSH HandleCtlColor(CStatic* wnd, CDC* pDC, UINT nCtlColor, ThemeContext<CStatic>& context) override
	{
		if (ctrl != nullptr)
		{
			bool focus = context.State.IsFocused() && context.State.IsFocusVisible();

			COLORREF txtColor = colors[1];
			// set the text color
			if (context.State.IsMouseDown())
				txtColor = colors[3];
			else if (focus)
				txtColor = colors[2];
			else
				txtColor = colors[1];

			if (context.State.IsMouseOver())
			{
#pragma warning(push)
#pragma warning(disable : 4127)
				if (!applyTheme || !CVS2010Colours::IsExtendedThemeActive())
					txtColor = ThemeUtils::BrightenColor(txtColor, 15);
				else
					txtColor = colors[2];
#pragma warning(pop)
			}

			pDC->SetTextColor(txtColor);

			// make the underline visible
			if ((context.State.IsMouseOver() || focus || forceUnderline) && (HFONT)underlineFont)
				pDC->SelectObject(&underlineFont);

			pDC->SetBkColor(colors[0]);
			return BrushMap(context).GetHBRUSH(colors[0]);
		}

		return nullptr;
	}

	void Attach(CStatic* wnd, ThemeContext<CStatic>& context) override
	{
		DWORD styles = WS_TABSTOP | SS_NOTIFY;
		// we want to get messages for the ItemState class
		if (wnd && styles != (wnd->GetStyle() & styles))
			wnd->ModifyStyle(0, styles);

		// get state updated in WM_SETCURSOR
		context.State.SetUpdateInWM_SETCURSOR(true);
		// context.State.SetCheckOverlaps(false);

		__super::Attach(wnd, context);

		// create the font copy with underline
		if (wnd && !(HFONT)underlineFont)
		{
			auto hFont = (HFONT)wnd->SendMessage(WM_GETFONT, 0, 0);
			if (hFont)
			{
				LOGFONTW lf;
				if (::GetObjectW(hFont, sizeof(lf), &lf))
				{
					lf.lfUnderline = TRUE;
					underlineFont.Attach(::CreateFontIndirectW(&lf));
				}
			}
		}

		// fill colors used to draw the control
		if (g_IdeSettings)
		{
			ctrl = wnd;

			const GUID& npc = ThemeCategory11::NewProjectDialog;

#pragma warning(push)
#pragma warning(disable : 4127)
			if (applyTheme && CVS2010Colours::IsExtendedThemeActive())
			{
				// Label area BG
				colors[0] = g_IdeSettings->GetThemeColor(npc, L"BackgroundLowerRegion", FALSE);
				colors[1] = g_IdeSettings->GetEnvironmentColor(L"PanelHyperlink", FALSE);
				colors[2] = g_IdeSettings->GetEnvironmentColor(L"PanelHyperlinkHover", FALSE);
				colors[3] = g_IdeSettings->GetEnvironmentColor(L"PanelHyperlinkPressed", FALSE);
			}
			else
			{
				ThemeUtils::CXTheme theme;

				auto getSysColor = [&theme](int index) {
					if (theme.IsAppThemed())
						return theme.GetSysColor(index);
					else
						return ::GetSysColor(index);
				};

				bool isBlackHContrast =
				    ::GetSysColor(COLOR_3DLIGHT) == RGB(255, 255, 255) && ::GetSysColor(COLOR_3DFACE) == RGB(0, 0, 0);
				if (isBlackHContrast)
				{
					colors[0] = getSysColor(COLOR_WINDOWTEXT);
					colors[1] = colors[0];
					colors[2] = colors[0];
					colors[3] = colors[0];
				}
				else
				{
					colors[0] = getSysColor(COLOR_BTNFACE);
					colors[1] = getSysColor(COLOR_HOTLIGHT);
					colors[2] = RGB(0, 0, 255);
					colors[3] = colors[2];
				}
			}
#pragma warning(pop)
		}
	}

	void Detach(CStatic* wnd, ThemeContext<CStatic>& context) override
	{
		ctrl = nullptr;
		__super::Detach(wnd, context);
	}
};

class ListBoxIDE11Draw : public ThemeRenderer<CListBox>
{
  protected:
	COLORREF colors[5];
	CListBox* ctrl;
	WndStyles styles;

  public:
	ListBoxIDE11Draw() : ThemeRenderer<CListBox>(f_BorderInNCPaint | f_PostMessages), ctrl(nullptr)
	{
		memset(colors, 0, sizeof(colors));
	}

	virtual bool IsEnabled() const
	{
		return ctrl != nullptr;
	}

	virtual void PostPaintBorder(CDC& dc, CRect& rect, ThemeContext<CListBox>& context);
	virtual HBRUSH HandleCtlColor(CListBox* wnd, CDC* pDC, UINT nCtlColor, ThemeContext<CListBox>& context);
	virtual void Attach(CListBox* wnd, ThemeContext<CListBox>& context);
	virtual void Detach(CListBox* wnd, ThemeContext<CListBox>& context);

	virtual void OnStateChanged(const ItemState& state, CWnd* wnd);
};

class EditIDE11Draw : public ThemeRenderer<CEdit>
{
  public:
	enum class ColorIndex
	{
		Normal = 0,
		MouseOver = 3,
		Active = 6,
		Disabled = 9
	};

  protected:
	COLORREF colors[16];
	CEdit* edit;
	ColorIndex bg_index;
	ItemState::state_flags m_ES_READONLY;

	std::shared_ptr<std::map<ColorIndex, COLORREF>> overrideTextColors;

	COLORREF GetTextColor(ColorIndex index);

  public:
	EditIDE11Draw()
	    : ThemeRenderer<CEdit>(f_BorderInNCPaint | f_PostMessages | f_PreMessages), edit(nullptr),
	      bg_index(ColorIndex::Normal), m_ES_READONLY(ItemState::none)
	{
		memset(colors, 0, sizeof(colors));
	}

	void RemoveTextOverrideColor(ColorIndex index, bool redraw = true);
	void SetTextOverrideColor(ColorIndex index, COLORREF color, bool redraw = true);

	virtual bool IsEnabled() const
	{
		return edit != nullptr;
	}
	virtual void Attach(CEdit* wnd, ThemeContext<CEdit>& context);
	virtual void Detach(CEdit* wnd, ThemeContext<CEdit>& context);
	virtual HBRUSH HandleCtlColor(CEdit* wnd, CDC* pDC, UINT nCtlColor, ThemeContext<CEdit>& context);
	virtual void OnStateChanged(const ItemState& state, CWnd* wnd);

	virtual void PostPaintBorder(CDC& dc, CRect& rect, ThemeContext<CEdit>& context);
	virtual void PostPaintForeground(CDC& dc, CRect& rect, ThemeContext<CEdit>& context);
	virtual void PostPaintBackground(CDC& dc, CRect& rect, ThemeContext<CEdit>& context);
	virtual void WindowProcPost(UINT message, WPARAM wParam, LPARAM lParam, CEdit* wnd, ThemeContext<CEdit>& context);

	virtual void WindowProcPre(UINT message, WPARAM wParam, LPARAM lParam, CEdit* wnd, ThemeContext<CEdit>& context);
};

class EditBrowseDraw : public ThemeRenderer<CMFCEditBrowseCtrl>
{
  protected:
	ThemeUtils::ColorSet colors[12];
	ThemeUtils::ColorSet btn_colors[4];
	DWORD last_update_ticks;
	ThemeUtils::CXThemePtr xtheme;

	CMFCEditBrowseCtrl* edit;
	int bg_index;
	ItemState::state_flags m_ES_READONLY;

	UINT_PTR TimerID();
	void DoRedraw(bool force = false);

  public:
	EditBrowseDraw()
	    : ThemeRenderer<CMFCEditBrowseCtrl>(f_BorderInNCPaint | f_PostMessages | f_PreMessages), edit(nullptr),
	      bg_index(0), m_ES_READONLY(ItemState::none)
	{
	}

	COLORREF GetColorExt(std::initializer_list<std::wstring> names)
	{
		const GUID& cat = ThemeCategory11::SearchControl;

		for (auto& name : names)
		{
			COLORREF clr = UINT_MAX;

			LPCWSTR pName = name.c_str();
			BOOL fore = wcsncmp(name.c_str(), L"FG.", 3) == 0 ? TRUE : FALSE;
			if (fore || wcsncmp(name.c_str(), L"BG.", 3) == 0)
				pName += 3;

			if (wcsncmp(pName, L"ENV.", 4) == 0)
				clr = g_IdeSettings->GetEnvironmentColor(pName + 4, fore);
			else if (wcsncmp(pName, L"NPD.", 4) == 0)
				clr = g_IdeSettings->GetThemeColor(ThemeCategory11::NewProjectDialog, pName + 4, fore);
			else
				clr = g_IdeSettings->GetThemeColor(cat, pName, fore);

			if (clr != UINT_MAX)
				return clr;
		}

		_ASSERTE(!"Could not find any of alternatives!");

		return UINT_MAX;
	}

	virtual bool IsEnabled() const
	{
		return edit != nullptr;
	}
	virtual void Attach(CMFCEditBrowseCtrl* wnd, ThemeContext<CMFCEditBrowseCtrl>& context);
	virtual void Detach(CMFCEditBrowseCtrl* wnd, ThemeContext<CMFCEditBrowseCtrl>& context);
	virtual HBRUSH HandleCtlColor(CMFCEditBrowseCtrl* wnd, CDC* pDC, UINT nCtlColor,
	                              ThemeContext<CMFCEditBrowseCtrl>& context);
	virtual void OnStateChanged(const ItemState& state, CWnd* wnd);

	virtual void PostPaintBorder(CDC& dc, CRect& rect, ThemeContext<CMFCEditBrowseCtrl>& context);
	virtual void PostPaintForeground(CDC& dc, CRect& rect, ThemeContext<CMFCEditBrowseCtrl>& context);
	virtual void PostPaintBackground(CDC& dc, CRect& rect, ThemeContext<CMFCEditBrowseCtrl>& context);
	virtual void WindowProcPost(UINT message, WPARAM wParam, LPARAM lParam, CMFCEditBrowseCtrl* wnd,
	                            ThemeContext<CMFCEditBrowseCtrl>& context);

	virtual void WindowProcPre(UINT message, WPARAM wParam, LPARAM lParam, CMFCEditBrowseCtrl* wnd,
	                           ThemeContext<CMFCEditBrowseCtrl>& context);
};

// extended theming of CDimEditCtrl
class DimEditIDE11Draw : public ThemeRenderer<CDimEditCtrl>
{
  protected:
	COLORREF colors[16];
	CDimEditCtrl* edit;

  public:
	DimEditIDE11Draw() : ThemeRenderer<CDimEditCtrl>(f_BorderInNCPaint), edit(nullptr)
	{
		memset(colors, 0, sizeof(colors));
	}
	virtual bool IsEnabled() const
	{
		return edit != nullptr;
	}
	virtual void Attach(CDimEditCtrl* wnd, ThemeContext<CDimEditCtrl>& context);
	virtual void Detach(CDimEditCtrl* wnd, ThemeContext<CDimEditCtrl>& context);
	void SetColors(const ItemState& state);
	virtual HBRUSH HandleCtlColor(CDimEditCtrl* wnd, CDC* pDC, UINT nCtlColor, ThemeContext<CDimEditCtrl>& context);
	virtual void OnStateChanged(const ItemState& state, CWnd* wnd);
};

template <typename WINDOW> class CThemeHandler : public ThemeContext<WINDOW>
{
  protected:
	using BASE = ThemeContext<WINDOW>;

	typedef std::shared_ptr<IWndLifeTime> IWndLifeTimePtr;
	std::vector<IWndLifeTimePtr> m_subclassers;
	std::set<UINT> m_ctl_ids;

  public:
	CThemeHandler(WINDOW* window) : ThemeContext<WINDOW>(window)
	{
	}
	virtual ~CThemeHandler()
	{
	}

	template <typename SUBCLASSER> SUBCLASSER* AddThemedSubclasserForDlgItem(UINT nId, WINDOW* parent)
	{
		SUBCLASSER* btn = nullptr;
		IWndLifeTimePtr ptr(new WndLifeTime<SUBCLASSER>(btn = new SUBCLASSER()));
		if (btn && btn->SubclassDlgItem(nId, parent))
		{
			m_subclassers.push_back(ptr);
			return btn;
		}
		return nullptr;
	}

	void AddDlgItemForDefaultTheming(UINT nId)
	{
		m_ctl_ids.insert(nId);
	}

	bool IsDlgItemForDefaultTheming(UINT nId)
	{
		return m_ctl_ids.find(nId) != m_ctl_ids.end();
	}

	HBRUSH HandleCtlColor(WINDOW* wnd, CDC* pDC, UINT nCtlColor)
	{
		if (!BASE::IsEnabled)
			return nullptr;

		if (BASE::Renderer)
			return BASE::Renderer->HandleCtlColor(wnd, pDC, nCtlColor, *this);

		return nullptr;
	}

	void HandleDrawItem(LPDRAWITEMSTRUCT disP)
	{
		if (BASE::IsEnabled && BASE::Renderer)
			BASE::Renderer->DrawItem(disP, *this);
	}

	void HandleWindowProcPre(UINT message, WPARAM wParam, LPARAM lParam, WINDOW* wnd)
	{
		//////////////////////////////////////////////////////////////////////////
		// handle WM_VA_APPLYTHEME
		if (message == WM_VA_APPLYTHEME)
		{
			bool enable = wParam != 0;

			if (enable != BASE::IsEnabled)
			{
				BASE::IsEnabled = enable;

				if (BASE::IsEnabled)
				{
					BASE::State.ApplyCurrentWindowState(wnd);

					// Attach to parent's BrushMap and PenMap
					CWnd* parent = wnd->GetParent();
					if (parent)
					{
						// Window message is used, because it can be sent also to subclassed control,
						ThemeContext<CDialog>* parent_context =
						    reinterpret_cast<ThemeContext<CDialog>*>(parent->SendMessage(WM_VA_GETTHEMECONTEXT));

						if (parent_context)
						{
							BASE::BrushMap = parent_context->BrushMap;
							BASE::PenMap = parent_context->PenMap;
						}
					}
				}
				else
				{
					BASE::BrushMap.reset();
					BASE::PenMap.reset();
				}

				if (BASE::Behavior)
				{
					if (enable)
						BASE::Behavior->Attach(wnd, *this);
					else
						BASE::Behavior->Detach(wnd, *this);
				}

				if (BASE::Renderer)
				{
					if (enable)
						BASE::Renderer->Attach(wnd, *this);
					else
						BASE::Renderer->Detach(wnd, *this);
				}

				wnd->RedrawWindow(NULL, NULL,
				                  RDW_INVALIDATE | RDW_VALIDATE | RDW_UPDATENOW | RDW_ERASENOW | RDW_ERASE | RDW_FRAME);
			}

			BASE::SetResult(TRUE);
			return;
		}

		BASE::State.OnMessage(message, wParam, lParam, wnd);

		if (BASE::Behavior && BASE::Behavior->IsEnabled())
			BASE::Behavior->OnMessagePre(message, wParam, lParam, wnd, *this);

		if (BASE::Renderer && BASE::Renderer->IsEnabled() && BASE::Renderer->PreMessages())
			BASE::Renderer->WindowProcPre(message, wParam, lParam, wnd, *this);
	}

	void HandleWindowProcPost(UINT message, WPARAM wParam, LPARAM lParam, WINDOW* wnd)
	{
		if (BASE::Behavior && BASE::Behavior->IsEnabled())
			BASE::Behavior->OnMessagePost(message, wParam, lParam, wnd, *this);

		if (BASE::Renderer && BASE::Renderer->IsEnabled() && BASE::Renderer->PostMessages())
			BASE::Renderer->WindowProcPost(message, wParam, lParam, wnd, *this);
	}

	void OnWindowCreated(WINDOW* wnd)
	{
		if (BASE::Behavior && BASE::Behavior->IsEnabled() && BASE::Behavior->AttachOnCreate())
			BASE::Behavior->Attach(wnd, *this);

		if (BASE::Renderer && BASE::Renderer->IsEnabled() && BASE::Renderer->AttachOnCreate())
			BASE::Renderer->Attach(wnd, *this);
	}
};

enum ThemedType
{
	thType_None,
	thType_Dialog,
	thType_Button,
	thType_Check,
	thType_Radio,
	thType_Edit,
	thType_Static,
	thType_Group
};

class CWndTextHelp : public CWnd
{
  public:
	static bool GetText(const CWnd* wnd, CStringW& text)
	{
		if (wnd == nullptr || wnd->m_hWnd == nullptr)
			return false;

		int nLen = GetWindowTextLengthW(wnd->m_hWnd);
		GetWindowTextW(wnd->m_hWnd, text.GetBufferSetLength(nLen), nLen + 1);
		text.ReleaseBuffer();

		return true;
	}

	static bool GetText(const CWnd* wnd, WTString& text)
	{
		if (wnd == nullptr || wnd->m_hWnd == nullptr)
			return false;

		CStringW wstr;
		if (GetText(wnd, wstr))
		{
			text = ::WideToMbcs(wstr, wstr.GetLength());
			return true;
		}

		return false;
	}

	static bool GetText(const CWnd* wnd, CString& text)
	{
		if (wnd == nullptr || wnd->m_hWnd == nullptr)
			return false;

		WTString wtstr;
		if (GetText(wnd, wtstr))
		{
			text = wtstr.c_str();
			return true;
		}

		return false;
	}

	static bool SetText(CWnd* wnd, const CStringW& text)
	{
		if (wnd == nullptr || wnd->m_hWnd == nullptr)
			return false;

		return SetWindowTextW(wnd->m_hWnd, (LPCWSTR)text) != FALSE;
	}

	// sets correct unicode text to control
	static bool SetText(CWnd* wnd, const CString& text)
	{
		if (wnd == nullptr || wnd->m_hWnd == nullptr)
			return false;

		return SetText(wnd, ::MbcsToWide(text, text.GetLength()));
	}

	// sets correct unicode text to control
	static bool SetText(CWnd* wnd, const WTString& text)
	{
		if (wnd == nullptr || wnd->m_hWnd == nullptr)
			return false;

		return SetText(wnd, text.Wide());
	}
};

template <typename BASE> class CMenuXPCtl : public BASE
{
  public:
#pragma warning(push)
#pragma warning(disable : 4191)
	BEGIN_MESSAGE_MAP_INLINE(CMenuXPCtl, BASE)
	ON_MENUXP_MESSAGES()
	ON_WM_CONTEXTMENU()
	END_MESSAGE_MAP_INLINE()
#pragma warning(pop)

	void OnInitMenuPopup(CMenu* pPopupMenu, UINT nIndex, BOOL bSysMenu)
	{
		__super::OnInitMenuPopup(pPopupMenu, nIndex, bSysMenu);
		CMenuXP::SetXPLookNFeel(this, pPopupMenu->m_hMenu, CMenuXP::GetXPLookNFeel(this) /* && !bSysMenu*/);
	}

	void OnMeasureItem(int, LPMEASUREITEMSTRUCT lpMeasureItemStruct)
	{
		CMenuXP::OnMeasureItem(lpMeasureItemStruct);
	}

	void OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct)
	{
		if (!CMenuXP::OnDrawItem(lpDrawItemStruct, BASE::m_hWnd))
		{
			__super::OnDrawItem(nIDCtl, lpDrawItemStruct);
		}
	}

	LRESULT OnMenuChar(UINT nChar, UINT nFlags, CMenu* pMenu)
	{
		if (CMenuXP::IsOwnerDrawn(pMenu->m_hMenu))
		{
			return CMenuXP::OnMenuChar(pMenu->m_hMenu, nChar, nFlags);
		}
		return __super::OnMenuChar(nChar, nFlags, pMenu);
	}

	virtual void OnPopulateContextMenu(CMenu& contextMenu)
	{
	}

	afx_msg void OnContextMenu(CWnd* pWnd, CPoint pos)
	{
		if (!g_FontSettings)
		{
			// [case: 141409]
			__super::OnContextMenu(pWnd, pos);
			return;
		}

		CMenu contextMenu;
		contextMenu.CreatePopupMenu();

		OnPopulateContextMenu(contextMenu);

		CMenuXP::SetXPLookNFeel(this, contextMenu);

		if (pos.x == -1 && pos.y == -1)
		{
			pos = BASE::GetCaretPos();
			BASE::ClientToScreen(&pos);
		}

		MenuXpHook hk(this);
		contextMenu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, pos.x, pos.y, this);
	}
};

template <typename TEventArgs> class OnEvent : public Event<TEventArgs>
{
	virtual bool invoke(TEventArgs& args)
	{
		if (on_invoke)
			return on_invoke(args);

		return false;
	}

  public:
	typedef std::function<bool(TEventArgs& args)> Func;
	Func on_invoke;

	OnEvent(Func fnc) : on_invoke(fnc)
	{
	}
};

template <typename TSender> class OnWMEvent : public Event<WndProcEventArgs<TSender>>
{
	virtual bool invoke(WndProcEventArgs<TSender>& args)
	{
		bool do_call = false;
		switch (WM_list.size())
		{
		case 0:
			do_call = true;
			break;
		case 1:
			do_call = *WM_list.begin() == args.msg;
			break;
		case 2:
			do_call = is_range ? args.msg >= *WM_list.begin() && args.msg <= *WM_list.rbegin()
			                   : WM_list.find(args.msg) != WM_list.end();
			break;
		default:
			do_call = WM_list.find(args.msg) != WM_list.end();
			break;
		}

		if (do_call)
		{
			return on_WM(args);
		}

		return false;
	}

  public:
	typedef std::function<bool(WndProcEventArgs<TSender>& args)> Func;
	Func on_WM;
	std::set<UINT> WM_list;
	bool is_range;

	// fnc is called for each window message
	OnWMEvent(Func fnc) : on_WM(fnc), is_range(false)
	{
	}

	// fnc is called for specific window message
	OnWMEvent(UINT wm, Func fnc) : on_WM(fnc), WM_list({wm}), is_range(false)
	{
	}

	// fnc is called for all window messages within range
	OnWMEvent(UINT wm_first, UINT wm_last, Func fnc) : on_WM(fnc), WM_list({wm_first, wm_last}), is_range(true)
	{
	}

	// fnc is called for specific window messages
	OnWMEvent(const std::initializer_list<UINT>& wms, Func fnc) : on_WM(fnc), WM_list(wms), is_range(false)
	{
	}
};

template <typename TSender> class WndProcEventList : public EventList<WndProcEventArgs<TSender>>
{
  public:
	using BASE = EventList<WndProcEventArgs<TSender>>;
	using EventArgsT = WndProcEventArgs<TSender>;
	using EventT = Event<EventArgsT>;
	using EventPtrT = std::shared_ptr<EventT>;

	bool ProcessWndProc(CWnd* wnd, TSender* sender, LRESULT& rslt, UINT message, WPARAM wParam, LPARAM lParam)
	{
		EventArgsT event_args;
		event_args.wnd = wnd;
		event_args.sender = sender;
		event_args.msg = message;
		event_args.wParam = wParam;
		event_args.lParam = lParam;
		event_args.result = 0;
		event_args.handled = false;

		BASE::invoke(event_args);

		rslt = event_args.result;
		return event_args.handled;
	}

	EventPtrT AddMessageHandler(std::function<bool(EventArgsT&)> fnc)
	{
		return BASE::add(EventPtrT(new OnWMEvent<TSender>(fnc)));
	}

	EventPtrT AddMessageHandler(UINT wm, std::function<bool(EventArgsT&)> fnc)
	{
		return BASE::add(EventPtrT(new OnWMEvent<TSender>(wm, fnc)));
	}

	EventPtrT AddMessageListHandler(const std::initializer_list<UINT>& wms, std::function<bool(EventArgsT&)> fnc)
	{
		return BASE::add(EventPtrT(new OnWMEvent<TSender>(wms, fnc)));
	}

	EventPtrT AddMessageRangeHandler(UINT wm_first, UINT wm_last, std::function<bool(EventArgsT&)> fnc)
	{
		return BASE::add(EventPtrT(new OnWMEvent<TSender>(wm_first, wm_last, fnc)));
	}

	EventPtrT AddCommandHandler(UINT notify_code, std::function<bool(UINT id_from)> fnc)
	{
		return BASE::add(EventPtrT(new OnWMEvent<TSender>(WM_COMMAND, [notify_code, fnc](EventArgsT& args) -> bool {
			UINT id_from;
			if (args.is_command(notify_code, id_from))
				return fnc(id_from);
			return false;
		})));
	}

	EventPtrT AddNotifyHandler(UINT notify_code, std::function<bool(UINT id_from)> fnc)
	{
		return BASE::add(EventPtrT(new OnWMEvent<TSender>(WM_NOTIFY, [notify_code, fnc](EventArgsT& args) -> bool {
			UINT id_from;
			if (args.is_notify(notify_code, id_from))
				return fnc(id_from);
			return false;
		})));
	}

	EventPtrT AddCommandHandler(UINT notify_code, UINT id_from, std::function<bool()> fnc)
	{
		return BASE::add(
		    EventPtrT(new OnWMEvent<TSender>(WM_COMMAND, [notify_code, fnc, id_from](EventArgsT& args) -> bool {
			    UINT _id_from;
			    if (args.is_command(notify_code, _id_from) && _id_from == id_from)
				    return fnc();
			    return false;
		    })));
	}

	EventPtrT AddNotifyHandler(UINT notify_code, UINT id_from, std::function<bool()> fnc)
	{
		return BASE::add(
		    EventPtrT(new OnWMEvent<TSender>(WM_NOTIFY, [notify_code, fnc, id_from](EventArgsT& args) -> bool {
			    UINT _id_from;
			    if (args.is_notify(notify_code, _id_from) && _id_from == id_from)
				    return fnc();
			    return false;
		    })));
	}
};

// To change the traits for any type,
// define specialization for that type.
// ----------------------------------------------------------
// For example:
//
// template<>
// struct ThemeTraits< MyRendererVS2010 >
// {
//		static bool Apply()
//		{
// 			return CVS2010Colours::IsVS2010ColouringActive();
//		}
// };
//

template <typename T> struct ThemeRendererTraits
{
	static bool Apply()
	{
		return CVS2010Colours::IsExtendedThemeActive();
	}
};

template <typename T> struct ThemeBehaviorTraits
{
	static bool Apply()
	{
		return false;
	}
};

class EditBrowseDraw;
template <> struct ThemeRendererTraits<EditBrowseDraw>
{
	static bool Apply()
	{
		// apply regardless IDE or OS, always theme
		return true;
	}
};

template <> struct ThemeRendererTraits<StaticLinkDraw<>>
{
	static bool Apply()
	{
		// apply regardless IDE or OS, always theme
		return true;
	}
};

template <typename BASE, typename RENDERER, ThemedType TH_TYPE, bool ThemeEnableOnCreate = false,
          typename BEHAVIOR = DefaultBehavior<BASE>>
class CThemedCtl : public BASE
{
  protected:
	CThemeHandler<BASE> Theme;

	LPARAM CallWindowProcInternal(UINT msg, WPARAM wParam, LPARAM lParam)
	{
		return BASE::WindowProc(msg, wParam, lParam);
	}

  public:
	CThemedCtl() : BASE(), Theme(this)
	{
		if (ThemeRendererTraits<RENDERER>::Apply())
			Theme.Renderer.reset(new RENDERER());

		if (ThemeBehaviorTraits<BEHAVIOR>::Apply())
			Theme.Behavior.reset(new BEHAVIOR());
	}

	bool IsThemeEnabled() const
	{
		return Theme.IsEnabled;
	}

	void EnableTheme(bool enable)
	{
		Theme.HandleWindowProcPre(WM_VA_APPLYTHEME, WPARAM(enable ? 1 : 0), 0, this);
	}

#pragma warning(push)
#pragma warning(disable : 4191)
	BEGIN_MESSAGE_MAP_INLINE(CThemedCtl, BASE)
	ON_WM_CTLCOLOR_REFLECT()
	END_MESSAGE_MAP_INLINE()
#pragma warning(pop)

	afx_msg HBRUSH CtlColor(CDC* pDC, UINT nCtlColor)
	{
		return Theme.HandleCtlColor(this, pDC, nCtlColor);
	}

	virtual void DrawItem(LPDRAWITEMSTRUCT disP)
	{
		Theme.HandleDrawItem(disP);
	}

	virtual void PreSubclassWindow()
	{
		BASE::PreSubclassWindow();
		Theme.OnWindowCreated(this);

		if (ThemeEnableOnCreate)
			EnableTheme(true);
	}

	WndProcEventList<CThemedCtl> WndProcEvents;
	typedef typename WndProcEventList<CThemedCtl>::EventArgsT EventArgsT;

	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
	{
		if (WndProcEvents)
		{
			LRESULT rslt = 0;
			if (WndProcEvents.ProcessWndProc(this, this, rslt, message, wParam, lParam))
				return rslt;
		}

		if (!ThemeRendererTraits<RENDERER>::Apply() && !ThemeBehaviorTraits<BEHAVIOR>::Apply())
		{
			return BASE::WindowProc(message, wParam, lParam);
		}

		Theme.Result = 0;
		Theme.CallDefault = true;
		if (!Theme.CallWndProc)
		{
			Theme.CallWndProc = [this](UINT msg, WPARAM wParam, LPARAM lParam) {
				return CallWindowProcInternal(msg, wParam, lParam);
			};
		}
		Theme.Msg = BASE::GetCurrentMessage();
		Theme.HandleWindowProcPre(message, wParam, lParam, this);

		if (message == WM_VA_THEMEDTYPE)
			return (LRESULT)TH_TYPE;

		if (message == WM_VA_GETTHEMECONTEXT)
			return (LRESULT)&Theme;

		if (Theme.CallDefault)
			Theme.Result = BASE::WindowProc(message, wParam, lParam);

		Theme.HandleWindowProcPost(message, wParam, lParam, this);
		Theme.Msg = nullptr;
		return Theme.Result;
	}
};

typedef CWndTextW<CThemedCtl<CButton, BtnIDE11Draw, thType_Button>> CThemedButton;
typedef CWndTextW<CThemedCtl<CButton, BtnIDE11DrawIcon, thType_Button>> CThemedButtonIcon;
typedef CWndTextW<CThemedCtl<CButton, CheckIDE11Draw, thType_Check>> CThemedCheckBox;
typedef CWndTextW<CThemedCtl<CButton, RadioIDE11Draw, thType_Radio>> CThemedRadioButton;
typedef CWndTextW<CThemedCtl<CButton, GroupIDE11Draw, thType_Group>> CThemedGroup;
typedef CWndTextW<CThemedCtl<CWnd, CTL_IDE11Draw<CWnd>, thType_Static>> CThemedWnd;
typedef CWndTextW<CThemedCtl<CStatic, CTL_IDE11Draw<CStatic>, thType_Static>> CThemedStatic;
typedef CWndTextW<CThemedCtl<CStatic, CTL_IDE11Draw<CStatic, false>, thType_Static>>
    CThemedStaticNormal; // text is not gray

template <typename _BASE> class CWndTextAutosizeW : public CWndTextW<_BASE>
{
	using BASE = CWndTextW<_BASE>;

  public:
	virtual bool GetTextSize(LPSIZE pSize)
	{
		auto font = (HFONT)BASE::SendMessage(WM_GETFONT, 0, 0);
		if (font)
		{
			CStringW str;
			if (BASE::GetText(str))
			{
				CClientDC dc(this);
				auto old_font = dc.SelectObject(font);
				bool result = ThemeUtils::GetTextSizeW(dc, str, str.GetLength(), pSize);
				dc.SelectObject(old_font);
				return result;
			}
		}

		return false;
	}

	virtual bool SizeToContent()
	{
		CSize size;
		if (GetTextSize(&size))
		{
			CRect rect;
			BASE::GetWindowRect(&rect);

			// note: FontHeight / 6 is used by .NET controls for overhang padding
			auto overhangPadding = (float)size.cy / 6.0f;
			auto italicPadding =
			    overhangPadding / 2.0f; // italic factor 1/2 for overhang padding is also .NET magic number
			auto padding = (int)ceil(2.0f * overhangPadding + italicPadding);

			// [overhang padding][Text][overhang padding][italic padding]
			BASE::SetWindowPos(NULL, -1, -1, size.cx + padding, rect.Height(),
			                   SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER);
			return true;
		}

		return false;
	}
};

typedef CWndTextAutosizeW<CThemedCtl<CStatic, StaticLinkDraw<true>, thType_Static>> CThemedStaticLink;

class CStaticLink : public CWndTextAutosizeW<CThemedCtl<CStatic, StaticLinkDraw<>, thType_Static, true>>
{
  public:
	CStaticLink()
	{
		m_font_type = VaFontType::None; // case: 142819 - use default font when not themed
	}
};

class CThemedListBox : public CListBox
{
	bool m_ext_theme;

  public:
	CThemedListBox()
	{
		m_ext_theme = false;
	}

  protected:
	COLORREF
	bg, text, sel_bg, sel_text, unf_sel_bg, unf_sel_text, border, unf_border;

	afx_msg HBRUSH CtlColor(CDC* pDC, UINT nCtlColor);

#pragma warning(push)
#pragma warning(disable : 4191)
	BEGIN_MESSAGE_MAP_INLINE(CThemedListBox, CListBox)
	ON_WM_CTLCOLOR_REFLECT()
	END_MESSAGE_MAP_INLINE()
#pragma warning(pop)

	void InitColorSchema();
	virtual void PreSubclassWindow();
	virtual void DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct);
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);

	virtual void MeasureItem(LPMEASUREITEMSTRUCT lpMeasureItemStruct);
};

class CThemedDimEdit : public CMenuXPCtl<CWndTextW<CThemedCtl<CDimEditCtrl, DimEditIDE11Draw, thType_Edit>>>
{
  protected:
	virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam);
	virtual void OnPopulateContextMenu(CMenu& contextMenu);
};

class CThemedEdit : public CMenuXPCtl<CWndTextW<CThemedCtl<CEdit, EditIDE11Draw, thType_Edit>>>
{
  protected:
	bool m_bNCAreaUpdated = false;
	virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam);
	virtual void OnPopulateContextMenu(CMenu& contextMenu);
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);

  public:
	void SetTextOverrideColor(EditIDE11Draw::ColorIndex index, COLORREF color, bool redraw = true)
	{
		auto r = dynamic_cast<EditIDE11Draw*>(Theme.Renderer.get());
		if (r)
			r->SetTextOverrideColor(index, color, redraw);
	}

	void RemoveTextOverrideColor(EditIDE11Draw::ColorIndex index, bool redraw = true)
	{
		auto r = dynamic_cast<EditIDE11Draw*>(Theme.Renderer.get());
		if (r)
			r->RemoveTextOverrideColor(index, redraw);
	}
};

class CThemedEditBrowse : public CMenuXPCtl<CWndTextW<CThemedCtl<CMFCEditBrowseCtrl, EditBrowseDraw, thType_Edit>>>
{
  public:
	struct DrawGlyphContext
	{
		CThemedEditBrowse* wnd;          // control
		CDC* dc;                         // dc to draw to
		ThemeUtils::ColorSet* bg_colors; // button background color(s)
		COLORREF border;                 // button border color
		CRect rect;                      // button rect (all area)
		CImageList* img_list;            // image list
		int img_id;                      // image index to draw
		CSize img_size;                  // image size in pixels
		bool pressed;                    // true if button is pressed
		bool hot;                        // true if mouse is over button
	};

	typedef std::function<void(DrawGlyphContext& ctx)> DrawGlyphFnc;
	typedef std::function<void()> KeyEvent;

  private:
	int m_defaultButtonSize = 0;
	bool m_bDrawButtonBorder = true;
	DrawGlyphFnc m_drawGlyph;
	std::map<UINT, KeyEvent> m_keyEvents;
	ThemeUtils::ColorSet* m_btnBkColorSet = nullptr;
	COLORREF m_btnBorder = 0;

  protected:
	virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam);
	virtual void OnPopulateContextMenu(CMenu& contextMenu);

  public:
	CThemedEditBrowse();

	void SetDrawButtonBorder(bool draw);
	void SetDrawGlyphFnc(DrawGlyphFnc fnc)
	{
		m_drawGlyph = fnc;
	}
	void GetButtonRect(CRect& rect)
	{
		rect = m_rectBtn;
	}

	void SetSearchDrawGlyphFnc();
	void SetClearDrawGlyphFnc();

	void SetKeyEvent(BYTE virtual_key, KeyEvent func);
	KeyEvent GetKeyEvent(BYTE virtual_key);

	virtual void DrawButton(CDC* dc, ThemeUtils::ColorSet* colors);
	virtual void OnDrawBrowseButton(CDC* pDC, CRect rect, BOOL bIsButtonPressed, BOOL bHighlight);
	virtual void OnChangeLayout();
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);

  protected:
	virtual void OnDpiChanged(DpiChange change, bool& handled);
};

// This edit control displays disabled text
// with the same color as if it was active.
template <bool actLikeDisabled = false> class CThemedEditACID : public CThemedEdit
{
  public:
	CThemedEditACID()
	{
		Theme.State.SetOption(TEXT("ActiveColorInDisabled"));
	}

	virtual ~CThemedEditACID()
	{
	}

	void SetActLikeDisabled(bool value)
	{
		actLikeDisabled = value;
	}

	LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam) override
	{
		if (actLikeDisabled)
		{
			if (message == WM_SETCURSOR)
				return TRUE;

			if (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST)
				return 0;
		}

		return __super::WindowProc(message, wParam, lParam);
	}
};

// this edit control highlights syntax
template <int PAINT_TYPE = PaintType::ListBox> class CThemedEditSHL : public CThemedEdit
{
  protected:
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
	{
		if (message == WM_PAINT || message == WM_ERASEBKGND)
		{
			int old_in_WM_PAINT = PaintType::in_WM_PAINT;
			PaintType::in_WM_PAINT = PAINT_TYPE;

			LRESULT ret = __super::WindowProc(message, wParam, lParam);

			PaintType::in_WM_PAINT = old_in_WM_PAINT;
			return ret;
		}

		return __super::WindowProc(message, wParam, lParam);
	}
};

//////////////////////////////////////////////////////////////////////////
// THEMED CTreeCtrl
class CThemedTree : public CWndDpiAware<CTreeCtrl>
{
	std::unique_ptr<CGradientCache> background_gradient_cache;
	DWORD saved_style, saved_ex_style;
	CImageList* saved_img_list;
	bool m_doublebuffer_support;
	bool m_erase_bg;
	bool m_in_paint;
	bool m_is_focus;
	COLORREF m_border;
	COLORREF m_focus;
	COLORREF m_bg;

  public:
	bool ThemeRendering;

	CThemedTree();

	DECLARE_MESSAGE_MAP()
	afx_msg void OnNMCustomdraw(NMHDR* pNMHDR, LRESULT* pResult);

	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);
	afx_msg void OnNcPaint();
	afx_msg void OnPaint();

	afx_msg void OnKillFocus(CWnd* pNewWnd);
	afx_msg void OnSetFocus(CWnd* pOldWnd);
};

class CEditVC : public CEdit
{
  public:
	CEditVC() : m_rectNCBottom(0, 0, 0, 0), m_rectNCTop(0, 0, 0, 0)
	{
	}

  protected:
	CRect m_rectNCBottom;
	CRect m_rectNCTop;

  public:
	virtual ~CEditVC()
	{
	}

  protected:
	afx_msg void OnNcCalcSize(BOOL bCalcValidRects, NCCALCSIZE_PARAMS FAR* lpncsp)
	{
		CRect rectWnd, rectClient;

		// calculate client area height needed for a font
		CFont* pFont = GetFont();
		CRect rectText;
		rectText.SetRectEmpty();

		CDC* pDC = GetDC();

		CFont* pOld = pDC->SelectObject(pFont);
		pDC->DrawText("Ky", rectText, DT_CALCRECT | DT_LEFT);
		int uiVClientHeight = rectText.Height();

		pDC->SelectObject(pOld);
		ReleaseDC(pDC);

		// calculate NC area to center text.

		GetClientRect(rectClient);
		GetWindowRect(rectWnd);

		ClientToScreen(rectClient);

		int uiCenterOffset = (rectClient.Height() - uiVClientHeight) / 2;
		int uiCY = (rectWnd.Height() - rectClient.Height()) / 2;
		int uiCX = (rectWnd.Width() - rectClient.Width()) / 2;

		rectWnd.OffsetRect(-rectWnd.left, -rectWnd.top);
		m_rectNCTop = rectWnd;

		m_rectNCTop.DeflateRect(uiCX, uiCY, uiCX, uiCenterOffset + uiVClientHeight + uiCY);

		m_rectNCBottom = rectWnd;

		m_rectNCBottom.DeflateRect(uiCX, uiCenterOffset + uiVClientHeight + uiCY, uiCX, uiCY);

		lpncsp->rgrc[0].top += uiCenterOffset;
		lpncsp->rgrc[0].bottom -= uiCenterOffset;

		lpncsp->rgrc[0].left += uiCX;
		lpncsp->rgrc[0].right -= uiCY;
	}

	afx_msg void OnNcPaint()
	{
		Default();

		CWindowDC dc(this);
		CBrush Brush(GetSysColor(COLOR_WINDOW));

		dc.FillRect(m_rectNCBottom, &Brush);
		dc.FillRect(m_rectNCTop, &Brush);
	}

	afx_msg UINT OnGetDlgCode()
	{
		if (m_rectNCTop.IsRectEmpty())
		{
			SetWindowPos(NULL, 0, 0, 0, 0, SWP_NOOWNERZORDER | SWP_NOSIZE | SWP_NOMOVE | SWP_FRAMECHANGED);
		}

		return CEdit::OnGetDlgCode();
	}

#pragma warning(push)
#pragma warning(disable : 4191)
	BEGIN_MESSAGE_MAP_INLINE(CEditVC, CEdit)
	ON_WM_NCCALCSIZE()
	ON_WM_NCPAINT()
	ON_WM_GETDLGCODE()
	END_MESSAGE_MAP_INLINE()
#pragma warning(pop)
};

// This combobox is always virtual, however
// it contains default implementation of IItemSource based on std::vector
// If you would want sorted list, implement IItemSource based on sorted
// container such as std::set or just add a sort method
// The state of list is checked only before opening of dropdown.
class CThemedComboBox : public CWndTextW<CGenericComboBox>
{
  public:
	// Defines what should be compared during
	// execution of Compare method.
	// Componentes are compared in following order:

	enum CompareOpts
	{
		cmpText = 0x01,
		cmpTextNoCase = 0x02 | cmpText,
		cmpTip = 0x04,
		cmpTipNoCase = 0x08 | cmpTip,
		cmpImage = 0x10,
		cmpAll = cmpText | cmpTip | cmpImage,
		cmpAllNoCase = cmpTextNoCase | cmpTipNoCase | cmpImage,
	};

	class ItemData
	{
	  private:
		static bool IsBitSet(CompareOpts mask, CompareOpts bit)
		{
			return (mask & bit) == bit;
		}

	  public:
		CStringW Text;
		CStringW Tip;
		int Image;

		ItemData() : Image(-1)
		{
		}
		ItemData(const CStringW& text, const CStringW& tip = L"", int image = -1) : Text(text), Tip(tip), Image(image)
		{
		}
		ItemData(const CStringA& text, const CStringA& tip = "", int image = -1)
		    : Text(::MbcsToWide(text, text.GetLength())), Tip(::MbcsToWide(tip, tip.GetLength())), Image(image)
		{
		}

		void GetText(CStringA& str) const
		{
			str = WideToMbcs(Text, Text.GetLength()).c_str();
		}

		void GetText(CStringW& str) const
		{
			str = Text;
		}

		void SetText(const CStringA& str)
		{
			Text = ::MbcsToWide(str, str.GetLength());
		}

		void SetText(const CStringW& str)
		{
			Text = str;
		}

		int Compare(const ItemData& other, CompareOpts opts = cmpAll) const
		{
			int cmp = 0;

			if (IsBitSet(opts, cmpText))
			{
				cmp = IsBitSet(opts, cmpTextNoCase) ? Text.CompareNoCase(other.Text) : Text.Compare(other.Text);
			}

			if (cmp == 0 && IsBitSet(opts, cmpTip))
			{
				cmp = IsBitSet(opts, cmpTipNoCase) ? Tip.CompareNoCase(other.Tip) : Tip.Compare(other.Tip);
			}

			if (cmp == 0 && IsBitSet(opts, cmpImage))
			{
				cmp = Image == other.Image ? 0 : (Image > other.Image ? 1 : -1);
			}

			return cmp;
		}
	};

	struct IItemSource
	{
		typedef std::function<bool(const ItemData&, const ItemData&, CompareOpts)> CompareFunc;
		CThemedComboBox& ParentCB;

		CompareFunc UserCompareFnc;

		IItemSource(CThemedComboBox& cb) : ParentCB(cb)
		{
		}
		virtual ~IItemSource()
		{
		}

		void OnCountChanged()
		{
			ParentCB.SetItemCount((int)Count());
		}

		virtual void Clear() = 0;
		virtual size_t Count() const = 0;
		virtual bool Insert(size_t index, const ItemData& item) = 0;
		virtual bool Add(const ItemData& item) = 0;
		virtual bool Remove(size_t index) = 0;
		virtual ItemData& At(size_t index) = 0;
		virtual const ItemData& At(size_t index) const = 0;

		// default implementation uses Count() and At() to iterate list
		// then ItemData::Compare is used to compare two items
		virtual int Find(const ItemData& item, CompareOpts opts = cmpText) const;

		ItemData& operator[](size_t index)
		{
			return At(index);
		}
		const ItemData& operator[](size_t index) const
		{
			return At(index);
		}
	};

	std::set<int> SelectedItems;
	std::shared_ptr<IItemSource> Items;

	typedef WndEventArgs<CThemedComboBox> WndEventArgsT;
	typedef EventList<WndEventArgsT> WndEventList;

	WndEventList SelectionChanged;
	WndEventList TextChanged;

	typedef WndProcEventArgs<CThemedComboBox> WndProcEventArgsT;
	typedef Event<WndProcEventArgsT> WndProcEvent;
	typedef EventList<WndProcEventArgsT> WndProcEventList;

	WndProcEventList EditWindowProc;
	WndProcEventList ListWindowProc;
	WndProcEventList ComboWindowProc;

	CStringW Text;
	bool ThemingActive;

	CThemedComboBox(bool has_colorable_content = false);

	void AddCtrlBackspaceEditHandler();
	void SubclassDlgItemAndInit(HWND dlg, INT id);

	void InsertItemW(int index, const CStringW& text, const CStringW& tip = CStringW(), int image = -1);
	bool RemoveItemAt(int index);
	int FindItemW(const CStringW& text, bool case_sensitive = true) const;
	void AddItemW(const CStringW& text, const CStringW& tip = CStringW(), int image = -1);
	void AddItemA(const CStringA& text, const CStringA& tip = CStringA(), int image = -1);
	size_t ItemsCount()
	{
		return Items->Count();
	}

	virtual bool OnEditWindowProc(UINT message, WPARAM wParam, LPARAM lParam, LRESULT& result);
	virtual bool OnListWindowProc(UINT message, WPARAM wParam, LPARAM lParam, LRESULT& result);
	virtual bool OnComboWindowProc(UINT message, WPARAM wParam, LPARAM lParam, LRESULT& result);

	virtual void OnDropdown();
	virtual void OnEditChange();
	virtual void GetDefaultTitleAndIconIdx(CString& title, int& iconIdx) const;
	virtual void OnSelect();
	virtual void SelectItem(int index, bool append = false);

	virtual WTString GetItemTip(int nRow) const;
	virtual CStringW GetItemTipW(int nRow) const;

	virtual void OnCloseup();
	virtual void Init();
	virtual int GetItemsCount() const;

	virtual bool IsVS2010ColouringActive() const;

	virtual bool GetText(CStringW& text) const;

	virtual bool GetText(CStringA& text) const;
	virtual bool SetText(const CStringW& text);

	virtual bool SetText(const CStringA& text);

	virtual bool GetItemInfo(int nRow, CStringW* text, CStringW* tip, int* image, UINT* state);

	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);
};

//////////////////////////////////////////////////////////////////////////
// THEMED CHtmlDialog class
class CThemedHtmlDlg : public CHtmlDialog
{
  protected:
	CThemeHandler<CDialog> Theme;

	LPARAM CallWindowProcInternal(UINT msg, WPARAM wParam, LPARAM lParam)
	{
		return CHtmlDialog::WindowProc(msg, wParam, lParam);
	}

  public:
	CThemedHtmlDlg(UINT idd, CWnd* pParent, UINT nID_HTML, UINT n_ID_static)
	    : CHtmlDialog(idd, pParent, nID_HTML, n_ID_static), Theme(this)
	{
		if (ThemeRendererTraits<VADlgIDE11Draw>::Apply())
		{
			Theme.Renderer.reset(new VADlgIDE11Draw());
			Theme.State.AddHandler(Theme.Renderer.get());
			Theme.BrushMap.reset(new ThemeUtils::CBrushMap());
			Theme.PenMap.reset(new ThemeUtils::CPenMap());
		}
	}

#pragma warning(push)
#pragma warning(disable : 4191)
	BEGIN_MESSAGE_MAP_INLINE(CThemedHtmlDlg, CHtmlDialog)
	ON_WM_CTLCOLOR()
	END_MESSAGE_MAP_INLINE()
#pragma warning(pop)

	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
	{
		if (ThemeRendererTraits<VADlgIDE11Draw>::Apply() && Theme.IsEnabled &&
		    Theme.IsDlgItemForDefaultTheming((UINT)pWnd->GetDlgCtrlID()))
		{
			if (Theme.Renderer.get())
				return Theme.Renderer->HandleDefaultCtlColor(pWnd, pDC, nCtlColor);
		}

		return CHtmlDialog::OnCtlColor(pDC, pWnd, nCtlColor);
	}

	virtual void PreSubclassWindow()
	{
		CHtmlDialog::PreSubclassWindow();
		Theme.OnWindowCreated(this);
	}

	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
	{
		if (message == WM_ENTERSIZEMOVE || message == WM_EXITSIZEMOVE)
		{
			BOOL is_enter = message == WM_ENTERSIZEMOVE ? TRUE : FALSE;

			ThemeUtils::ForEachChild(m_hWnd, [is_enter](HWND wnd) {
				::SendMessage(wnd, WM_VA_DLGENTEREXITSIZEMOVE, (WPARAM)is_enter, 0);
				return true;
			});
		}

		if (!ThemeRendererTraits<VADlgIDE11Draw>::Apply())
			return CHtmlDialog::WindowProc(message, wParam, lParam);

		Theme.Result = 0;
		Theme.Msg = GetCurrentMessage();
		Theme.CallDefault = true;
		if (!Theme.CallWndProc)
		{
			Theme.CallWndProc = [this](UINT msg, WPARAM wParam, LPARAM lParam) {
				return CallWindowProcInternal(msg, wParam, lParam);
			};
		}
		Theme.HandleWindowProcPre(message, wParam, lParam, this);

		if (message == WM_VA_THEMEDTYPE)
			return (LRESULT)thType_Dialog;

		if (message == WM_VA_GETTHEMECONTEXT)
			return (LRESULT)&Theme;

		if (Theme.CallDefault)
			Theme.Result = CHtmlDialog::WindowProc(message, wParam, lParam);

		Theme.HandleWindowProcPost(message, wParam, lParam, this);
		Theme.Msg = nullptr;
		return Theme.Result;
	}
};

//////////////////////////////////////////////////////////////////////////
// THEMED VADialog class
template <typename RENDERER> class CThemedVADlgTmpl : public VADialog
{
	typedef CThemedVADlgTmpl<RENDERER> self_Type;

  protected:
	CThemeHandler<CDialog> Theme;

	LPARAM CallWindowProcInternal(UINT msg, WPARAM wParam, LPARAM lParam)
	{
		return VADialog::WindowProc(msg, wParam, lParam);
	}

  public:
	CThemedVADlgTmpl(UINT idd = 0, CWnd* pParent = NULL, Freedom fd = fdAll, UINT nFlags = flDefault)
	    : VADialog(idd, pParent, fd, nFlags), Theme(this)
	{
		if (ThemeRendererTraits<RENDERER>::Apply())
		{
			Theme.Renderer.reset(new RENDERER());
			Theme.State.AddHandler(Theme.Renderer.get());
			Theme.BrushMap.reset(new ThemeUtils::CBrushMap());
			Theme.PenMap.reset(new ThemeUtils::CPenMap());
		}
	}

	CThemedVADlgTmpl(LPCTSTR lpszTemplateName, CWnd* pParent = NULL, Freedom fd = fdAll, UINT nFlags = flDefault)
	    : VADialog(lpszTemplateName, pParent, fd, nFlags), Theme(this)
	{
		if (ThemeRendererTraits<RENDERER>::Apply())
		{
			Theme.Renderer.reset(new RENDERER());
			Theme.State.AddHandler(Theme.Renderer.get());
			Theme.BrushMap.reset(new ThemeUtils::CBrushMap());
			Theme.PenMap.reset(new ThemeUtils::CPenMap());
		}
	}

	virtual ~CThemedVADlgTmpl() = default;

#pragma warning(push)
#pragma warning(disable : 4191)
	BEGIN_MESSAGE_MAP_INLINE(self_Type, VADialog)
	ON_WM_CTLCOLOR()
	END_MESSAGE_MAP_INLINE()
#pragma warning(pop)

	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
	{
		if (Theme.IsEnabled && Theme.Renderer && Theme.IsDlgItemForDefaultTheming((UINT)pWnd->GetDlgCtrlID()))
		{
			return Theme.Renderer->HandleDefaultCtlColor(pWnd, pDC, nCtlColor);
		}

		return VADialog::OnCtlColor(pDC, pWnd, nCtlColor);
	}

	virtual void PreSubclassWindow()
	{
		VADialog::PreSubclassWindow();
		Theme.OnWindowCreated(this);
	}

	typedef Event<WndProcEventArgs<CThemedVADlgTmpl>> WndProcEvent;
	typedef std::shared_ptr<WndProcEvent> WndProcEventShrP;
	WndProcEventList<CThemedVADlgTmpl> WndProcEvents;

	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
	{
		if (WndProcEvents)
		{
			LRESULT rslt = 0;
			if (WndProcEvents.ProcessWndProc(this, this, rslt, message, wParam, lParam))
				return rslt;
		}

		if (message == WM_ENTERSIZEMOVE || message == WM_EXITSIZEMOVE)
		{
			BOOL is_enter = message == WM_ENTERSIZEMOVE ? TRUE : FALSE;

			ThemeUtils::ForEachChild(m_hWnd, [is_enter](HWND wnd) {
				::SendMessage(wnd, WM_VA_DLGENTEREXITSIZEMOVE, (WPARAM)is_enter, 0);
				return true;
			});
		}

		if (!ThemeRendererTraits<RENDERER>::Apply())
			return VADialog::WindowProc(message, wParam, lParam);

		Theme.Result = 0;
		Theme.CallDefault = true;

		if (!Theme.CallWndProc)
		{
			Theme.CallWndProc = [this](UINT msg, WPARAM wParam, LPARAM lParam) {
				return CallWindowProcInternal(msg, wParam, lParam);
			};
		}

		Theme.Msg = VADialog::GetCurrentMessage();
		Theme.HandleWindowProcPre(message, wParam, lParam, this);

		if (message == WM_VA_THEMEDTYPE)
			return (LRESULT)thType_Dialog;

		if (message == WM_VA_GETTHEMECONTEXT)
			return (LRESULT)&Theme;

		if (Theme.CallDefault)
			Theme.Result = VADialog::WindowProc(message, wParam, lParam);

		Theme.HandleWindowProcPost(message, wParam, lParam, this);
		Theme.Msg = nullptr;
		return Theme.Result;
	}
};

typedef CThemedVADlgTmpl<VADlgIDE11Draw> CThemedVADlg;

NS_THEMEDRAW_END

#ifndef NO_USING_NS_THEMEDRAW
using namespace ThemeDraw;
#endif

#endif // VAThemeDraw_h__
