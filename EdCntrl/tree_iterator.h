#pragma once

class tree_iterator : public std::iterator<std::forward_iterator_tag, HTREEITEM, size_t>
{
  public:
	tree_iterator(const tree_iterator& right)
	{
		*this = right;
	}
	tree_iterator& operator=(const tree_iterator& right)
	{
		tree = right.tree;
		item = right.item;
		return *this;
	}

	static tree_iterator begin(CTreeCtrl& tree)
	{
		return tree_iterator(&tree, tree.GetRootItem());
	}
	static tree_iterator end(CTreeCtrl& tree)
	{
		return tree_iterator(&tree);
	}

	tree_iterator& operator++()
	{ // prefix
		assert(item);
		if (!item)
			return *this;

		HTREEITEM nextitem = tree->GetChildItem(item);
		if (!nextitem)
			nextitem = tree->GetNextSiblingItem(item);
		while (!nextitem)
		{
			item = tree->GetParentItem(item);
			if (!item)
				break;
			nextitem = tree->GetNextSiblingItem(item);
		}
		item = nextitem;
		return *this;
	}
	tree_iterator operator++(int)
	{ // postfix
		tree_iterator ret = *this;
		++*this;
		return ret;
	}

	bool operator==(const tree_iterator& right) const
	{
		assert(tree == right.tree);
		return item == right.item;
	}
	bool operator!=(const tree_iterator& right) const
	{
		return !(*this == right);
	}

	HTREEITEM operator*() const
	{
		return item;
	}

  protected:
	tree_iterator(CTreeCtrl* tree, HTREEITEM item = NULL) : tree(tree), item(item)
	{
		assert(tree && tree->m_hWnd);
	}

	CTreeCtrl* tree;
	HTREEITEM item;
};
