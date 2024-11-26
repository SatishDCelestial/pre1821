#pragma once

#if defined(RAD_STUDIO) || defined(VA_CPPUNIT)

#include "WTString.h"
#include "CppBuilder/IRadStudioHost.h"

class symbolInfo;

struct TStringRef
{
	const char* data; // pointer to text data
	int len;          // Length in bytes/chars of the text data. The IDE does not require a 0x0 terminator

	TStringRef()
	    : data(nullptr), len(0)
	{
	}

	void init(const char* strData = nullptr, int strLen = 0)
	{
		data = strData;
		len = strLen;
	}

	void init(const WTString& str)
	{
		init(str.c_str(), str.length());
	}
};

enum class CompletionKind : unsigned int
{
	Undefined = 0,
	Text = 1,
	Method = 2,
	Function = 3,
	Constructor = 4,
	Field = 5,
	Variable = 6,
	Class = 7,
	Interface = 8,
	Module = 9,
	Property = 10,
	Unit = 11,
	Value = 12,
	Enum = 13,
	Keyword = 14,
	Snippet = 15,
	Color = 16,
	File = 17,
	Reference = 18,
	Folder = 19,
	EnumMember = 20,
	Constant = 21,
	Struct = 22,
	Event = 23,
	Operator = 24,
	TypeParameter = 25,

	// flags
	PrivateOrProtectedFlag = 0x80000000
};

CompletionKind GetCompletionKind(symbolInfo* symInfo);


struct TCompletionItem
{
	TStringRef label;         // The name of the identifier examples are "index" or "addToList"
	CompletionKind kind;      // the identifier kind this uses the same constants as before
	TStringRef detail;        // This is the declaration of he identifier examples are "int index" or "void addToList(char *a)"
	TStringRef documentation; //Currently not used

	TCompletionItem()
	    : kind(CompletionKind::Undefined)
	{
	}

	void Clear()
	{
		kind = CompletionKind::Undefined;
		label.init();
		detail.init();
		documentation.init();
	}
};

class VaRSCompletionItem
{
	CompletionKind kind;
	WTString label;
	WTString detail;
	WTString documentation;

  public:
	// [case: 164045] incorrect symbols in tooltips
	int item_index;		// index within completion set (unsorted)

	VaRSCompletionItem()
	    : kind(CompletionKind::Undefined), item_index(-1)
	{
	}

	// [case: 164045] incorrect symbols in tooltips
	int GetItemIndex()
	{
		return item_index;
	}

	void Clear()
	{
		kind = CompletionKind::Undefined;
		label.Empty();
		detail.Empty();
		documentation.Empty();
	}

	void Set(int itemIndex, CompletionKind itemKind, const WTString& strLabel, const WTString& strDetail)
	{
		this->item_index = itemIndex;

		SetKind(itemKind);
		SetLabel(strLabel);
		SetDetail(strDetail);
	}

	void Set(int itemIndex, CompletionKind itemKind, const WTString& strLabel, const WTString& strDetail, const WTString& strDoc)
	{
		this->item_index = itemIndex;

		SetKind(itemKind);
		SetLabel(strLabel);
		SetDetail(strDetail);
		SetDoc(strDoc);
	}

	void SetItem(TCompletionItem& item) const
	{
		item.kind = kind;
		item.label.init(label);
		item.detail.init(detail);
		item.documentation.init(documentation);
	}

	void SetKind(CompletionKind itemKind)
	{
		kind = itemKind;
	}

	void SetLabel(const WTString& text)
	{
		label = text;
	}

	void SetDetail(const WTString& text)
	{
		detail = text;
	}

	void SetDoc(const WTString& text)
	{
		documentation = text;
	}
};


class VaRSCompletionData
{
	std::vector<VaRSCompletionItem> data;
	std::vector<TCompletionItem> pass_data;
	bool dirty = false;

  public:
	static VaRSCompletionData* Get();
	static void Cleanup();

	const TCompletionItem* GetArray()
	{
		if (dirty)
		{
			auto size = data.size();
			pass_data.resize(size);
			for (size_t i = 0; i < size; i++)
			{
				data[i].SetItem(pass_data[i]);
			}
			dirty = false;
		}

		if (pass_data.empty())
			return nullptr;

		return &pass_data.front();
	}

	int GetLength() const
	{
		return (int)data.size();
	}

	void Init(size_t count)
	{
		data.resize(count);
	}

	void Set(size_t index, CompletionKind kind, const WTString& strLabel, const WTString& strDetail, const WTString& strDoc)
	{
		data[index].Set((int)index, kind, strLabel, strDetail, strDoc);
		dirty = true;
	}

	void Set(size_t index, CompletionKind kind, const WTString& strLabel, const WTString& strDetail)
	{
		data[index].Set((int)index, kind, strLabel, strDetail);
		dirty = true;
	}

	int GetItemIndex(size_t index)
	{
		if (index < data.size())
		{
			// item index also must be within same range,
			int item_index = data[index].GetItemIndex();
			if (item_index >= 0 && item_index < (int)data.size())
				return item_index;
		}

		return -1;
	}


	void Clear(bool clearItems = false)
	{
		if (clearItems)
		{
			for (auto& d : data)
			{
				d.Clear();
			}
		}

		data.clear();
		dirty = true;
	}
};

struct VaRSParamInfo
{
	WTString label;
	WTString doc;

	VaRSParamInfo()
	{
	}

	VaRSParamInfo(WTString _label, WTString _doc)
	    : label(_label), doc(_doc)
	{
	}
};

class VaRSSignatureInfo
{
	std::vector<TParameterInformation> pass_params;
	TSignatureInfo pass_info = {0};

  public:
	WTString label;
	WTString doc;
	std::vector<VaRSParamInfo> params;

	TSignatureInfo & Get()
	{
		pass_info = {0};
		pass_info.FLabel = label.c_str();
		pass_info.FDocumentation = doc.c_str();

		pass_params.resize(params.size());
		for (size_t i = 0; i < params.size(); i++)
		{
			pass_params[i].Flabel = params[i].label.c_str();
			pass_params[i].FDocumentation = params[i].doc.c_str();
		}

		pass_info.FParameters = &pass_params.front();
		pass_info.FLength = (int)pass_params.size();

		return pass_info;
	}
};

struct VaRSParamCompetionOverloads
{
	std::vector<WTString> defs;
	std::vector<std::vector<WTString>> params;

	void Clear()
	{
		defs.clear();
		params.clear();
	}
};

struct VaRSParamsHelper
{
	static bool ParamsEqual(std::vector<WTString>& params1, std::vector<WTString>& params2);
	static void ParamsFromDef(WTString def, std::vector<WTString>& params);
};

class VaRSParamCompletionData 
{
	std::vector<TSignatureInfo> pass_signatures;

  public:
	VaRSParamCompletionData()
	{
	}

	std::vector<VaRSSignatureInfo> signatures;

	int activeSig = -1;
	int activeParam = -1;
	WTString errorMessage;
	std::vector<WTString> overloads;
	std::vector<std::vector<WTString>> overloads_params;

	void Populate();
	bool FindBestIndex(std::vector<WTString> bestParams, int bestArg);

	void Clear()
	{
		pass_signatures.clear();
		signatures.clear();
		errorMessage.Empty();
	}

	TSignatureInfo* Get()
	{
		pass_signatures.resize(signatures.size());
		for (size_t i = 0; i < signatures.size(); i++)
		{
			pass_signatures[i] = signatures[i].Get();
		}
		return &pass_signatures.front();
	}

	int Size()
	{
		return (int)signatures.size();
	}
};

#endif