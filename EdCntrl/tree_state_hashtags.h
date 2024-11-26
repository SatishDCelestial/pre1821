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
#include <limits>
#undef min
#undef max

namespace tree_state_hashtags
{
const std::string path_separator = "$!$";

inline static std::string get_item_text(const CTreeCtrl& tree, HTREEITEM item,
                                        std::unordered_map<std::string, int>* current_level_names = nullptr);
inline static bool is_sibling(const std::string& name1, const std::string& name2);
inline static std::string get_parent_name(const std::string& full_name);
inline static bool is_space_or_cntrl(char ch);
inline static bool get_hash_tag(std::string& name);

inline static bool is_substr(const std::string& str, const std::string& prefix, size_t offset = 0)
{
	return str.size() >= prefix.size() + offset && 0 == str.compare(offset, prefix.size(), prefix);
}

inline static bool local_name_equals_full_name(const std::string& path, const std::string& local_name,
                                               const std::string& full_name)
{
	if (path.empty())
		return local_name == full_name;
	else
	{
		size_t offset = 0;

		if (is_substr(full_name, path))
		{
			offset += path.length();
			if (is_substr(full_name, path_separator, offset))
			{
				offset += path_separator.length();

				return full_name.length() == offset + local_name.length() && is_substr(full_name, local_name, offset);
			}
		}
	}

	return false;
}

inline HTREEITEM get_item_by_full_name(CTreeCtrl& tree, const std::string& fullname);

struct storage
{
  public:
	void clear()
	{
		mExpansionState.clear();
		mSelectedNode.clear();
		mSelectedNodePath.clear();
	}

	std::unordered_map<std::string, bool> mExpansionState;
	std::vector<std::string> mSelectedNode;
	std::string mSelectedNodePath;
	std::string mFirstVisible;

	bool is_selected_item(const std::string& item) const
	{
		return !mSelectedNode.empty() && local_name_equals_full_name(mSelectedNodePath, mSelectedNode.back(), item);
	}

	bool is_sibling_of_selected_item(const std::string& item) const
	{
		if (!mSelectedNode.empty() && !mSelectedNodePath.empty())
		{
			size_t offset = 0;

			if (is_substr(item, mSelectedNodePath))
			{
				offset += mSelectedNodePath.length();
				if (is_substr(item, path_separator, offset))
				{
					offset += path_separator.length();
					return item.find(path_separator, offset) == std::string::npos;
				}
			}
		}

		return false;
	}
};

class save
{
  public:
	save(storage& state) : state(state)
	{
		state.clear();
	}

	uint do_before_children(const CTreeCtrl& tree, HTREEITEM item, int child_index, const std::string& name)
	{
		return 0;
	}

	uint do_after_children(const CTreeCtrl& tree, HTREEITEM item, int child_index, const std::string& name)
	{
		do_after_children__store_expand(tree, item, child_index, name);
		do_after_children__store_selected(tree, item, child_index, name);
		do_after_children__store_first_visible(tree, item, child_index, name);
		return 0;
	}
	void do_after_children__store_expand(const CTreeCtrl& tree, HTREEITEM item, int child_index,
	                                     const std::string& name)
	{
		const auto itemState = tree.GetItemState(item, TVIS_EXPANDED | TVIS_SELECTED);
		if ((itemState & TVIS_EXPANDED) != 0)
			state.mExpansionState[name] = true;
		else if (tree.ItemHasChildren(item))
			state.mExpansionState[name] = false;
	}
	void do_after_children__store_selected(const CTreeCtrl& tree, HTREEITEM item, int child_index,
	                                       const std::string& name)
	{
		const auto itemState = tree.GetItemState(item, TVIS_EXPANDED | TVIS_SELECTED);
		if ((itemState & TVIS_SELECTED) != 0)
		{
			auto path_end = name.rfind(path_separator);
			if (path_end != std::string::npos)
				state.mSelectedNodePath = name.substr(0, path_end);

			auto itm = tree.GetParentItem(item);

			if (itm != nullptr)
				itm = tree.GetChildItem(itm);
			else
				itm = tree.GetRootItem();

			while (itm != nullptr)
			{
				state.mSelectedNode.push_back(get_item_text(tree, itm));
				if (itm == item)
					break;

				itm = tree.GetNextSiblingItem(itm);
			}
		}
	}
	void do_after_children__store_first_visible(const CTreeCtrl& tree, HTREEITEM item, int child_index,
	                                            const std::string& name)
	{
		if (item == tree.GetFirstVisibleItem())
			state.mFirstVisible = name;
	}

	void do_finally(const CTreeCtrl& tree, uint flags)
	{
	}

	storage& state;
};

class restore
{
	HTREEITEM parent_node;

  public:
	restore(const storage& state) : state(state)
	{
		parent_node = nullptr;
	}

