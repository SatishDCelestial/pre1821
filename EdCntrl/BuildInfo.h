#define VA_VER_MAJOR 10
#define VA_VER_MINOR 9
// #UpdateForReleaseBuilds VA build number
#define VA_VER_BUILD_NUMBER 2536
#define VA_VER_BUILD_SPECIAL 0
// #UpdateForReleaseBuilds A release number that is easier for customers. Display only.
#define VA_VER_SIMPLIFIED_YEAR 2024
#define VA_VER_SIMPLIFIED_RELEASE 8

#define tokenizeSTR(x) #x
#define tokenizeMacro(x) tokenizeSTR(x)

#define FILE_VERSION VA_VER_MAJOR, VA_VER_MINOR, VA_VER_BUILD_NUMBER, VA_VER_BUILD_SPECIAL
#define PRODUCT_VERSION FILE_VERSION
#define FILE_VERSION_STRING                                                                                            \
	tokenizeMacro(VA_VER_MAJOR) "." tokenizeMacro(VA_VER_MINOR) "." tokenizeMacro(                                     \
	    VA_VER_BUILD_NUMBER) "." tokenizeMacro(VA_VER_BUILD_SPECIAL)
#define PRODUCT_VERSION_STRING FILE_VERSION_STRING
