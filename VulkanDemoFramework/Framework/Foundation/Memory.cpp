#include "Memory.hpp"
#include <assert.h>

#include <Externals/tlsf.h>

#include <stdlib.h>
#include <memory.h>

#if defined FRAMEWORK_IMGUI
#  include <Externals/imgui/imgui.h>
#endif // FRAMEWORK_IMGUI

#define HEAP_ALLOCATOR_STATS

namespace Framework
{
static MemoryService g_MemoryService;

// Locals
static size_t g_Size = FRAMEWORK_MEGA(32) + tlsf_size() + 8;

//
// Walker methods
static void exitWalker(void* p_Ptr, size_t p_Size, int p_Used, void* p_User);
static void imguiWalker(void* p_Ptr, size_t p_Size, int p_Used, void* p_User);

MemoryService* MemoryService::instance() { return &g_MemoryService; }

//
//
void MemoryService::init(void* p_Configuration)
{
  OutputDebugStringA("Memory Service Init\n");
  MemoryServiceConfiguration* memoryConfiguration =
      static_cast<MemoryServiceConfiguration*>(p_Configuration);
  m_SystemAllocator.init(memoryConfiguration ? memoryConfiguration->MaximumDynamicSize : g_Size);
}

void MemoryService::shutdown()
{
  m_SystemAllocator.shutdown();
  OutputDebugStringA("Memory Service Shutdown\n");
}

void exitWalker(void* p_Ptr, size_t p_Size, int p_Used, void* p_User)
{
  MemoryStatistics* stats = (MemoryStatistics*)p_User;
  stats->add(p_Used ? p_Size : 0);

  if (p_Used)
  {
    char msg[256];
    sprintf(msg, "Found active allocation %p, %llu\n", p_Ptr, p_Size);
    OutputDebugStringA(msg);
  }
}

#if defined FRAMEWORK_IMGUI
void imguiWalker(void* p_Ptr, size_t p_Size, int p_Used, void* p_User)
{

  uint32_t memorySize = (uint32_t)p_Size;
  const char* memoryUnit = "b";
  if (memorySize > 1024 * 1024)
  {
    memorySize /= 1024 * 1024;
    memoryUnit = "Mb";
  }
  else if (memorySize > 1024)
  {
    memorySize /= 1024;
    memoryUnit = "kb";
  }
  ImGui::Text("\t%p %s size: %4llu %s\n", p_Ptr, p_Used ? "used" : "free", memorySize, memoryUnit);

  MemoryStatistics* stats = (MemoryStatistics*)p_User;
  stats->add(p_Used ? p_Size : 0);
}

void MemoryService::imguiDraw()
{

  if (ImGui::Begin("Memory Service"))
  {

    m_SystemAllocator.debugUi();
  }
  ImGui::End();
}
#endif // FRAMEWORK_IMGUI

void MemoryService::test() { assert(false && "Not implemented"); }
//---------------------------------------------------------------------------//
// Heap allocator
//---------------------------------------------------------------------------//
HeapAllocator::~HeapAllocator() {}

void HeapAllocator::init(size_t p_Size)
{
  // Allocate
  m_Memory = malloc(p_Size);
  m_MaxSize = p_Size;
  m_AllocatedSize = 0;

  m_TlsfHandle = tlsf_create_with_pool(m_Memory, p_Size);

  {
    char msg[256];
    sprintf(msg, "HeapAllocator of size %llu created\n", p_Size);
    OutputDebugStringA(msg);
  }
}

void HeapAllocator::shutdown()
{

  // Check memory at the application exit.
  MemoryStatistics stats{0, m_MaxSize};
  pool_t pool = tlsf_get_pool(m_TlsfHandle);
  tlsf_walk_pool(pool, exitWalker, (void*)&stats);

  if (stats.m_AllocatedBytes)
  {
    char msg[256];
    sprintf(
        msg,
        "HeapAllocator Shutdown.\n===============\nFAILURE! Allocated memory detected. allocated "
        "%llu, total %llu\n===============\n\n",
        stats.m_AllocatedBytes,
        stats.m_TotalBytes);
    OutputDebugStringA(msg);
  }
  else
  {
    OutputDebugStringA("HeapAllocator Shutdown - all memory free!\n");
  }

  // assert(stats.m_AllocatedBytes == 0 && "Allocations still present. Check your code!");
  if (stats.m_AllocatedBytes == 0)
    OutputDebugStringA("Allocations still present. Check your code!");

  tlsf_destroy(m_TlsfHandle);

  free(m_Memory);
}

#if defined FRAMEWORK_IMGUI
void HeapAllocator::debugUi()
{

  ImGui::Separator();
  ImGui::Text("Heap Allocator");
  ImGui::Separator();
  MemoryStatistics stats{0, m_MaxSize};
  pool_t pool = tlsf_get_pool(m_TlsfHandle);
  tlsf_walk_pool(pool, imguiWalker, (void*)&stats);

  ImGui::Separator();
  ImGui::Text("\tAllocation count %d", stats.m_AllocationCount);
  ImGui::Text(
      "\tAllocated %llu K, free %llu Mb, total %llu Mb",
      stats.m_AllocatedBytes / (1024 * 1024),
      (m_MaxSize - stats.m_AllocatedBytes) / (1024 * 1024),
      m_MaxSize / (1024 * 1024));
}
#endif // FRAMEWORK_IMGUI

void* HeapAllocator::allocate(size_t p_Size, size_t p_Alignment)
{
#if defined(HEAP_ALLOCATOR_STATS)
  void* allocatedMemory = p_Alignment == 1 ? tlsf_malloc(m_TlsfHandle, p_Size)
                                           : tlsf_memalign(m_TlsfHandle, p_Alignment, p_Size);
  size_t actualSize = tlsf_block_size(allocatedMemory);
  m_AllocatedSize += actualSize;

  return allocatedMemory;
#endif // HEAP_ALLOCATOR_STATS
}

void* HeapAllocator::allocate(size_t p_Size, size_t p_Alignment, const char* p_File, int p_Line)
{
  return allocate(p_Size, p_Alignment);
}

void HeapAllocator::deallocate(void* p_Pointer)
{
#if defined(HEAP_ALLOCATOR_STATS)
  size_t actualSize = tlsf_block_size(p_Pointer);
  m_AllocatedSize -= actualSize;

  tlsf_free(m_TlsfHandle, p_Pointer);
#endif
}
//---------------------------------------------------------------------------//
// Linear allocator
//---------------------------------------------------------------------------//
LinearAllocator::~LinearAllocator() {}

void LinearAllocator::init(size_t p_Size)
{

  m_Memory = (uint8_t*)malloc(p_Size);
  m_TotalSize = p_Size;
  m_AllocatedSize = 0;
}

void LinearAllocator::shutdown()
{
  clear();
  free(m_Memory);
}

void* LinearAllocator::allocate(size_t p_Size, size_t p_Alignment)
{
  assert(p_Size > 0);

  const size_t newStart = memoryAlign(m_AllocatedSize, p_Alignment);
  assert(newStart < m_TotalSize);
  const size_t newAllocatedSize = newStart + p_Size;
  if (newAllocatedSize > m_TotalSize)
  {
    assert(false && "Overflow");
    return nullptr;
  }

  m_AllocatedSize = newAllocatedSize;
  return m_Memory + newStart;
}

void* LinearAllocator::allocate(size_t p_Size, size_t p_Alignment, const char* p_File, int p_Line)
{
  return allocate(p_Size, p_Alignment);
}

void LinearAllocator::deallocate(void*)
{
  // This allocator does not allocate on a per-pointer base!
}

void LinearAllocator::clear() { m_AllocatedSize = 0; }
//---------------------------------------------------------------------------//
// Memory methods
//---------------------------------------------------------------------------//
void memoryCopy(void* p_Destination, void* p_Source, size_t p_Size)
{
  memcpy(p_Destination, p_Source, p_Size);
}

size_t memoryAlign(size_t p_Size, size_t p_Alignment)
{
  const size_t p_AlignmentMask = p_Alignment - 1;
  return (p_Size + p_AlignmentMask) & ~p_AlignmentMask;
}
//---------------------------------------------------------------------------//
// Malloc allocator
//---------------------------------------------------------------------------//
void* MallocAllocator::allocate(size_t p_Size, size_t p_Alignment) { return malloc(p_Size); }

void* MallocAllocator::allocate(size_t p_Size, size_t p_Alignment, const char* p_File, int p_Line)
{
  return malloc(p_Size);
}

void MallocAllocator::deallocate(void* p_Pointer) { free(p_Pointer); }
//---------------------------------------------------------------------------//
// Stack allocator
//---------------------------------------------------------------------------//
void StackAllocator::init(size_t p_Size)
{
  m_Memory = (uint8_t*)malloc(p_Size);
  m_AllocatedSize = 0;
  m_TotalSize = p_Size;
}

void StackAllocator::shutdown() { free(m_Memory); }

void* StackAllocator::allocate(size_t p_Size, size_t p_Alignment)
{
  assert(p_Size > 0);

  const size_t newStart = memoryAlign(m_AllocatedSize, p_Alignment);
  assert(newStart < m_TotalSize);
  const size_t newAllocatedSize = newStart + p_Size;
  if (newAllocatedSize > m_TotalSize)
  {
    assert(false && "Overflow");
    return nullptr;
  }

  m_AllocatedSize = newAllocatedSize;
  return m_Memory + newStart;
}

void* StackAllocator::allocate(size_t p_Size, size_t p_Alignment, const char* file, int line)
{
  return allocate(p_Size, p_Alignment);
}

void StackAllocator::deallocate(void* p_Pointer)
{

  assert(p_Pointer >= m_Memory);
  assert(
      p_Pointer < m_Memory + m_TotalSize &&
      "Out of bound free on linear allocator (outside bounds)");
  assert(
      p_Pointer < m_Memory + m_AllocatedSize &&
      "Out of bound free on linear allocator (inside bounds, after allocated)");

  const size_t sizeAtPointer = (uint8_t*)p_Pointer - m_Memory;

  m_AllocatedSize = sizeAtPointer;
}

size_t StackAllocator::getMarker() { return m_AllocatedSize; }

void StackAllocator::freeMarker(size_t p_Marker)
{
  const size_t difference = p_Marker - m_AllocatedSize;
  if (difference > 0)
  {
    m_AllocatedSize = p_Marker;
  }
}

void StackAllocator::clear() { m_AllocatedSize = 0; }
//---------------------------------------------------------------------------//
// Double allocator
//---------------------------------------------------------------------------//
void DoubleStackAllocator::init(size_t p_Size)
{
  m_Memory = (uint8_t*)malloc(p_Size);
  m_Top = p_Size;
  m_Bottom = 0;
  m_TotalSize = p_Size;
}

void DoubleStackAllocator::shutdown() { free(m_Memory); }

void* DoubleStackAllocator::allocate(size_t p_Size, size_t p_Alignment)
{
  assert(false);
  return nullptr;
}

void* DoubleStackAllocator::allocate(size_t p_Size, size_t p_Alignment, const char* file, int line)
{
  assert(false);
  return nullptr;
}

void DoubleStackAllocator::deallocate(void* p_Pointer) { assert(false); }

void* DoubleStackAllocator::allocateTop(size_t p_Size, size_t p_Alignment)
{
  assert(p_Size > 0);

  const size_t newStart = memoryAlign(m_Top - p_Size, p_Alignment);
  if (newStart <= m_Bottom)
  {
    assert(false && "Overflow Crossing");
    return nullptr;
  }

  m_Top = newStart;
  return m_Memory + newStart;
}

void* DoubleStackAllocator::allocateBottom(size_t p_Size, size_t p_Alignment)
{
  assert(p_Size > 0);

  const size_t newStart = memoryAlign(m_Bottom, p_Alignment);
  const size_t newAllocatedSize = newStart + p_Size;
  if (newAllocatedSize >= m_Top)
  {
    assert(false && "Overflow Crossing");
    return nullptr;
  }

  m_Bottom = newAllocatedSize;
  return m_Memory + newStart;
}

void DoubleStackAllocator::deallocateTop(size_t p_Size)
{
  if (p_Size > m_TotalSize - m_Top)
  {
    m_Top = m_TotalSize;
  }
  else
  {
    m_Top += p_Size;
  }
}

void DoubleStackAllocator::deallocateBottom(size_t p_Size)
{
  if (p_Size > m_Bottom)
  {
    m_Bottom = 0;
  }
  else
  {
    m_Bottom -= p_Size;
  }
}

size_t DoubleStackAllocator::getTopMarker() { return m_Top; }

size_t DoubleStackAllocator::getBottomMarker() { return m_Bottom; }

void DoubleStackAllocator::freeTopNarker(size_t p_Marker)
{
  if (p_Marker > m_Top && p_Marker < m_TotalSize)
  {
    m_Top = p_Marker;
  }
}

void DoubleStackAllocator::freeBottomMarker(size_t p_Marker)
{
  if (p_Marker < m_Bottom)
  {
    m_Bottom = p_Marker;
  }
}

void DoubleStackAllocator::clearTop() { m_Top = m_TotalSize; }

void DoubleStackAllocator::clearBottom() { m_Bottom = 0; }

} // namespace Framework
