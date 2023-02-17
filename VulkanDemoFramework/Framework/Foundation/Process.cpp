#include "Process.hpp"

namespace Framework
{
//---------------------------------------------------------------------------//
// Static buffer to log the error coming from windows.
static const uint32_t kProcessLogBuffer = 256;
char g_ProcessLogBuffer[kProcessLogBuffer];
static char g_ProcessOutputBuffer[1025];
//---------------------------------------------------------------------------//
void win32GetError(char* p_Buffer, uint32_t p_Size)
{
  DWORD errorCode = GetLastError();

  char* errorString;
  if (!FormatMessageA(
          FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER,
          NULL,
          errorCode,
          MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
          (LPSTR)&errorString,
          0,
          NULL))
    return;

  sprintf_s(p_Buffer, p_Size, "%s", errorString);

  LocalFree(errorString);
}

//---------------------------------------------------------------------------//
bool processExecute(
    const char* p_WorkingDirectory,
    const char* p_ProcessFullpath,
    const char* p_Arguments,
    const char* p_SearchErrorString)
{
  // From the post in
  // https://stackoverflow.com/questions/35969730/how-to-read-output-from-cmd-exe-using-createprocess-and-createpipe/55718264#55718264
  // Create pipes for redirecting output
  HANDLE handleStdinPipeRead = NULL;
  HANDLE handleStdinPipeWrite = NULL;
  HANDLE handleStdoutPipeRead = NULL;
  HANDLE handleStdPipeWrite = NULL;

  SECURITY_ATTRIBUTES securityAttributes = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};

  BOOL ok = CreatePipe(&handleStdinPipeRead, &handleStdinPipeWrite, &securityAttributes, 0);
  if (ok == FALSE)
    return false;
  ok = CreatePipe(&handleStdoutPipeRead, &handleStdPipeWrite, &securityAttributes, 0);
  if (ok == FALSE)
    return false;

  // Create startup informations with std redirection
  STARTUPINFOA startupInfo = {};
  startupInfo.cb = sizeof(startupInfo);
  startupInfo.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
  startupInfo.hStdInput = handleStdinPipeRead;
  startupInfo.hStdError = handleStdPipeWrite;
  startupInfo.hStdOutput = handleStdPipeWrite;
  startupInfo.wShowWindow = SW_SHOW;

  bool executionSuccess = false;
  // Execute the process
  PROCESS_INFORMATION processInfo = {};
  BOOL inheritHandles = TRUE;
  if (CreateProcessA(
          p_ProcessFullpath,
          (char*)p_Arguments,
          0,
          0,
          inheritHandles,
          0,
          0,
          p_WorkingDirectory,
          &startupInfo,
          &processInfo))
  {

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);

    executionSuccess = true;
  }
  else
  {
    win32GetError(&g_ProcessLogBuffer[0], kProcessLogBuffer);

    char msg1[512]{};
    sprintf(
        msg1,
        "Execute process error.\n Exe: \"%s\" - Args: \"%s\" - Work_dir: \"%s\"\n",
        p_ProcessFullpath,
        p_Arguments,
        p_WorkingDirectory);
    OutputDebugStringA(msg1);

    char msg2[256]{};
    sprintf(msg2, "Message: %s\n", g_ProcessLogBuffer);
    OutputDebugStringA(msg2);
  }
  CloseHandle(handleStdinPipeRead);
  CloseHandle(handleStdPipeWrite);

  // Output
  DWORD bytesRead;
  ok = ReadFile(handleStdoutPipeRead, g_ProcessOutputBuffer, 1024, &bytesRead, nullptr);

  // Consume all outputs.
  // Terminate current read and initialize the next.
  while (ok == TRUE)
  {
    g_ProcessOutputBuffer[bytesRead] = 0;
    char msg[256]{};
    sprintf(msg, "Message: %s\n", g_ProcessLogBuffer);
    OutputDebugStringA(msg);

    ok = ReadFile(handleStdoutPipeRead, g_ProcessOutputBuffer, 1024, &bytesRead, nullptr);
  }

  if (strlen(p_SearchErrorString) > 0 && strstr(g_ProcessOutputBuffer, p_SearchErrorString))
  {
    executionSuccess = false;
  }

  OutputDebugStringA("\n");

  // Close handles.
  CloseHandle(handleStdoutPipeRead);
  CloseHandle(handleStdinPipeWrite);

  DWORD process_exit_code = 0;
  GetExitCodeProcess(processInfo.hProcess, &process_exit_code);

  return executionSuccess;
}
//---------------------------------------------------------------------------//
const char* processGetOutput() { return g_ProcessOutputBuffer; }
//---------------------------------------------------------------------------//
} // namespace Framework
