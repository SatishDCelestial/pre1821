#pragma once

// template<>
// struct std::less<std::string>
// 	: public std::binary_function<std::string, std::string, bool>
// {	// functor for operator<
// 	bool operator()(const std::string& _Left, const std::string& _Right) const
// 	{
// 		return (strcmp(_Left.c_str(), _Right.c_str()) < 0);
// 	}
// };
#include <unordered_map>

namespace tree_state
{
struct storage
{
  public:
	void clear()
	{
		mExpansionState.clear();
		mSelectedItem.clear();
	}

	std::unordered_map<std::string, bool> mExpansionState;
	std::string mSelectedItem;
	std::string mFirstVisible;
};

class save
{
  public:
	save(storage& state) : state(state)
	{
		state.clear();
	}

	void operator()(const CTreeCtrl& tree, HTREEITEM item, const std::string& name)
	{
		const auto itemState = tree.GetItemState(item, TVIS_EXPANDED | TVIS_SELECTED);
		if ((itemState & TVIS_EXPANDED) != 0)
			state.mExpansionState[name] = true;
		else if (tree.ItemHasChildren(item))
			state.mExpansionState[name] = false;

		if ((itemState & TVIS_SELECTED) != 0)
			state.mSelectedItem = name;
		if (item == tree.GetFirstVisibleItem())
			state.mFirstVisible = name;
	}

	storage& state;

	static const bool do_before_children = false;
};

class restore
{
  public:
	restore(const storage& state) : state(state)
	{
	}

	void operator()(CTreeCtrl& tree, HTREEITEM item, const std::string& name) const
	{
		const auto itemState = tree.GetItemState(item, TVIS_EXPANDED | TVIS_SELECTED);
		const bool isExpanded = (itemState & TVIS_EXPANDED) != 0;
		const bool isSelected = (itemState & TVIS_SELECTED) != 0;

		if (tree.ItemHasChildren(item))
		{
			auto it = state.mExpansionState.find(name);
			if (it != state.mExpansionState.end())
			{
				bool wasExpanded = it->second;
				if (wasExpanded != isExpanded)
					tree.Expand(item, UINT(wasExpanded ? TVE_EXPAND : TVE_COLLAPSE));
			}
		}

		if (state.mFirstVisible == name)
			tree.SelectSetFirstVisible(item);

		bool wasSelected = (name == state.mSelectedItem);
		if (wasSelected != isSelected)
			tree.SelectItem(item);
	}

	const storage& state;

	static const bool do_before_children = true;
};

// gmit: reusable, but have in mind that it will process only nodes that have at least one child
template <typename PROC>
inline void traverse(CTreeCtrl& tree, HTREEITEM item, PROC&& proc, const char* const separator = "$!$",
                     const std::string& path = "")
{
	stdext::hash_map<std::string, int> current_level_names;

	for (; item; item = tree.GetNextSiblingItem(item))
	{
		CString nameStr = tree.GetItemText(item);
		nameStr.Remove('\5'); // MARKER_RECT
		std::string name(nameStr);

		if (1 < ++current_level_names[name]) // handle items with same name (at some level)
			name += std::string((uint)current_level_names[name], '#');

		std::string fullName = path + separator + name;

		if (proc.do_before_children)
			proc(tree, item, fullName);

		traverse(tree, tree.GetChildItem(item), proc, separator, fullName);

		if (!proc.do_before_children)
			proc(tree, item, fullName);
	}
}

template <typename PROC> inline void traverse(CTreeCtrl& tree, PROC&& proc)
{
	return traverse(tree, tree.GetRootItem(), std::move(proc));
}
} // namespace tree_state
