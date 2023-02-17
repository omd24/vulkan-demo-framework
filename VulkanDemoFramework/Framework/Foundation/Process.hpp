#pragma once

#include "Foundation/Prerequisites.hpp"

namespace Framework
{
//---------------------------------------------------------------------------//
bool processExecute(
    const char* p_WorkingDirectory,
    const char* p_ProcessFullpath,
    const char* p_Arguments,
    const char* p_SearchErrorString = "");
//---------------------------------------------------------------------------//
const char* processGetOutput();
//---------------------------------------------------------------------------//
} // namespace Framework
