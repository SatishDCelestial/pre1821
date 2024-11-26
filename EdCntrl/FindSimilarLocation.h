#pragma once
#include "WTString.h"
#include "FileLineMarker.h"
#include "UnrealPostfixType.h"

#define FSL_SEPARATOR_CHAR '@'
#define FSL_SEPARATOR_STR "@"

class FindSimilarLocation
{
  public:
	enum eLabel
	{
		iNone,
		iPublic,
		iPrivate,
		iProtected,
		iPublished,
		iFreeFunction
	};
	struct sResult
	{
		sResult(const WTString& qualification_w_ns, const WTString& qualification_wo_ns, const WTString& name,
		        int mainBlockFrom, int mainBlockTo)
		    : Qualification_w_ns(qualification_w_ns), Qualification_wo_ns(qualification_wo_ns), Name(name),
		      MainBlockFrom(mainBlockFrom), MainBlockTo(mainBlockTo)
		{
		}

		WTString Qualification_w_ns;
		WTString Qualification_wo_ns;
		WTString Name;
		int MainBlockFrom;
		int MainBlockTo;
	};

	FindSimilarLocation()
	{
	}
	~FindSimilarLocation()
	{
	}

	int WhereToPutImplementation(const WTString& targetBuf, const CStringW& targetFileName,
	                             UnrealPostfixType unrealPostfixType = UnrealPostfixType::None);
	int WhereToPutDeclaration(const WTString& sourceBuf, uint curPos, const WTString& targetBuf);

	const WTString& GetImplNamespace()
	{
		return ImplNamespace;
	}

  private:
	struct FlatItem
	{
		FlatItem(const WTString& str, const WTString& qualification_wo_ns, const WTString& qualification_w_ns,
		         int startLine, int endLine, eLabel label)
		{
			FullName = str;
			Qualification_wo_ns = qualification_wo_ns;
			Qualification_impl_ns = qualification_w_ns;
			StartLine = (unsigned long)startLine;
			EndLine = (unsigned long)endLine;
			Label = label;
		}

		WTString FullName;              // with qualification
		WTString Qualification_wo_ns;   // without name
		WTString Qualification_impl_ns; // without name
		unsigned long StartLine;
		unsigned long EndLine;
		eLabel Label;
	};

	sResult GetQualificationViaOutline(ULONG ln, LineMarkers::Node& node, WTString actQualification_w_ns = WTString(),
	                                   WTString actQualification_wo_ns = WTString());
	WTString GetExtendedQualification(WTString& actQualification, FileLineMarker& marker,
	                                  FileOutlineFlags::DisplayFlag& displayFlag, WTString& currentName,
	                                  bool includeNamespaces);
	void BuildFlatImplementationList(sResult qualification, std::vector<FlatItem>& flatItems, LineMarkers::Node& node,
	                                 int& index, int& prevCommentStartLine, WTString actQualification_ns = WTString());
	bool BuildFlatDeclarationList(sResult qualification, std::vector<FlatItem>& flatItems, LineMarkers::Node& node,
	                              int& index, int& prevCommentStartLine, eLabel label = iNone,
	                              WTString actQualification_w_ns = WTString(),
	                              WTString actQualification_wo_ns = WTString());

	bool FindSimilarInBlock(int& index, const std::vector<FlatItem>& source, const std::vector<FlatItem>& target,
	                        int& res, int from, int to, bool freeFunc);
	int FindSimilarLocationInTargetOutline(int index, const std::vector<FlatItem>& source,
	                                       const std::vector<FlatItem>& target, bool sameFile,
	                                       bool insertAfterOverloads = true);
	void AmendLocationIfNeed(const WTString& fileBuf, int& line);
	WTString ImplNamespace;
};
