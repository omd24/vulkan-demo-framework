#include "Bit.hpp"
#include "Memory.hpp"

#include <string.h>

namespace Framework
{

uint32_t trailingZerosU32(uint32_t x)
{
  /*unsigned long result = 0;  // NOLINT(runtime/int)
  _BitScanForward( &result, x );
  return result;*/
#if defined(_MSC_VER)
  return _tzcnt_u32(x);
#endif
}

uint32_t leadingZeroesU32(uint32_t x)
{
  /*unsigned long result = 0;  // NOLINT(runtime/int)
  _BitScanReverse( &result, x );
  return result;*/
#if defined(_MSC_VER)
  return __lzcnt(x);
#endif
}

#if defined(_MSC_VER)
uint32_t leadingZeroesU32MSVC(uint32_t x)
{
  unsigned long result = 0; // NOLINT(runtime/int)
  if (_BitScanReverse(&result, x))
  {
    return 31 - result;
  }
  return 32;
}
#endif

uint64_t trailingZerosU64(uint64_t x)
{
#if defined(_MSC_VER)
  return _tzcnt_u64(x);
#endif
}

uint32_t roundUpToPowerOf2(uint32_t v)
{

  uint32_t nv = 1 << (32 - Framework::leadingZeroesU32(v));
#if 0
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v++;
#endif
  return nv;
}
void printBinary(uint64_t n)
{

  OutputDebugStringA("0b");
  for (uint32_t i = 0; i < 64; ++i)
  {
    uint64_t bit = (n >> (64 - i - 1)) & 0x1;

    {
      char msg[256];
      sprintf(msg, "%llu", bit);
      OutputDebugStringA(msg);
    }
  }
  OutputDebugStringA(" ");
}

void printBinary(uint32_t n)
{

  OutputDebugStringA("0b");
  for (uint32_t i = 0; i < 32; ++i)
  {
    uint32_t bit = (n >> (32 - i - 1)) & 0x1;

    {
      char msg[256];
      sprintf(msg, "%u", bit);
      OutputDebugStringA(msg);
    }
  }
  OutputDebugStringA(" ");
}

// BitSet /////////////////////////////////////////////////////////////////
void BitSet::init(Allocator* p_Allocator, uint32_t p_TotalBits)
{
  m_Allocator = p_Allocator;
  m_Bits = nullptr;
  m_Size = 0;

  resize(p_TotalBits);
}

void BitSet::shutdown() { FRAMEWORK_FREE(m_Bits, m_Allocator); }

void BitSet::resize(uint32_t p_TotalBits)
{
  uint8_t* old_bits = m_Bits;

  const uint32_t new_size = (p_TotalBits + 7) / 8;
  if (m_Size == new_size)
  {
    return;
  }

  m_Bits = (uint8_t*)FRAMEWORK_ALLOCAM(new_size, m_Allocator);

  if (old_bits)
  {
    memcpy(m_Bits, old_bits, m_Size);
    FRAMEWORK_FREE(old_bits, m_Allocator);
  }
  else
  {
    memset(m_Bits, 0, new_size);
  }

  m_Size = new_size;
}

} // namespace Framework
