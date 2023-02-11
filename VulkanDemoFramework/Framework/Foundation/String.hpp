#pragma once

#include "Foundation/Prerequisites.hpp"

namespace Framework
{

/// Forward declarations
struct Allocator;

template <typename K, typename V> struct FlatHashMap;

struct FlatHashMapIterator;

//
// String view that references an already existing stream of chars.
struct StringView
{
  char* m_Text;
  size_t m_Length;

  // static StringView

  static bool equals(const StringView& a, const StringView& b);
  static void copyTo(const StringView& a, char* buffer, size_t m_BufferSize);
}; // struct StringView

//
// Class that preallocates a buffer and appends strings to it. Reserve an additional byte for the
// null termination when needed.
struct StringBuffer
{

  void init(size_t size, Allocator* allocator);
  void shutdown();

  void append(const char* p_String);
  void append(const StringView& p_Text);
  void appendMemory(void* p_Memory, size_t p_Size); // Memory version of append.
  void append(const StringBuffer& p_OtherBuffer);
  void appendFormatted(const char* p_Format, ...); // Formatted version of append.

  char* appendUse(const char* p_String);
  char* appendUseFormatted(const char* p_Format, ...);
  char* appendUse(const StringView& p_Text); // Append and returns a pointer to the start. Used for
                                             // strings mostly.
  char* appendUseSubstring(
      const char* p_String,
      uint32_t p_StartIndex,
      uint32_t p_EndIndex); // Append a substring of the passed string.

  void closeCurrentString();

  // Index interface
  uint32_t getIndex(const char* p_Text) const;
  const char* getText(uint32_t p_Index) const;

  char* reserve(size_t p_Size);

  char* current() { return m_Data + m_CurrentSize; }

  void clear();

  char* m_Data = nullptr;
  uint32_t m_BufferSize = 1024;
  uint32_t m_CurrentSize = 0;
  Allocator* m_Allocator = nullptr;

}; // struct StringBuffer

//
//
struct StringArray
{

  void init(uint32_t p_Size, Allocator* p_Allocator);
  void shutdown();
  void clear();

  FlatHashMapIterator* beginStringIteration();
  size_t getStringCount() const;
  const char* getString(uint32_t p_Index) const;
  const char* getNextString(FlatHashMapIterator* p_Iterator) const;
  bool hasNextString(FlatHashMapIterator* p_Iterator) const;

  const char* intern(const char* p_String);

  FlatHashMap<uint64_t, uint32_t>*
      m_StringToIndex; // Note: trying to avoid bringing the hash map header.
  FlatHashMapIterator* m_StringsIterator;

  char* m_Data = nullptr;
  uint32_t m_BufferSize = 1024;
  uint32_t m_CurrentSize = 0;

  Allocator* m_Allocator = nullptr;

}; // struct StringArray

} // namespace Framework
