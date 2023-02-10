#pragma once

#include <stdint.h>

//---------------------------------------------------------------------------//
// Compiler specific settings
//---------------------------------------------------------------------------//

#if defined(_MSC_VER)
//#pragma warning(disable : 4005)
//#pragma warning(disable : 4100)
#endif // _MSC_VER

//---------------------------------------------------------------------------//
// Various macros
//---------------------------------------------------------------------------//

#define FRAMEWORK_INLINE inline
#define FRAMEWORK_FINLINE __forceinline
#define FRAMEWORK_DEBUG_BREAK __debugbreak();
#define FRAMEWORK_DISABLE_WARNING(warning_number) __pragma(warning(disable : warning_number))
#define FRAMEWORK_CONCAT_OPERATOR(x, y) x##y

#define FRAMEWORK_STRINGIZE(L) #L
#define FRAMEWORK_MAKESTRING(L) FRAMEWORK_STRINGIZE(L)
#define FRAMEWORK_CONCAT(x, y) FRAMEWORK_CONCAT_OPERATOR(x, y)
#define FRAMEWORK_LINE_STRING FRAMEWORK_MAKESTRING(__LINE__)
#define FRAMEWORK_FILELINE(MESSAGE) __FILE__ "(" FRAMEWORK_LINE_STRING ") : " MESSAGE

// Unique names
#define FRAMEWORK_UNIQUE_SUFFIX(PARAM) RAPTOR_CONCAT(PARAM, __LINE__)
