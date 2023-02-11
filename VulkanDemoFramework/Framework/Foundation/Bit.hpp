#pragma once

#include "Foundation/Prerequisites.hpp"

namespace Framework
{
struct Allocator;

// Common methods:
uint32_t leadingZeroesU32(uint32_t x);
#if defined(_MSC_VER)
uint32_t leadingZeroesU32MSVC(uint32_t x);
#endif
uint32_t trailingZerosU32(uint32_t x);
uint64_t trailingZerosU64(uint64_t x);

uint32_t roundUpToPowerOf2(uint32_t v);

void printBinary(uint64_t n);
void printBinary(uint32_t n);

// class BitMask:

// An abstraction over a bitmask. It provides an easy way to iterate through the
// indexes of the set bits of a bitmask.  When Shift=0 (platforms with SSE),
// this is a true bitmask.  On non-SSE, platforms the arithematic used to
// emulate the SSE behavior works in bytes (Shift=3) and leaves each bytes as
// either 0x00 or 0x80.
//
// For example:
//   for (int i : BitMask<uint32_t, 16>(0x5)) -> yields 0, 2
//   for (int i : BitMask<uint64_t, 8, 3>(0x0000000080800000)) -> yields 2, 3
template <class T, int SignificantBits, int Shift = 0> class BitMask
{
  // static_assert( std::is_unsigned<T>::value, "" );
  // static_assert( Shift == 0 || Shift == 3, "" );

public:
  // These are useful for unit tests (gunit).
  using value_type = int;
  using iterator = BitMask;
  using const_iterator = BitMask;

  explicit BitMask(T mask) : m_Mask(mask) {}
  BitMask& operator++()
  {
    m_Mask &= (m_Mask - 1);
    return *this;
  }
  explicit operator bool() const { return m_Mask != 0; }
  int operator*() const { return lowestBitSet(); }
  uint32_t lowestBitSet() const { return trailingZerosU32(m_Mask) >> Shift; }
  uint32_t highestBitSet() const { return static_cast<uint32_t>((bitWidth(m_Mask) - 1) >> Shift); }

  BitMask begin() const { return *this; }
  BitMask end() const { return BitMask(0); }

  uint32_t trailingZeros() const
  {
    return trailingZerosU32(m_Mask); // >> Shift;
  }

  uint32_t leadingZeros() const
  {
    return leadingZeroesU32(m_Mask); // >> Shift;
  }

private:
  friend bool operator==(const BitMask& a, const BitMask& b) { return a.m_Mask == b.m_Mask; }
  friend bool operator!=(const BitMask& a, const BitMask& b) { return a.m_Mask != b.m_Mask; }

  T m_Mask;
}; // class BitMask

// Utility methods
inline uint32_t bitMask8(uint32_t bit) { return 1 << (bit & 7); }
inline uint32_t bitSlot8(uint32_t bit) { return bit / 8; }

//
//
struct BitSet
{
  void init(Allocator* m_Allocator, uint32_t m_TotalBits);
  void shutdown();

  void resize(uint32_t p_TotalBits);

  void setBit(uint32_t index) { m_Bits[index / 8] |= bitMask8(index); }
  void clearBit(uint32_t index) { m_Bits[index / 8] &= ~bitMask8(index); }
  uint8_t getBit(uint32_t index) { return m_Bits[index / 8] & bitMask8(index); }

  Allocator* m_Allocator = nullptr;
  uint8_t* m_Bits = nullptr;
  uint32_t m_Size = 0;

}; // struct BitSet

//
//
template <uint32_t SizeInBytes> struct BitSetFixed
{
  void setBit(uint32_t index) { m_Bits[index / 8] |= bitMask8(index); }
  void clearBit(uint32_t index) { m_Bits[index / 8] &= ~bitMask8(index); }
  uint8_t getBit(uint32_t index) { return m_Bits[index / 8] & bitMask8(index); }

  uint8_t m_Bits[SizeInBytes];

}; // struct BitSetFixed

} // namespace Framework