	uint do_before_children(CTreeCtrl& tree, HTREEITEM item, int child_index, const std::string& name)
	{
		uint flags = 0;
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
		{
			tree.SelectSetFirstVisible(item);
			flags |= 1;
		}

		if (!isSelected && state.is_selected_item(name))
		{
			select_item(tree, item, false);
			flags |= 2;
		}
		else if (parent_node == nullptr && state.is_sibling_of_selected_item(name))
			parent_node = tree.GetParentItem(item);

		return flags;
	}

	uint do_after_children(CTreeCtrl& tree, HTREEITEM item, int child_index, const std::string& name)
	{
		return 0;
	}

	void select_item(CTreeCtrl& tree, HTREEITEM item, bool previous)
	{
		if (previous)
		{
			auto parent = tree.GetParentItem(item);
			if (parent != nullptr)
			{
				item = tree.GetPrevSiblingItem(item);
				if (item == nullptr)
					item = parent;
			}
			else
			{
				auto prev = tree.GetPrevSiblingItem(item);
				if (prev != nullptr)
					item = prev;
			}
		}

		tree.Select(item, TVGN_CARET);
		tree.SetItemState(item, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	}

	void do_finally(CTreeCtrl& tree, uint flags)
	{
		// gmit: flag is set if visible or selected nodes are previously found. if they are not, we'll try to find them
		// now
		if (!(flags & 2) && (state.mSelectedNode.size() > 0))
		{
			std::string fullpath = state.mSelectedNode.back();
			if (state.mSelectedNodePath.size() > 0)
				fullpath = state.mSelectedNodePath + path_separator + fullpath; // no idea why it's done that way
			auto item = get_item_by_full_name(tree, fullpath);
			if (item)
			{
				tree.SelectItem(item);
				flags &= ~1;
			}
		}
		if (!(flags & 1) && state.mFirstVisible.length())
		{
			auto item = get_item_by_full_name(tree, state.mFirstVisible);
			if (item)
				tree.SelectSetFirstVisible(item);
		}

		if (tree.GetSelectedItem() == nullptr && !state.mSelectedNode.empty())
		{
			// current items (after re-population)
			// map allows quick searching
			std::map<std::string, HTREEITEM> items;

			auto fill_items = [&]() {
				// fill the map
				auto current = tree.GetChildItem(parent_node);
				while (current)
				{
					auto name = get_item_text(tree, current);

					if (!name.empty())
						items[name] = current;

					current = tree.GetNextSiblingItem(current);
				}
			};

			if (parent_node)
			{
				// try to find any persisted item from previously selected item to top
				// if found, select directly as it is preceding already,
				// F8 navigates to next item (not yet navigated)
				fill_items();
				for (int i = (int)state.mSelectedNode.size() - 1; i >= 0; --i)
				{
					auto it = items.find(state.mSelectedNode[(uint)i]);
					if (it != items.end())
					{
						select_item(tree, it->second, false);
						return;
					}
				}

				// none of previous items persisted?
				// select the parent node, so F8 navigates to first child
				select_item(tree, parent_node, false);
			}
			else
			{
				// alphabetically preceding the previously selected item
				HTREEITEM preceding_item = nullptr; // used when nothing found

				// try to find by hash tag

				auto sel_hash = state.mSelectedNodePath.empty() ? state.mSelectedNode.back() : state.mSelectedNodePath;

				if (get_hash_tag(sel_hash))
				{
					auto current = tree.GetRootItem();
					while (current)
					{
						auto curr_hash = get_item_text(tree, current);
						if (get_hash_tag(curr_hash))
						{
							int cmp = curr_hash.compare(sel_hash);

							if (cmp == 0)
							{
								// exact match, select now
								select_item(
								    tree, current,
								    !state.mSelectedNodePath
								            .empty() && // only in case of sub-items select preceding item
								        !tree.ItemHasChildren(
								            current) // if item is top-level, we don't need to select preceding one
								);

								return;
							}
							else if (cmp < 0)
							{
								// alphabetically preceding
								preceding_item = current;
							}
						}

						current = tree.GetNextSiblingItem(current);
					}
				}

				// try to find any persisted item from previously selected item to top
				// if found, select directly as it is preceding already,
				// F8 navigates to next item (not yet navigated)
				fill_items();
				for (int i = (int)state.mSelectedNode.size() - 1; i >= 0; --i)
				{
					auto it = items.find(state.mSelectedNode[(uint)i]);
					if (it != items.end())
					{
						select_item(tree, it->second, false);
						return;
					}
				}

				// select preceding item or first one
				// applies when the item was completely removed from the list
				// preceding_item is alphabetically preceding the previously selected hash tag
				preceding_item = preceding_item ? preceding_item : tree.GetRootItem();
				if (preceding_item)
					select_item(tree, preceding_item, false);
			}
		}
	}

	const storage& state;
};

inline static bool is_space_or_cntrl(char ch)
{
	return wt_iscntrl(ch) || wt_isspace(ch);
}

inline static bool get_hash_tag(std::string& name)
{
	auto hash_idx = name.find('#');
	if (hash_idx != std::string::npos)
	{
		auto separ = name.find(path_separator, hash_idx);
		auto length = std::string::npos == separ ? name.length() : separ;
		auto x = hash_idx + 1;
		for (; x < length; x++)
			if (is_space_or_cntrl(name[x]))
				break;

		name = name.substr(hash_idx, x - hash_idx);
		return true;
	}
	return false;
}

inline static bool trim_children_count(std::string& name)
{
	if (name.length() > 2 && name.at(name.length() - 1) == ')')
	{
		int x = (int)name.length() - 2;

		while (x >= 0 && wt_isdigit(name.at((uint)x)))
			x--;

		if (name.at((uint)x) == '(')
		{
			x--;

			while (x >= 0 && is_space_or_cntrl(name.at((uint)x)))
				x--;

			if (x + 1 >= 0)
			{
				name.resize(uint(x + 1));
				return true;
			}
		}
	}

	return false;
}

inline static std::string get_item_text(const CTreeCtrl& tree, HTREEITEM item,
                                        std::unordered_map<std::string, int>* current_level_names /* = nullptr*/)
{
	CString nameStr = tree.GetItemText(item);
	nameStr.Remove('\5'); // MARKER_RECT
	nameStr.Remove('\a'); // hidden

	std::string name(nameStr);

	// remove children count from the name
	// "Root Node (2)" becomes "Root Node"
	// that allows finding of the item when child is removed
	if (tree.ItemHasChildren(item))
		trim_children_count(name);

	if (current_level_names && 1 < ++(*current_level_names)[name]) // handle items with same name (at some level)
		name += std::string(uint((*current_level_names)[name]), '#');

	return name;
}

inline static bool is_sibling(const std::string& name1, const std::string& name2)
{
	auto separ1 = name1.rfind(path_separator);
	auto separ2 = name2.rfind(path_separator);

	return separ1 == separ2 && name1.compare(0, separ1, name2, 0, separ2) == 0;
}

inline static bool is_parent(const std::string& parent, const std::string& child)
{
	auto separ = child.rfind(path_separator);

	return std::string::npos != separ && parent.compare(0, std::string::npos, child, 0, separ) == 0;
}

inline static std::string get_parent_name(const std::string& full_name)
{
	auto last_separ = full_name.rfind(path_separator);
	if (std::string::npos != last_separ)
		return full_name.substr(0, last_separ);
	return std::string();
}

// gmit: reusable, but have in mind that it will process only nodes that have at least one child
template <typename PROC>
inline uint traverse_items(CTreeCtrl& tree, HTREEITEM item, PROC& proc, int max_level = std::numeric_limits<int>::max(),
                           const std::string& parent_name = "")
{
	if (item == nullptr)
		return 0;

	uint flags = 0;
	std::unordered_map<std::string, int> current_level_names;
	int child_index = 0;

	for (; item; item = tree.GetNextSiblingItem(item))
	{
		std::string name = get_item_text(tree, item, &current_level_names);
		std::string full_name = parent_name.empty() ? name : (parent_name + path_separator + name);

		flags |= proc.do_before_children(tree, item, child_index, full_name);

		if (max_level > 0)
			flags |= traverse_items(tree, tree.GetChildItem(item), proc, max_level - 1, full_name);

		flags |= proc.do_after_children(tree, item, child_index, full_name);

		child_index++;
	}
	return flags;
}

template <typename PROC>
inline void traverse(CTreeCtrl& tree, PROC& proc, int max_level = std::numeric_limits<int>::max())
{
	auto root_item = tree.GetRootItem();
	if (root_item != nullptr)
	{
		uint flags = traverse_items(tree, root_item, proc, max_level);
		proc.do_finally(tree, flags);
	}
}

inline std::string get_full_name(CTreeCtrl& tree, HTREEITEM item)
{
	auto first = item;
	for (auto item2 = item; item2; item2 = tree.GetPrevSiblingItem(item2))
		first = item2;

	std::unordered_map<std::string, int> current_level_names;
	for (auto item2 = first; item2 != item; item2 = tree.GetNextSiblingItem(item2))
		get_item_text(tree, item2, &current_level_names);
	auto text = get_item_text(tree, item, &current_level_names);

	auto parent = tree.GetParentItem(item);
	if (parent)
		return get_full_name(tree, parent) + path_separator + text;
	else
		return text;
}

inline HTREEITEM get_item_by_full_name(CTreeCtrl& tree, const std::string& fullname)
{
	HTREEITEM item = tree.GetRootItem();
	auto start_it = fullname.cbegin();
	while (item)
	{
		auto end_it = std::search(start_it, fullname.cend(), path_separator.cbegin(), path_separator.cend());
		std::string name(start_it, end_it);

		std::unordered_map<std::string, int> current_level_names;
		for (; item; item = tree.GetNextSiblingItem(item))
		{
			if (get_item_text(tree, item, &current_level_names) == name)
				break;
		}

		if (!item || (end_it == fullname.cend()))
			return item; // either not found on current level or complete path is found
		start_it = end_it + (int)path_separator.size();
		item = tree.GetChildItem(item);
	}
	return nullptr; // no children
}
} // namespace tree_state_hashtags
