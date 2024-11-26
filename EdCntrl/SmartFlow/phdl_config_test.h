#pragma once

// #phdlConfig debug/test mode

// Use EncryptString.exe to encrypt and encode: name, password, urls
// Syntax: EncryptString.exe string_to_encrypt seed
// 	Make sure to use same seed as PHDL_KEY_SEED

#define PHDL_KEY_SEED 9669

#define PHDL_ACCOUNT_NAME "DsrY2oPoRnc1e9oUkwGsuDipz34zRaxc2tbSKh4w0ks="

// NOTE: we suspect a bad password -- connection fails in test mode
#define PHDL_PASSWORD "hWEB90euo7lGJh7aal/FMMolTMSaT4UAYBtBVaf2KWk="

// http://test-sdk.smartflow.online/PhdlTestSec
#define PHDL_SERVER_URL1 "5t1zrb1FtMcZb7hvQNoXssHs6v9ShC9iGFdwEI7RXBJtv/LUXz/gu/PzSXZSVzMzKwCw5kDDak561V1crIRczw=="
#define PHDL_SERVER_URL2 "5t1zrb1FtMcZb7hvQNoXssHs6v9ShC9iGFdwEI7RXBJtv/LUXz/gu/PzSXZSVzMzKwCw5kDDak561V1crIRczw=="
#define PHDL_SERVER_URL3 "5t1zrb1FtMcZb7hvQNoXssHs6v9ShC9iGFdwEI7RXBJtv/LUXz/gu/PzSXZSVzMzKwCw5kDDak561V1crIRczw=="

// hex enc key per server settings page in online console:
// 61709027155D290DE601A905BAF3ACD59457B5D606812648170E4F7385E86F8B use https://holtstrom.com/michael/tools/hextopem.php
// to base64 encode encryption hex key
#define PHDL_CLIENT_ENCRYPTION_KEY "YXCQJxVdKQ3mAakFuvOs1ZRXtdYGgSZIFw5Pc4Xob4s="
