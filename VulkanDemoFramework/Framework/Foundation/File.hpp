#pragma once

#include "Foundation/Prerequisites.hpp"
#include <stdio.h>

namespace Framework
{
struct Allocator;
struct StringArray;

#if defined(_WIN64)

typedef struct __FILETIME
{
  unsigned long dwLowDateTime;
  unsigned long dwHighDateTime;
} FILETIME, *PFILETIME, *LPFILETIME;

using FileTime = __FILETIME;

#endif

using FileHandle = FILE*;

static const uint32_t kMaxPath = 512;

//
//
struct Directory
{
  char path[kMaxPath];
  void* handle;
}; // struct Directory

struct FileReadResult
{
  char* data;
  size_t size;
};

// Read file and allocate memory from allocator.
// User is responsible for freeing the memory.
char* fileReadBinary(const char* p_Filename, Allocator* p_Allocator, size_t* p_Size);
char* fileReadText(const char* p_Filename, Allocator* p_Allocator, size_t* p_Size);

FileReadResult fileReadBinary(const char* p_Filename, Allocator* p_Allocator);
FileReadResult fileReadText(const char* p_Filename, Allocator* p_Allocator);

void fileWriteBinary(const char* p_Filename, void* p_Memory, size_t p_Size);

bool fileExists(const char* p_Path);
void fileOpen(const char* p_Filename, const char* p_Mode, FileHandle* m_File);
void fileClose(FileHandle m_File);
size_t fileWrite(uint8_t* p_Memory, uint32_t p_ElementSize, uint32_t p_Count, FileHandle m_File);
bool fileDelete(const char* p_Path);

#if defined(_WIN64)
FileTime fileLastWriteTime(const char* p_Filename);
#endif

// Try to resolve path to non-relative version.
uint32_t fileResolveToFullPath(const char* p_Path, char* p_OutFullPath, uint32_t p_MaxSize);

// Inplace path methods
void fileDirectoryFromPath(
    char* p_Path); // Retrieve path without the filename. Path is a preallocated string buffer. It
                   // moves the terminator before the name of the file.
void filenameFromPath(char* p_Path);
char* fileExtensionFromPath(char* p_Path);

bool directoryExists(const char* p_Path);
bool directoryCreate(const char* p_Path);
bool directoryDelete(const char* p_Path);

void directoryCurrent(Directory* p_Directory);
void directoryChange(const char* p_Path);

void fileOpenDirectory(const char* p_Path, Directory* p_OutDirectory);
void fileCloseDirectory(Directory* p_Directory);
void fileParentDirectory(Directory* p_Directory);
void fileSubDirectory(Directory* p_Directory, const char* p_SubDirectoryName);

void fileFindFilesInPath(
    const char* p_FilePattern,
    StringArray& p_Files); // Search files matching file_pattern and puts them in files array.
                           // Examples: "..\\data\\*", "*.bin", "*.*"
void fileFindFilesInPath(
    const char* p_Extension,
    const char* p_SearchPattern,
    StringArray& p_Files,
    StringArray& p_Directories); // Search files and directories using search_patterns.

// TODO: move
void environmentVariableGet(const char* p_Name, char* p_Output, uint32_t p_OutputSize);

struct ScopedFile
{
  ScopedFile(const char* p_Filename, const char* p_Mode);
  ~ScopedFile();

  FileHandle m_File;
}; // struct ScopedFile

} // namespace Framework
