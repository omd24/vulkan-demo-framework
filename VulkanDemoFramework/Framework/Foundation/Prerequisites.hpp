#pragma once

//---------------------------------------------------------------------------//
// Compiler specific settings
//---------------------------------------------------------------------------//

#if defined(_MSC_VER)
#  if !defined(_CRT_SECURE_NO_WARNINGS)
#    define _CRT_SECURE_NO_WARNINGS
#  endif

//#pragma warning(disable : 4005)
//#pragma warning(disable : 4100)
#endif // _MSC_VER

#if defined(_MSC_VER)
#  if !defined(WIN32_LEAN_AND_MEAN)
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>

// headers needed by bit impl
#  include <immintrin.h>
#  include <intrin0.h>
#endif

//---------------------------------------------------------------------------//
// General Includes
//---------------------------------------------------------------------------//

#include <stdint.h>
#include <stdio.h>

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
