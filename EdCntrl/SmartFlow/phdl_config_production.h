#pragma once

// #phdlConfig normal production mode

// Use EncryptString.exe to encrypt and encode: name, password, urls
// Syntax: EncryptString.exe string_to_encrypt seed
// 	Make sure to use same seed as PHDL_KEY_SEED

#define PHDL_KEY_SEED 9669

#define PHDL_ACCOUNT_NAME "DsrY2oPoRnc1e9oUkwGsuDipz34zRaxc2tbSKh4w0ks="
#define PHDL_PASSWORD "daYoDwdJb+mpCnc123tVQxLMNkQo8hy46wXCVdfm+6E="

// http://updates.wholetomato.com/SWUpdate
#define PHDL_SERVER_URL1 "OrjcDFTjMcJO3cmrxiBEs+P7TpszCJdJzuf7NV31sAhIxjp/CyhGNXA9kYgZxUf//J/sx03FJiKeuiUtLNv/HQ=="
// http://vaupdate.wholetomato.com/SWUpdate
#define PHDL_SERVER_URL2 "f2xH/nIBg6LFiKdI2nG39xXHoRG/t1t2G9XJx6Lhu/n9141GeP+ayLBwKtpSRDevzqLcvE4p1a49Ot5bXbR9cQ=="
// http://visualassistupdate.wholetomato.com/SWUpdate
#define PHDL_SERVER_URL3                                                                                               \
	"vSgj8ftwg18+O6yN+X3KxtRdaMFYkifi9u2T3IebayF1n+ZuBpDp1ruvKK77rBB84EsfcuC9dsr14clLK+bJYLnt5tC6tALIFAW5WzHQVXE="

// hex enc key per server settings page in online console:
// 59AC7DF71A8B93F7ABC0D8CDE26FEED419CD5BB05665F67AE0A73115523F212C use https://holtstrom.com/michael/tools/hextopem.php
// to base64 encode encryption hex key
#define PHDL_CLIENT_ENCRYPTION_KEY "Wax99xqLk/erwNjN4m/u1BnNW7BWZfZ64KcxFVI/ISw="
