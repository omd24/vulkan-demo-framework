#include "String.hpp"
#include "Memory.hpp"
#include "HashMap.hpp"

#include <stdio.h>
#include <stdarg.h>
#include <memory.h>
#include <assert.h>

namespace Framework
{
/// StringView
bool StringView::equals(const StringView& a, const StringView& b)
{
  if (a.m_Length != b.m_Length)
    return false;

  for (uint32_t i = 0; i < a.m_Length; ++i)
  {
    if (a.m_Text[i] != b.m_Text[i])
    {
      return false;
    }
  }

  return true;
}

void StringView::copyTo(const StringView& p_Text, char* p_Buffer, size_t p_BufferSize)
{
  // Take in account the null vector
  const size_t max_length = p_BufferSize - 1 < p_Text.m_Length ? p_BufferSize - 1 : p_Text.m_Length;
  memoryCopy(p_Buffer, p_Text.m_Text, max_length);
  p_Buffer[p_Text.m_Length] = 0;
}

//
/// StringBuffer
void StringBuffer::init(size_t p_Size, Allocator* p_Allocator)
{
  if (m_Data)
  {
    m_Allocator->deallocate(m_Data);
  }

  if (p_Size < 1)
  {
    OutputDebugStringA("ERROR: Buffer cannot be empty!\n");
    return;
  }
  m_Allocator = p_Allocator;
  m_Data = (char*)FRAMEWORK_ALLOCA(p_Size + 1, p_Allocator);
  assert(m_Data);
  m_Data[0] = 0;
  m_BufferSize = (uint32_t)p_Size;
  m_CurrentSize = 0;
}

void StringBuffer::shutdown()
{

  FRAMEWORK_FREE(m_Data, m_Allocator);

  m_BufferSize = m_CurrentSize = 0;
}

void StringBuffer::append(const char* p_String) { appendFormatted("%s", p_String); }

void StringBuffer::appendFormatted(const char* p_Format, ...)
{
  if (m_CurrentSize >= m_BufferSize)
  {
    OutputDebugStringA("Buffer full! Please allocate more size.\n");
    return;
  }

  // TODO: safer version!
  va_list args;
  va_start(args, p_Format);
  int writtenChars =
      vsnprintf_s(&m_Data[m_CurrentSize], m_BufferSize - m_CurrentSize, _TRUNCATE, p_Format, args);

  m_CurrentSize += writtenChars > 0 ? writtenChars : 0;
  va_end(args);

  if (writtenChars < 0)
  {
    OutputDebugStringA("New string too big for current buffer! Please allocate more size.\n");
  }
}

void StringBuffer::append(const StringView& p_Text)
{
  const size_t max_length = m_CurrentSize + p_Text.m_Length < m_BufferSize
                                ? p_Text.m_Length
                                : m_BufferSize - m_CurrentSize;
  if (max_length == 0 || max_length >= m_BufferSize)
  {
    OutputDebugStringA("Buffer full! Please allocate more size.\n");
    return;
  }

  memcpy(&m_Data[m_CurrentSize], p_Text.m_Text, max_length);
  m_CurrentSize += (uint32_t)max_length;

  // Add null termination for string.
  // By allocating one extra character for the null termination this is always safe to do.
  m_Data[m_CurrentSize] = 0;
}

void StringBuffer::appendMemory(void* p_Memory, size_t p_Size)
{

  if (m_CurrentSize + p_Size >= m_BufferSize)
  {
    OutputDebugStringA("Buffer full! Please allocate more size.\n");
    return;
  }

  memcpy(&m_Data[m_CurrentSize], p_Memory, p_Size);
  m_CurrentSize += (uint32_t)p_Size;
}

void StringBuffer::append(const StringBuffer& p_OtherBuffer)
{

  if (p_OtherBuffer.m_CurrentSize == 0)
  {
    return;
  }

  if (m_CurrentSize + p_OtherBuffer.m_CurrentSize >= m_BufferSize)
  {
    OutputDebugStringA("Buffer full! Please allocate more size.\n");
    return;
  }

  memcpy(&m_Data[m_CurrentSize], p_OtherBuffer.m_Data, p_OtherBuffer.m_CurrentSize);
  m_CurrentSize += p_OtherBuffer.m_CurrentSize;
}

char* StringBuffer::appendUse(const char* p_String) { return appendUseFormatted("%s", p_String); }

char* StringBuffer::appendUseFormatted(const char* p_Format, ...)
{
  uint32_t cachedOffset = this->m_CurrentSize;

  // TODO: safer version!
  // TODO: do not copy paste!
  if (m_CurrentSize >= m_BufferSize)
  {
    OutputDebugStringA("Buffer full! Please allocate more size.\n");
    return nullptr;
  }

  va_list args;
  va_start(args, p_Format);
  int writtenChars =
      vsnprintf_s(&m_Data[m_CurrentSize], m_BufferSize - m_CurrentSize, _TRUNCATE, p_Format, args);
  m_CurrentSize += writtenChars > 0 ? writtenChars : 0;
  va_end(args);

  if (writtenChars < 0)
  {
    OutputDebugStringA("New string too big for current buffer! Please allocate more size.\n");
  }

  // Add null termination for string.
  // By allocating one extra character for the null termination this is always safe to do.
  m_Data[m_CurrentSize] = 0;
  ++m_CurrentSize;

  return this->m_Data + cachedOffset;
}

char* StringBuffer::appendUse(const StringView& p_Text)
{
  uint32_t cachedOffset = this->m_CurrentSize;

  append(p_Text);
  ++m_CurrentSize;

  return this->m_Data + cachedOffset;
}

char* StringBuffer::appendUseSubstring(
    const char* p_String, uint32_t p_StartIndex, uint32_t p_EndIndex)
{
  uint32_t size = p_EndIndex - p_StartIndex;
  if (m_CurrentSize + size >= m_BufferSize)
  {
    OutputDebugStringA("Buffer full! Please allocate more size.\n");
    return nullptr;
  }

  uint32_t cachedOffset = this->m_CurrentSize;

  memcpy(&m_Data[m_CurrentSize], p_String, size);
  m_CurrentSize += size;

  m_Data[m_CurrentSize] = 0;
  ++m_CurrentSize;

  return this->m_Data + cachedOffset;
}

void StringBuffer::closeCurrentString()
{
  m_Data[m_CurrentSize] = 0;
  ++m_CurrentSize;
}

uint32_t StringBuffer::getIndex(const char* p_Text) const
{
  uint64_t text_distance = p_Text - m_Data;
  // TODO: how to handle an error here ?
  return text_distance < m_BufferSize ? uint32_t(text_distance) : UINT32_MAX;
}

const char* StringBuffer::getText(uint32_t p_Index) const
{
  // TODO: how to handle an error here ?
  return p_Index < m_BufferSize ? static_cast<const char*>(m_Data + p_Index) : nullptr;
}

char* StringBuffer::reserve(size_t p_Size)
{
  if (m_CurrentSize + p_Size >= m_BufferSize)
    return nullptr;

  uint32_t offset = m_CurrentSize;
  m_CurrentSize += (uint32_t)p_Size;

  return m_Data + offset;
}

void StringBuffer::clear()
{
  m_CurrentSize = 0;
  m_Data[0] = 0;
}

/// StringArray
void StringArray::init(uint32_t p_Size, Allocator* p_Allocator)
{

  m_Allocator = p_Allocator;
  // Allocate also memory for the hash map
  char* allocatedMemory = (char*)p_Allocator->allocate(
      p_Size + sizeof(FlatHashMap<uint64_t, uint32_t>) + sizeof(FlatHashMapIterator), 1);
  m_StringToIndex = (FlatHashMap<uint64_t, uint32_t>*)allocatedMemory;
  m_StringToIndex->init(m_Allocator, 8);
  m_StringToIndex->setDefaultValue(UINT32_MAX);

  m_StringsIterator =
      (FlatHashMapIterator*)(allocatedMemory + sizeof(FlatHashMap<uint64_t, uint32_t>));

  m_Data = allocatedMemory + sizeof(FlatHashMap<uint64_t, uint32_t>) + sizeof(FlatHashMapIterator);

  m_BufferSize = p_Size;
  m_CurrentSize = 0;
}

void StringArray::shutdown()
{
  // m_StringToIndex contains ALL the memory including data.
  FRAMEWORK_FREE(m_StringToIndex, m_Allocator);

  m_BufferSize = m_CurrentSize = 0;
}

void StringArray::clear()
{
  m_CurrentSize = 0;

  m_StringToIndex->clear();
}

FlatHashMapIterator* StringArray::beginStringIteration()
{
  *m_StringsIterator = m_StringToIndex->iteratorBegin();
  return m_StringsIterator;
}

size_t StringArray::getStringCount() const { return m_StringToIndex->m_Size; }

const char* StringArray::getNextString(FlatHashMapIterator* p_Iterator) const
{
  uint32_t index = m_StringToIndex->get(*p_Iterator);
  m_StringToIndex->iteratorAdvance(*p_Iterator);
  const char* string = getString(index);
  return string;
}

bool StringArray::hasNextString(FlatHashMapIterator* p_Iterator) const
{
  return p_Iterator->isValid();
}

const char* StringArray::getString(uint32_t p_Index) const
{
  uint32_t dataIndex = p_Index;
  if (dataIndex < m_CurrentSize)
  {
    return m_Data + dataIndex;
  }
  return nullptr;
}

const char* StringArray::intern(const char* p_String)
{
  static size_t seed = 0xf2ea4ffad;
  const size_t length = strlen(p_String);
  const size_t hashedString = Framework::hashBytes((void*)p_String, length, seed);

  uint32_t stringIndex = m_StringToIndex->get(hashedString);
  if (stringIndex != UINT32_MAX)
  {
    return m_Data + stringIndex;
  }

  stringIndex = m_CurrentSize;
  // Increase current buffer with new interned string
  m_CurrentSize += (uint32_t)length + 1; // null termination
  strcpy(m_Data + stringIndex, p_String);

  // Update hash map
  m_StringToIndex->insert(hashedString, stringIndex);

  return m_Data + stringIndex;
}

} // namespace Framework
