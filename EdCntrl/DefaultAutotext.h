#pragma once

struct TemplateItem;

bool IsDefaultAutotextItemTitle(int langType, const WTString& title);
const TemplateItem* GetDefaultAutotextItem(int langType, LPCTSTR title);
const TemplateItem* GetDefaultAutotextItem(int langType, int idx);
