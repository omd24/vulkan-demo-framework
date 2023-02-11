#include "Time.hpp"

namespace Internal
{
//---------------------------------------------------------------------------//
// Cached frequency.
// From Microsoft Docs:
// (https://docs.microsoft.com/en-us/windows/win32/api/profileapi/nf-profileapi-queryperformancefrequency)
// "The frequency of the performance counter is fixed at system boot and is consistent across all
// processors. Therefore, the frequency need only be queried upon application initialization, and
// the result can be cached."
static LARGE_INTEGER g_Frequency;
//---------------------------------------------------------------------------//
// Taken from the Rust code base:
// https://github.com/rust-lang/rust/blob/3809bbf47c8557bd149b3e52ceb47434ca8378d5/src/libstd/sys_common/mod.rs#L124
// Computes (value*numer)/p_Denom without overflow, as long as both
// (numer*p_Denom) and the overall result fit into int64_t (which is the case
// for our time conversions).
static int64_t int64_mul_div(int64_t p_Value, int64_t p_Numer, int64_t p_Denom)
{
  const int64_t q = p_Value / p_Denom;
  const int64_t r = p_Value % p_Denom;
  // Decompose value as (value/p_Denom*p_Denom + value%p_Denom),
  // substitute into (value*p_Numer)/p_Denom and simplify.
  // r < p_Denom, so (p_Denom*p_Numer) is the upper bound of (r*p_Numer)
  return q * p_Numer + r * p_Numer / p_Denom;
}
//---------------------------------------------------------------------------//
} // namespace Internal

namespace Framework
{
namespace Time
{
void serviceInit()
{
#if defined(_MSC_VER)
  // Cache this value - by Microsoft Docs it will not change during process lifetime.
  QueryPerformanceFrequency(&Internal::g_Frequency);
#endif
}

//
//
void serviceShutdown()
{
  // Nothing to do.
}

int64_t getCurrentTime()
{
  // Get current time
  LARGE_INTEGER time;
  QueryPerformanceCounter(&time);

  // Convert to microseconds
  const int64_t microseconds =
      Internal::int64_mul_div(time.QuadPart, 1000000LL, Internal::g_Frequency.QuadPart);

  return microseconds;
}

int64_t deltaFromStart(int64_t p_StartingTime) { return getCurrentTime() - p_StartingTime; }

double deltaFromStartMicroseconds(int64_t p_StartingTime)
{
  return getMicroseconds(deltaFromStart(p_StartingTime));
}

double deltaFromStartMilliseconds(int64_t p_StartingTime)
{
  return getMilliseconds(deltaFromStart(p_StartingTime));
}

double deltaFromStartSeconds(int64_t p_StartingTime)
{
  return getSeconds(deltaFromStart(p_StartingTime));
}

double deltaSeconds(int64_t p_StartingTime, int64_t p_EndingTime)
{
  return getSeconds(p_EndingTime - p_StartingTime);
}

double deltaMilliseconds(int64_t p_StartingTime, int64_t p_EndingTime)
{
  return getMilliseconds(p_EndingTime - p_StartingTime);
}

double getMicroseconds(int64_t time) { return (double)time; }

double getMilliseconds(int64_t time) { return (double)time / 1000.0; }

double getSeconds(int64_t time) { return (double)time / 1000000.0; }
} // namespace Time
} // namespace Framework
