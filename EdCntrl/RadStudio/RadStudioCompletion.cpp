#include "stdafxed.h"
#if defined(RAD_STUDIO) || defined(VA_CPPUNIT)

#include "RadStudioCompletion.h"
#include "SymbolTypes.h"
#include "RadStudioPlugin.h"
#include "Expansion.h"

VaRSCompletionData sRSCompletionData;

// #RAD_ParamCompletion
VaRSParamCompletionData sRSParamCompletionData;
VaRSParamCompetionOverloads sRSParamComplOverloads;

CompletionKind GetCompletionKind(symbolInfo* symInfo)
{
	if (!symInfo)
		return CompletionKind::Undefined;

	CompletionKind mask = CompletionKind::Undefined;
	CompletionKind kind = CompletionKind::Undefined;

	if (symInfo->mAttrs & V_PRIVATE || symInfo->mAttrs & V_PROTECTED)
		mask = CompletionKind::PrivateOrProtectedFlag;

	if (symInfo->mAttrs & V_CONSTRUCTOR)
		kind = CompletionKind::Constructor;
	else
	{
		switch (symInfo->m_type)
		{
		case CLASS:
			kind = CompletionKind::Class;
			break;
		case STRUCT:
			kind = CompletionKind::Struct;
			break;
		case FUNC:
			kind = CompletionKind::Function;
			break;
		case VAR:
			kind = CompletionKind::Variable;
			break;
		case DEFINE:
			kind = CompletionKind::Method;
			break;
		case TYPE:
			kind = CompletionKind::TypeParameter;
			break;
		case OPERATOR:
			kind = CompletionKind::Operator;
			break;
		case C_ENUM:
			kind = CompletionKind::Enum;
			break;
		case C_ENUMITEM:
			kind = CompletionKind::EnumMember;
			break;
		case C_INTERFACE:
			kind = CompletionKind::Interface;
			break;
		case STRING:
		case CONSTANT:
		case NUMBER:
			kind = CompletionKind::Constant;
			break;
		case EVENT:
		case DELEGATE:
			kind = CompletionKind::Event;
			break;
		case SYM:
			kind = CompletionKind::Field;
			break;
		case RESWORD:
			kind = CompletionKind::Keyword;
			break;
		case PROPERTY:
			kind = CompletionKind::Property;
			break;
		}
	}

	return (CompletionKind)((DWORD)kind | (DWORD)mask);
}

VaRSCompletionData* VaRSCompletionData::Get()
{
	return &sRSCompletionData;
}

void VaRSCompletionData::Cleanup()
{
	sRSCompletionData.Clear(true);
}

bool VaRSParamsHelper::ParamsEqual(std::vector<WTString>& params1, std::vector<WTString>& params2)
{
	if (params1.size() != params2.size())
		return false;

	for (size_t i = 0; i < params2.size(); i++)
		if (params1[i] != params2[i])
			return false;

	return true;
}

void VaRSParamsHelper::ParamsFromDef(WTString def, std::vector<WTString>& params)
{
	if (def.IsEmpty())
		return;

	extern WTString ReadToNextParam(const WTString& input, WTString& appendTo, bool leftParams);

	auto openIndex = def.find('(');

	if (openIndex >= 0)
		def = def.substr(openIndex + 1);

	int oldLength = def.length();
	while (def[0] != ')')
	{
		WTString param;

		def = ReadToNextParam(def, param, true);
		param.Trim();
		param.TrimRightChar(',');
		param.Trim();

		if (param.IsEmpty())
			break;

		params.emplace_back(param);

		if (oldLength == def.length())
			break;

		oldLength = def.length();
	}
}

void VaRSParamCompletionData::Populate()
{
	// if this asserts make sure to call ed->Scope(TRUE);
	// see: EdCnt::RsParamCompletion where this method is used
	_ASSERTE(!sRSParamComplOverloads.defs.empty());

	sRSParamComplOverloads.params.clear();
	sRSParamComplOverloads.params.resize(sRSParamComplOverloads.defs.size());
	for (size_t i = 0; i < sRSParamComplOverloads.defs.size(); i++)
	{
		// split definition into params
		sRSParamComplOverloads.params[i].clear();
		VaRSParamsHelper::ParamsFromDef(sRSParamComplOverloads.defs[i], sRSParamComplOverloads.params[i]);

		// fill RAD structure
		VaRSSignatureInfo sig;
		for (size_t j = 0; j < sRSParamComplOverloads.params[i].size(); j++)
			sig.params.emplace_back(VaRSParamInfo(sRSParamComplOverloads.params[i][j], ""));

		sig.label = sRSParamComplOverloads.defs[i];
		sRSParamCompletionData.signatures.emplace_back(sig);
	}
}

bool VaRSParamCompletionData::FindBestIndex(std::vector<WTString> bestParams, int bestArg)
{
	activeSig = -1;
	activeParam = -1;

	// select active signature and parameter list
	for (size_t i = 0; i < sRSParamComplOverloads.params.size(); i++)
	{
		if (sRSParamComplOverloads.params[i].size() <= (size_t)bestArg)
			continue;

		// identify the active signature
		if (activeSig == -1 &&
		    VaRSParamsHelper::ParamsEqual(sRSParamComplOverloads.params[i], bestParams))
		{
			activeSig = (int)i;
			activeParam = bestArg;
			return true;
		}
	}

	return false;
}
#endif
