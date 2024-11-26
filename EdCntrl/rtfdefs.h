#ifndef RTFDEFS_H
#define RTFDEFS_H

// \cpg argument values
#define cpgIBM 437 // United States IBM
#define cpgGreece 737
#define cpgANSI 819     // Windows 3.1
#define cpgIBMMulti 850 // IBM multilingual
#define cpgEasternEurope 852
#define cpgPortugal 860
#define cpgFrenchCanada 863
#define cpgNorway 865
#define cpgRussia 866
#define cpgEasternEuropeWin 1250
#define cpgCyrillicWin 1251
#define cpgGreeceWin 1253
#define cpgTurkey 1254

// \ansicpg argument values
// see codepage.h

char* RTFFamilyFromPitchAndFamily(BYTE pitchAndFamily)
{
	int family = pitchAndFamily & 0xfc;
	switch (family)
	{
	case FF_MODERN:
		return "modern";
	case FF_DECORATIVE:
		return "decor";
	case FF_ROMAN:
		return "roman";
	case FF_SCRIPT:
		return "script";
	case FF_SWISS:
		return "swiss";
	case FF_DONTCARE:
	default:
		return "modern";
	}
}

#endif // RTFDEFS_H
