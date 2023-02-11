#pragma once

#include "Foundation/Prerequisites.hpp"
#include <assert.h>

namespace Framework
{
//---------------------------------------------------------------------------//
// Math utils
//---------------------------------------------------------------------------//

// Undefine the macro versions of this.
#undef max
#undef min

template <typename T> T max(const T& a, const T& b) { return a > b ? a : b; }

template <typename T> T min(const T& a, const T& b) { return a < b ? a : b; }

template <typename T> T clamp(const T& v, const T& a, const T& b)
{
  return v < a ? a : (v > b ? b : v);
}

template <typename To, typename From> To safeCast(From p_Input)
{
  To result = (To)p_Input;

  From check = (From)result;
  assert(check == result);

  return result;
}

FRAMEWORK_INLINE uint32_t roundU32(float p_Value)
{
  uint32_t ret = static_cast<uint32_t>(p_Value);
  return ret;
}
FRAMEWORK_INLINE uint32_t roundU32(double p_Value)
{
  uint32_t ret = static_cast<uint32_t>(p_Value);
  return ret;
}

const float PI = 3.1415926538f;
const float PI2 = 1.57079632679f;
} // namespace Framework
