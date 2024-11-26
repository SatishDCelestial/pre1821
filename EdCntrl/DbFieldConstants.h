#pragma once

// field separator used in .va db files (single byte that is compatible with utf8)
#define DB_FIELD_DELIMITER '\2'
#define DB_FIELD_DELIMITER_W L'\2'
#define DB_FIELD_DELIMITER_STR "\2"
#define DB_FIELD_DELIMITER_STRW L"\2"

// OLD: encode non ascii db_sep 'section' squiggle in octal for non-default
// ansi code page compiles (kept for compatibility with old data)
#define OLD_DB_FIELD_DELIMITER '\247'
#define OLD_DB_FIELD_DELIMITER_STR "\247"
