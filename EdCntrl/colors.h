#pragma once

// color struct		foregrnd	sep		backgrnd	sep	auto	??	auto	sep
//					rr gg bb	00		rr gg bb	00	10		00	11		00

#define SIZEOF_COLOR_STRUCT 12
typedef struct tagDevColorStr
{
	TCHAR keyName[25];
	byte devValue[SIZEOF_COLOR_STRUCT];
} DevColorStruct;

typedef struct tagColorStr
{
	enum
	{
		sentinelEntry = 0,
		useCurrentUserColor = 1,
		useVAColorIfIsMSDevDefault,
		useVAColor
	} Override;
	DevColorStruct m_devColors;
	VAColors m_vaColorEnum;
	EditColorStr m_vaColors;
} COLORSTRUCT;

#define FG_AUTOBYTE 8
#define BG_AUTOBYTE 10

extern COLORSTRUCT g_colors[];
