#pragma once

enum
{
	Plain = 1,
	Java,
	Tmp,
	RC,
	Other,
	Src,
	Header,
	Binary,
	Idl,
	HTML,
	VB,
	PERL,
	JS,
	CS,
	SQL,
	Image,
	UC,
	PHP,
	ASP,
	XAML,
	XML,
	VBS,
	kLanguageFiletypeCount
};

#define IsCFile(ftype) ((ftype) == Header || (ftype) == Src || (ftype) == UC)
#define Is_C_CS_File(ftype) ((ftype) == Header || (ftype) == Src || (ftype) == UC || (ftype) == CS)
#define Is_C_CS_VB_File(ftype)                                                                                         \
	((ftype) == Tmp || (ftype) == Header || (ftype) == Src || (ftype) == Idl || (ftype) == VB || (ftype) == VBS ||     \
	 (ftype) == CS || (ftype) == UC)
#define Is_HTML_JS_VBS_File(ftype)                                                                                     \
	((ftype) == HTML || (ftype) == JS || (ftype) == VBS || (ftype) == ASP || (ftype) == XAML || (ftype) == XML)
#define Is_VB_VBS_File(ftype) ((ftype) == VB || (ftype) == VBS)
#define Is_Tag_Based(ftype) ((ftype) == HTML || (ftype) == ASP || (ftype) == XAML || (ftype) == XML)
#define Is_Some_Other_File(ftype)                                                                                      \
	((ftype) == Plain || (ftype) == RC || (ftype) == Other || (ftype) == Binary || (ftype) == Image || (ftype) == SQL)
#define Defaults_to_Net_Symbols(ftype)                                                                                 \
	((ftype) == CS || (ftype) == ASP || (ftype) == XAML || (ftype) == VB || (ftype) == JS || (ftype) == VBS ||         \
	 (ftype) == PHP)

// If file has no extension, don't check to see if is extension-less
// header; just return Other in order to avoid hitting disk
// (or when passing random string literals)
int GetFileTypeByExtension(LPCTSTR s);
int GetFileTypeByExtension(const CStringW& s);

// When requireExtension is false, don't pass in random strings
// because support for extension-less headers requires checking disk
// if the string has no extension
int GetFileType(const CStringW& s, bool requireExtension = false, bool deepInspect = false);
int GetFileType(LPCTSTR s);

LPCTSTR GetFileType(int ftype);
bool IsLikelyXmlFile(const CStringW& filename);

inline BOOL IsCaseSensitiveType(int ftype)
{
	if (UC == ftype || VB == ftype)
		return FALSE;
	return TRUE;
}

inline BOOL IsCaseSensitiveFiletype(const CStringW& file)
{
	const int ftype = GetFileType(file);
	return IsCaseSensitiveType(ftype);
}
