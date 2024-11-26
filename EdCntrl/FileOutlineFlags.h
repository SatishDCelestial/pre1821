#pragma once

class FileOutlineFlags
{
  public:
	FileOutlineFlags(DWORD flagBits = 0xffffffff) : mFlagBits(flagBits)
	{
	}

	enum DisplayFlag
	{
		ff_None = 0x0000,
		ff_Includes = 0x0001,
		ff_Preprocessor = 0x0002,
		ff_Comments = 0x0004,
		ff_Globals = 0x0008,
		ff_TagsAndLabels = 0x0010,
		ff_MethodsAndFunctions = 0x0020,
		ff_MembersAndVariables = 0x0040,
		ff_Macros = 0x0080,
		ff_Regions = 0x0100,
		ff_Enums = 0x0200,
		ff_TypesAndClasses = 0x0400,
		ff_Namespaces = 0x0800,
		ff_MessageMaps = 0x1000,
		ff_IncludePseudoGroup = 0x2000,
		ff_MethodsPseudoGroup = 0x4000,
		ff_MacrosPseudoGroup = 0x8000,
		ff_FwdDeclPseudoGroup = 0x00010000,
		ff_FwdDecl = 0x00020000,
		ff_GlobalsPseudoGroup = 0x00040000,
		ff_Hidden = 0x00080000,
		ff_Expanded = 0x00100000, // ugly
		ff_PseudoGroups = ff_IncludePseudoGroup | ff_MethodsPseudoGroup | ff_MacrosPseudoGroup | ff_FwdDeclPseudoGroup |
		                  ff_GlobalsPseudoGroup
	};

	// 	DWORD Flags() const { return mFlagBits; }
	// 	void Flags(DWORD val) { mFlagBits = val; }

	const FileOutlineFlags& operator=(DWORD val)
	{
		mFlagBits = val;
		return *this;
	}
	operator DWORD() const
	{
		return mFlagBits;
	}

	void SetAll()
	{
		mFlagBits = 0xffffffff;
	}
	void ClearAll()
	{
		mFlagBits = 0;
	};

	void Flip(DisplayFlag f)
	{
		if (mFlagBits & f)
			mFlagBits &= ~f;
		else
			mFlagBits |= f;
	}

	bool IsEnabled(DisplayFlag f) const
	{
		return (mFlagBits & f) != 0;
	}

  private:
	DWORD mFlagBits;
};
