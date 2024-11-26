#pragma once

#include "WTString.h"
#include "VAParse.h"
#include "LocalRefactoring.h"

class IntroduceVariable : public LocalRefactoring
{
  public:
	enum eCreateBraces
	{
		NOCREATE,
		FOR_WITHOUT_BRACES,
		IF_WITHOUT_BRACES,
		WHILE_WITHOUT_BRACES,
		DO_WITHOUT_BRACES,
		ELSE_WITHOUT_BRACES,
		FOREACH_WITHOUT_BRACES,
		// CASE_WITHOUT_BRACES,
		CASE_BEFORE_COLON,
	};

	IntroduceVariable(int fType)
	{
		InsideStatementParens = false;
	}
	~IntroduceVariable()
	{
	}

	BOOL Introduce();
	BOOL CanIntroduce(bool select);

	BOOL AddBraces(bool canAdd);
	BOOL RemoveBraces(bool canRemove, int methodLine = -1);

	static int FindCorrespondingParen(const WTString& buf, int pos, char opParen = '(', char clParen = ')');
	static int FindCorrespondingParen2(const WTString& buf, int pos, char opParen /*='('*/, char clParen /*=')'*/);
	static int FindOpeningBraceAfterWhiteSpace(long curPos, const WTString& fileBuf);

  private:
	bool AreParensRemovable(WTString trimmedSelection, int trimmedLen);
	BOOL IsMultipleStatementBetween(int paren1, int paren2, const WTString& fileBuf);
	BOOL IsOnlyCharacterOnLine(int pos, const WTString& fileBuf);
	void DeleteParens(int paren1, int paren2, const WTString& fileBuf, MultiParse* mp);
	int FindPreviousBracket(int openingBrace, int currentPos, const WTString& fileBuf);
	void MoveInsideControlStatementParensIfNeeded(int& curPos, const WTString& fileBuf);
	void MoveBeforeSemicolonIfNeeded(int& curPos, const WTString& fileBuf);
	void MoveAfterOpeningBraceIfNeeded(int& curPos, const WTString& fileBuf);
	bool ShouldIntroduceInsideSelection(EdCntPtr ed, int insertPos, const WTString& fileBuf);
	BOOL ModifyInsertPosAndBracesIfNeeded(eCreateBraces& braces, int& insertPos, int statementPos, int closingParenPos,
	                                      long curPos, int prevStatementPos, int caseBraces, const WTString& fileBuf,
	                                      int& insertPos_brace);
	BOOL InsertClosingBracketSimple(EdCntPtr ed, bool introduceInsideSelection, int& closeBracePos);
	BOOL CorrectCurPosToSemicolon(WTString& fileBuf, int& curPos);
	BOOL InsertClosingBracketComplex(EdCntPtr ed, eCreateBraces braces, long selBegin, int& closeBracePos,
	                                 bool doFormat);
	int FindEndOfStructure(int pos, WTString& fileBuf, eCreateBraces braces);
	int FindPreviousStatement(const WTString& buf, int from, int to, eCreateBraces& braces, int& statementPos,
	                          int& prevStatementPos, int& closingParenPos, int& caseBraces, int& openBracePos);
	WTString GetSingleOrChainSymbolName(WTString selection);
	WTString GetDefaultName(WTString selection);
	int SkipStructureRecursive(const WTString& fileBuf, eCreateBraces braces, int closingParenPos);
	WTString ReadWordAhead(const WTString& fileBuf, int pos);
	bool MovePosToEndOfStatement(WTString& fileBuf, int& pos);
	int GuessEOS(const WTString& filebuf, int curPos);
	BOOL IsBracketMismatch(const WTString& sel, bool showError);
	BOOL IsSelectionValid(const WTString& sel, bool showError, int fileType);
	BOOL IsOnlyWhitespace(const WTString& sel, bool showError, int fileType);
	WTString RemoveWhiteSpaces(WTString selection);
	void ExtendSelectionToIncludeParensWhenAppropriate(int searchFrom, const WTString& fileBuf);
	bool IsPrecedingCharacterAlphaNumerical(const WTString& fileBuf);
	bool IsEmptyBetween(int delete1, int delete2, const WTString& fileBuf);
	int CountWhiteSpacesBeforeFirstChar(long curPos, const WTString& fileBuf);
	int CountCharsToEOL(long curPos, WTString fileBuf);
	void MoveCaretUntilNewLine(long curPos, int shift, WTString fileBuf);
	bool IsInsideCLambdaFunction(int mainFuncOpenBrace, int delete1, int delete2, const WTString& fileBuf,
	                             MultiParse* mp);
	long FindPosOutsideUniformInitializer(const WTString& fileBuf, int bracePos, long curPos);

	bool InsideStatementParens;
};

extern std::pair<WTString, int> GetNextWord(const WTString& fileBuf, int pos);
