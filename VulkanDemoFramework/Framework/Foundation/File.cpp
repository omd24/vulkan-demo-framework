#include "File.hpp"

#include "Foundation/Memory.hpp"
#include "Foundation/String.hpp"

#include <string.h>
#include <assert.h>

namespace Framework
{

void fileOpen(const char* p_Filename, const char* p_Mode, FileHandle* p_File)
{
  fopen_s(p_File, p_Filename, p_Mode);
}

void fileClose(FileHandle p_File)
{
  if (p_File)
    fclose(p_File);
}

size_t fileWrite(uint8_t* p_Memory, uint32_t p_ElementSize, uint32_t p_Count, FileHandle p_File)
{
  return fwrite(p_Memory, p_ElementSize, p_Count, p_File);
}

static long fileGetSize(FileHandle p_FileHandle)
{
  long fileSizeSigned;

  fseek(p_FileHandle, 0, SEEK_END);
  fileSizeSigned = ftell(p_FileHandle);
  fseek(p_FileHandle, 0, SEEK_SET);

  return fileSizeSigned;
}

#if defined(_WIN64)
FileTime fileLastWriteTime(const char* p_Filename)
{
  FILETIME lastWriteTime = {};

  WIN32_FILE_ATTRIBUTE_DATA data;
  if (GetFileAttributesExA(p_Filename, GetFileExInfoStandard, &data))
  {
    lastWriteTime.dwHighDateTime = data.ftLastWriteTime.dwHighDateTime;
    lastWriteTime.dwLowDateTime = data.ftLastWriteTime.dwLowDateTime;
  }

  return lastWriteTime;
}
#endif // _WIN64

uint32_t fileResolveToFullPath(const char* p_Path, char* p_OutFullPath, uint32_t p_MaxSize)
{
  return GetFullPathNameA(p_Path, p_MaxSize, p_OutFullPath, nullptr);
}

void fileDirectoryFromPath(char* p_Path)
{
  char* lastPoint = strrchr(p_Path, '.');
  char* lastSeparator = strrchr(p_Path, '/');
  if (lastSeparator != nullptr && lastPoint > lastSeparator)
  {
    *(lastSeparator + 1) = 0;
  }
  else
  {
    // Try searching backslash
    lastSeparator = strrchr(p_Path, '\\');
    if (lastSeparator != nullptr && lastPoint > lastSeparator)
    {
      *(lastSeparator + 1) = 0;
    }
    else
    {
      // Wrong input!
      assert(false && "Malformed path");
    }
  }
}

void filenameFromPath(char* p_Path)
{
  char* lastSeparator = strrchr(p_Path, '/');
  if (lastSeparator == nullptr)
  {
    lastSeparator = strrchr(p_Path, '\\');
  }

  if (lastSeparator != nullptr)
  {
    size_t nameLength = strlen(lastSeparator + 1);

    memcpy(p_Path, lastSeparator + 1, nameLength);
    p_Path[nameLength] = 0;
  }
}

char* fileExtensionFromPath(char* p_Path)
{
  char* lastSeparator = strrchr(p_Path, '.');

  return lastSeparator + 1;
}

bool fileExists(const char* p_Path)
{
  // path = "C:\\gltf-models\\FlightHelmet\\FlightHelmet.gltf";
  WIN32_FILE_ATTRIBUTE_DATA unused;
  return GetFileAttributesExA(p_Path, GetFileExInfoStandard, &unused);
}

bool fileDelete(const char* p_Path)
{
  int result = remove(p_Path);
  return result != 0;
}

bool directoryExists(const char* p_Path)
{
  WIN32_FILE_ATTRIBUTE_DATA unused;
  return GetFileAttributesExA(p_Path, GetFileExInfoStandard, &unused);
}

bool directoryCreate(const char* p_Path)
{
  int result = CreateDirectoryA(p_Path, NULL);
  return result != 0;
}

bool directoryDelete(const char* p_Path)
{
  int result = RemoveDirectoryA(p_Path);
  return result != 0;
}

void directoryCurrent(Directory* p_Directory)
{
  DWORD writtenChars = GetCurrentDirectoryA(kMaxPath, p_Directory->path);
  p_Directory->path[writtenChars] = 0;
}

void directoryChange(const char* p_Path)
{
  if (!SetCurrentDirectoryA(p_Path))
  {
    char msg[256];
    sprintf(msg, "Cannot change current directory to %s\n", p_Path);
    OutputDebugStringA(msg);
  }
}

//
static bool stringEndsWithChar(const char* p_Str, char p_Char)
{
  const char* lastEntry = strrchr(p_Str, p_Char);
  const size_t index = lastEntry - p_Str;
  return index == (strlen(p_Str) - 1);
}

void fileOpenDirectory(const char* p_Path, Directory* p_OutDirectory)
{

  // Open file trying to conver to full path instead of relative.
  // If an error occurs, just copy the name.
  if (fileResolveToFullPath(p_Path, p_OutDirectory->path, MAX_PATH) == 0)
  {
    strcpy(p_OutDirectory->path, p_Path);
  }

  // Add '\\' if missing
  if (!stringEndsWithChar(p_Path, '\\'))
  {
    strcat(p_OutDirectory->path, "\\");
  }

  if (!stringEndsWithChar(p_OutDirectory->path, '*'))
  {
    strcat(p_OutDirectory->path, "*");
  }

  p_OutDirectory->handle = nullptr;

  WIN32_FIND_DATAA findData;
  HANDLE foundHandle;
  if ((foundHandle = FindFirstFileA(p_OutDirectory->path, &findData)) != INVALID_HANDLE_VALUE)
  {
    p_OutDirectory->handle = foundHandle;
  }
  else
  {
    char msg[256];
    sprintf(msg, "Could not open directory %s\n", p_OutDirectory->path);
    OutputDebugStringA(msg);
  }
}

void fileCloseDirectory(Directory* p_Directory)
{
  if (p_Directory->handle)
  {
    FindClose(p_Directory->handle);
  }
}

void fileParentDirectory(Directory* p_Directory)
{

  Directory newDirectory;

  const char* lastDirectorySeparator = strrchr(p_Directory->path, '\\');
  size_t index = lastDirectorySeparator - p_Directory->path;

  if (index > 0)
  {

    strncpy(newDirectory.path, p_Directory->path, index);
    newDirectory.path[index] = 0;

    lastDirectorySeparator = strrchr(newDirectory.path, '\\');
    size_t second_index = lastDirectorySeparator - newDirectory.path;

    if (lastDirectorySeparator)
    {
      newDirectory.path[second_index] = 0;
    }
    else
    {
      newDirectory.path[index] = 0;
    }

    fileOpenDirectory(newDirectory.path, &newDirectory);

    // Update directory
    if (newDirectory.handle)
    {
      *p_Directory = newDirectory;
    }
  }
}

void fileSubDirectory(Directory* p_Directory, const char* p_SubDirectoryName)
{

  // Remove the last '*' from the path. It will be re-added by the fileOpen.
  if (stringEndsWithChar(p_Directory->path, '*'))
  {
    p_Directory->path[strlen(p_Directory->path) - 1] = 0;
  }

  strcat(p_Directory->path, p_SubDirectoryName);
  fileOpenDirectory(p_Directory->path, p_Directory);
}

void fileFindFilesInPath(const char* p_FilePattern, StringArray& p_Files)
{
  p_Files.clear();

  WIN32_FIND_DATAA findData;
  HANDLE hFind;
  if ((hFind = FindFirstFileA(p_FilePattern, &findData)) != INVALID_HANDLE_VALUE)
  {
    do
    {

      p_Files.intern(findData.cFileName);

    } while (FindNextFileA(hFind, &findData) != 0);
    FindClose(hFind);
  }
  else
  {
    char msg[256];
    sprintf(msg, "Cannot find file %s\n", p_FilePattern);
    OutputDebugStringA(msg);
  }
}

void fileFindFilesInPath(
    const char* p_Extension,
    const char* p_SearchPattern,
    StringArray& p_Files,
    StringArray& p_Directories)
{
  p_Files.clear();
  p_Directories.clear();

  WIN32_FIND_DATAA findData;
  HANDLE hFind;
  if ((hFind = FindFirstFileA(p_SearchPattern, &findData)) != INVALID_HANDLE_VALUE)
  {
    do
    {
      if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
      {
        p_Directories.intern(findData.cFileName);
      }
      else
      {
        // If filename contains the extension, add it
        if (strstr(findData.cFileName, p_Extension))
        {
          p_Files.intern(findData.cFileName);
        }
      }

    } while (FindNextFileA(hFind, &findData) != 0);
    FindClose(hFind);
  }
  else
  {
    char msg[256];
    sprintf(msg, "Cannot find directory %s\n", p_SearchPattern);
    OutputDebugStringA(msg);
  }
}

void environmentVariableGet(const char* p_Name, char* p_Output, uint32_t p_OutputSize)
{
  ExpandEnvironmentStringsA(p_Name, p_Output, p_OutputSize);
}

char* fileReadBinary(const char* p_Filename, Allocator* p_Allocator, size_t* p_Size)
{
  char* outData = 0;

  FILE* file = fopen(p_Filename, "rb");

  if (file)
  {

    // TODO: Use filesize or read result ?
    size_t filesize = fileGetSize(file);

    outData = (char*)FRAMEWORK_ALLOCA(filesize + 1, p_Allocator);
    fread(outData, filesize, 1, file);
    outData[filesize] = 0;

    if (p_Size)
      *p_Size = filesize;

    fclose(file);
  }

  return outData;
}

char* fileReadText(const char* p_Filename, Allocator* p_Allocator, size_t* size)
{
  char* text = 0;

  FILE* file = fopen(p_Filename, "r");

  if (file)
  {
    size_t filesize = fileGetSize(file);
    text = (char*)FRAMEWORK_ALLOCA(filesize + 1, p_Allocator);
    // Correct: use elementcount as filesize, bytesRead becomes the actual bytes read
    // AFTER the end of line conversion for Windows (it uses \r\n).
    size_t bytesRead = fread(text, 1, filesize, file);

    text[bytesRead] = 0;

    if (size)
      *size = filesize;

    fclose(file);
  }

  return text;
}

FileReadResult fileReadBinary(const char* p_Filename, Allocator* p_Allocator)
{
  FileReadResult result{nullptr, 0};

  FILE* file = fopen(p_Filename, "rb");

  if (file)
  {

    // TODO: Use filesize or read result ?
    size_t filesize = fileGetSize(file);

    result.data = (char*)FRAMEWORK_ALLOCA(filesize, p_Allocator);
    fread(result.data, filesize, 1, file);

    result.size = filesize;

    fclose(file);
  }

  return result;
}

FileReadResult fileReadText(const char* p_Filename, Allocator* p_Allocator)
{
  FileReadResult result{nullptr, 0};

  FILE* file = fopen(p_Filename, "r");

  if (file)
  {

    size_t filesize = fileGetSize(file);
    result.data = (char*)FRAMEWORK_ALLOCA(filesize + 1, p_Allocator);
    // Correct: use elementcount as filesize, bytesRead becomes the actual bytes read
    // AFTER the end of line conversion for Windows (it uses \r\n).
    size_t bytesRead = fread(result.data, 1, filesize, file);

    result.data[bytesRead] = 0;

    result.size = filesize;

    fclose(file);
  }

  return result;
}

void fileWriteBinary(const char* p_Filename, void* p_Memory, size_t p_Size)
{
  FILE* file = fopen(p_Filename, "wb");
  fwrite(p_Memory, p_Size, 1, file);
  fclose(file);
}

/// Scoped file
ScopedFile::ScopedFile(const char* p_Filename, const char* p_Mode)
{
  fileOpen(p_Filename, p_Mode, &m_File);
}

ScopedFile::~ScopedFile() { fileClose(m_File); }
} // namespace Framework
