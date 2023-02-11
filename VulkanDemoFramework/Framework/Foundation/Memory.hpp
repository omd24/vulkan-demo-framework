#pragma once

#include "Foundation/Prerequisites.hpp"
#include "Foundation/Service.hpp"

#define FRAMEWORK_IMGUI

namespace Framework
{
//---------------------------------------------------------------------------//
void memoryCopy(void* p_Destination, void* p_Source, size_t p_Size);
///  Calculate aligned memory size.
size_t memoryAlign(size_t p_Size, size_t p_Alignment);
//---------------------------------------------------------------------------//
struct MemoryStatistics
{
  size_t m_AllocatedBytes;
  size_t m_TotalBytes;

  uint32_t m_AllocationCount;

  void add(size_t p_AllocatedBytes)
  {
    if (p_AllocatedBytes > 0)
    {
      m_AllocatedBytes += p_AllocatedBytes;
      ++m_AllocationCount;
    }
  }
}; // struct MemoryStatistics
//---------------------------------------------------------------------------//
struct Allocator
{
  virtual ~Allocator() {}
  virtual void* allocate(size_t p_Size, size_t p_Alignment) = 0;
  virtual void* allocate(size_t p_Size, size_t p_Alignment, const char* p_File, int p_Line) = 0;

  virtual void deallocate(void* p_Pointer) = 0;
}; // struct Allocator
//---------------------------------------------------------------------------//
struct HeapAllocator : public Allocator
{
  ~HeapAllocator() override;

  void init(size_t p_Size);
  void shutdown();

#if defined FRAMEWORK_IMGUI
  void debugUi();
#endif // FRAMEWORK_IMGUI

  void* allocate(size_t p_Size, size_t p_Alignment) override;
  void* allocate(size_t p_Size, size_t p_Alignment, const char* p_File, int p_Line) override;

  void deallocate(void* p_Pointer) override;

  void* m_TlsfHandle;
  void* m_Memory;
  size_t m_AllocatedSize = 0;
  size_t m_MaxSize = 0;

}; // struct HeapAllocator
//---------------------------------------------------------------------------//
struct StackAllocator : public Allocator
{

  void init(size_t p_Size);
  void shutdown();

  void* allocate(size_t p_Size, size_t p_Alignment) override;
  void* allocate(size_t p_Size, size_t p_Alignment, const char* p_File, int p_Line) override;

  void deallocate(void* p_Pointer) override;

  size_t getMarker();
  void freeMarker(size_t m_Marker);

  void clear();

  uint8_t* m_Memory = nullptr;
  size_t m_TotalSize = 0;
  size_t m_AllocatedSize = 0;

}; // struct StackAllocator
//---------------------------------------------------------------------------//
struct DoubleStackAllocator : public Allocator
{

  void init(size_t p_Size);
  void shutdown();

  void* allocate(size_t p_Size, size_t p_Alignment) override;
  void* allocate(size_t p_Size, size_t p_Alignment, const char* p_File, int p_Line) override;
  void deallocate(void* p_Pointer) override;

  void* allocateTop(size_t p_Size, size_t p_Alignment);
  void* allocateBottom(size_t p_Size, size_t p_Alignment);

  void deallocateTop(size_t p_Size);
  void deallocateBottom(size_t p_Size);

  size_t getTopMarker();
  size_t getBottomMarker();

  void freeTopNarker(size_t m_Marker);
  void freeBottomMarker(size_t m_Marker);

  void clearTop();
  void clearBottom();

  uint8_t* m_Memory = nullptr;
  size_t m_TotalSize = 0;
  size_t m_Top = 0;
  size_t m_Bottom = 0;

}; // struct DoubleStackAllocator
//---------------------------------------------------------------------------//
/// Allocator that can only be reset.
struct LinearAllocator : public Allocator
{
  ~LinearAllocator();

  void init(size_t p_Size);
  void shutdown();

  void* allocate(size_t p_Size, size_t p_Alignment) override;
  void* allocate(size_t p_Size, size_t p_Alignment, const char* p_File, int p_Line) override;

  void deallocate(void* p_Pointer) override;

  void clear();

  uint8_t* m_Memory = nullptr;
  size_t m_TotalSize = 0;
  size_t m_AllocatedSize = 0;
}; // struct LinearAllocator
//---------------------------------------------------------------------------//
/// DANGER: this should be used for NON runtime processes, like compilation of resources.
struct MallocAllocator : public Allocator
{
  void* allocate(size_t p_Size, size_t p_Alignment) override;
  void* allocate(size_t p_Size, size_t p_Alignment, const char* p_File, int p_Line) override;

  void deallocate(void* p_Pointer) override;
};
//---------------------------------------------------------------------------//
// Memory service
//---------------------------------------------------------------------------//
struct MemoryServiceConfiguration
{
  size_t MaximumDynamicSize = 32 * 1024 * 1024; // Defaults to max 32MB of dynamic memory.
};
//---------------------------------------------------------------------------//
struct MemoryService : public Service
{
  FRAMEWORK_DECLARE_SERVICE(MemoryService);

  void init(void* p_Configuration);
  void shutdown();

#if defined FRAMEWORK_IMGUI
  void imguiDraw();
#endif // FRAMEWORK_IMGUI

  // Frame allocator
  LinearAllocator m_ScratchAllocator;
  HeapAllocator m_SystemAllocator;

  //
  // Test allocators.
  void test();

  static constexpr const char* ms_Name = "Framework memory service";

}; // struct MemoryService
//---------------------------------------------------------------------------//
// Macros helpers
//---------------------------------------------------------------------------//
#define FRAMEWORK_ALLOCA(size, allocator) ((allocator)->allocate(size, 1, __FILE__, __LINE__))
#define FRAMEWORK_ALLOCAM(size, allocator) \
  ((uint8_t*)(allocator)->allocate(size, 1, __FILE__, __LINE__))
#define FRAMEWORK_ALLOCAT(type, allocator) \
  ((type*)(allocator)->allocate(sizeof(type), 1, __FILE__, __LINE__))

#define FRAMEWORK_ALLOCAA(size, allocator, alignment) \
  ((allocator)->allocate(size, alignment, __FILE__, __LINE__))

#define FRAMEWORK_FREE(pointer, allocator) (allocator)->deallocate(pointer)

#define FRAMEWORK_KILO(size) (size * 1024)
#define FRAMEWORK_MEGA(size) (size * 1024 * 1024)
#define FRAMEWORK_GIGA(size) (size * 1024 * 1024 * 1024)

} // namespace Framework
