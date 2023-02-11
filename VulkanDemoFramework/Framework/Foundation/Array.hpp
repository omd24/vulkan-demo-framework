#pragma once

#include "Memory.hpp"
#include <assert.h>

namespace Framework
{

/// Data structures

/// ArrayAligned
template <typename T> struct Array
{

  Array();
  ~Array();

  void init(Allocator* p_Allocator, uint32_t p_InitialCapacity, uint32_t p_InitialSize = 0);
  void shutdown();

  void push(const T& p_Element);
  T& pushUse(); // Grow the size and return T to be filled.

  void pop();
  void deleteSwap(uint32_t p_Index);

  T& operator[](uint32_t p_Index);
  const T& operator[](uint32_t p_Index) const;

  void clear();
  void setSize(uint32_t p_NewSize);
  void setCapacity(uint32_t p_NewCapacity);
  void grow(uint32_t p_NewCapacity);

  T& back();
  const T& back() const;

  T& front();
  const T& front() const;

  uint32_t sizeInBytes() const;
  uint32_t capacityInBytes() const;

  T* m_Data;
  uint32_t m_Size;     // Occupied size
  uint32_t m_Capacity; // Allocated capacity
  Allocator* m_Allocator;

}; // struct Array

/// ArrayView

// View over a contiguous memory block.
template <typename T> struct ArrayView
{

  ArrayView(T* p_Data, uint32_t p_Size);

  void set(T* p_Data, uint32_t p_Size);

  T& operator[](uint32_t p_Index);
  const T& operator[](uint32_t p_Index) const;

  T* m_Data;
  uint32_t m_Size;
}; // struct ArrayView

/// Implementation

/// ArrayAligned
template <typename T> inline Array<T>::Array()
{
  // assert( true );
}

template <typename T> inline Array<T>::~Array()
{
  // assert( m_Data == nullptr );
}

template <typename T>
inline void
Array<T>::init(Allocator* p_Allocator, uint32_t p_InitialCapacity, uint32_t p_InitialSize)
{
  m_Data = nullptr;
  m_Size = p_InitialSize;
  m_Capacity = 0;
  m_Allocator = p_Allocator;

  if (p_InitialCapacity > 0)
  {
    grow(p_InitialCapacity);
  }
}

template <typename T> inline void Array<T>::shutdown()
{
  if (m_Capacity > 0)
  {
    m_Allocator->deallocate(m_Data);
  }
  m_Data = nullptr;
  m_Size = m_Capacity = 0;
}

template <typename T> inline void Array<T>::push(const T& p_Element)
{
  if (m_Size >= m_Capacity)
  {
    grow(m_Capacity + 1);
  }

  m_Data[m_Size++] = p_Element;
}

template <typename T> inline T& Array<T>::pushUse()
{
  if (m_Size >= m_Capacity)
  {
    grow(m_Capacity + 1);
  }
  ++m_Size;

  return back();
}

template <typename T> inline void Array<T>::pop()
{
  assert(m_Size > 0);
  --m_Size;
}

template <typename T> inline void Array<T>::deleteSwap(uint32_t p_Index)
{
  assert(m_Size > 0 && p_Index < m_Size);
  m_Data[p_Index] = m_Data[--m_Size];
}

template <typename T> inline T& Array<T>::operator[](uint32_t p_Index)
{
  assert(p_Index < m_Size);
  return m_Data[p_Index];
}

template <typename T> inline const T& Array<T>::operator[](uint32_t p_Index) const
{
  assert(p_Index < m_Size);
  return m_Data[p_Index];
}

template <typename T> inline void Array<T>::clear() { m_Size = 0; }

template <typename T> inline void Array<T>::setSize(uint32_t p_NewSize)
{
  if (p_NewSize > m_Capacity)
  {
    grow(p_NewSize);
  }
  m_Size = p_NewSize;
}

template <typename T> inline void Array<T>::setCapacity(uint32_t p_NewCapacity)
{
  if (p_NewCapacity > m_Capacity)
  {
    grow(p_NewCapacity);
  }
}

template <typename T> inline void Array<T>::grow(uint32_t p_NewCapacity)
{
  if (p_NewCapacity < m_Capacity * 2)
  {
    p_NewCapacity = m_Capacity * 2;
  }
  else if (p_NewCapacity < 4)
  {
    p_NewCapacity = 4;
  }

  T* newData = (T*)m_Allocator->allocate(p_NewCapacity * sizeof(T), alignof(T));
  if (m_Capacity)
  {
    memoryCopy(newData, m_Data, m_Capacity * sizeof(T));

    m_Allocator->deallocate(m_Data);
  }

  m_Data = newData;
  m_Capacity = p_NewCapacity;
}

template <typename T> inline T& Array<T>::back()
{
  assert(m_Size);
  return m_Data[m_Size - 1];
}

template <typename T> inline const T& Array<T>::back() const
{
  assert(m_Size);
  return m_Data[m_Size - 1];
}

template <typename T> inline T& Array<T>::front()
{
  assert(m_Size);
  return m_Data[0];
}

template <typename T> inline const T& Array<T>::front() const
{
  assert(m_Size);
  return m_Data[0];
}

template <typename T> inline uint32_t Array<T>::sizeInBytes() const { return m_Size * sizeof(T); }

template <typename T> inline uint32_t Array<T>::capacityInBytes() const
{
  return m_Capacity * sizeof(T);
}

/// ArrayView
template <typename T>
inline ArrayView<T>::ArrayView(T* p_Data, uint32_t p_Size) : m_Data(p_Data), m_Size(p_Size)
{
}

template <typename T> inline void ArrayView<T>::set(T* p_Data, uint32_t p_Size)
{
  m_Data = p_Data;
  m_Size = p_Size;
}

template <typename T> inline T& ArrayView<T>::operator[](uint32_t p_Index)
{
  assert(p_Index < m_Size);
  return m_Data[p_Index];
}

template <typename T> inline const T& ArrayView<T>::operator[](uint32_t p_Index) const
{
  assert(p_Index < m_Size);
  return m_Data[p_Index];
}

} // namespace Framework
