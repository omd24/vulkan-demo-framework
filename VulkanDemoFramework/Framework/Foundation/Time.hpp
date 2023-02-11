#pragma once

#include "Foundation/Prerequisites.hpp"

namespace Framework
{
namespace Time
{
void serviceInit();     // Needs to be called once at startup.
void serviceShutdown(); // Needs to be called at shutdown.

int64_t getCurrentTime(); // Get current time ticks.

double getMicroseconds(int64_t p_Time); // Get microseconds from time ticks
double getMilliseconds(int64_t p_Time); // Get milliseconds from time ticks
double getSeconds(int64_t p_Time);      // Get seconds from time ticks

int64_t deltaFromStart(int64_t p_StartingTime); // Get time difference from start to current time.
double deltaFromStartMicroseconds(int64_t p_StartingTime); // Convenience method.
double deltaFromStartMilliseconds(int64_t p_StartingTime); // Convenience method.
double deltaFromStartSeconds(int64_t p_StartingTime);      // Convenience method.

double deltaSeconds(int64_t p_StartingTime, int64_t p_EndingTime);
double deltaMilliseconds(int64_t p_StartingTime, int64_t p_EndingTime);

} // namespace Time
} // namespace Framework
