#pragma once

#include "Prerequisites.hpp"
#include "Foundation/Memory.hpp"
#include "Foundation/Bit.hpp"

#include "Externals/wyhash.h"

namespace Framework
{
/// Hash Map

static const uint64_t g_IteratorEnd = UINT64_MAX;

//
//
struct FindInfo
{
  uint64_t offset;
  uint64_t probeLength;
}; // struct FindInfo

//
//
struct FindResult
{
  uint64_t index;
  bool freeIndex; // States if the index is free or used.
};                // struct FindResult

//
// Iterator that stores the index of the entry.
struct FlatHashMapIterator
{
  uint64_t m_Index;

  bool isValid() const { return m_Index != g_IteratorEnd; }
  bool isInvalid() const { return m_Index == g_IteratorEnd; }
}; // struct FlatHashMapIterator

// A single block of empty control bytes for tables without any slots allocated.
// This enables removing a branch in the hot path of find().
int8_t* groupInitEmpty();

/// Probing
struct ProbeSequence
{

  static const uint64_t kWidth = 16; // TODO: this should be selectable.
  static const size_t kEngineHash = 0x31d3a36013e;

  ProbeSequence(uint64_t p_Hash, uint64_t p_Mask);

  uint64_t getOffset() const;
  uint64_t getOffset(uint64_t i) const;

  // 0-based probe index. The i-th probe in the probe sequence.
  uint64_t getIndex() const;

  void next();

  uint64_t m_Mask;
  uint64_t m_Offset;
  uint64_t m_Index = 0;

}; // struct ProbeSequence

template <typename K, typename V> struct FlatHashMap
{

  struct KeyValue
  {
    K key;
    V value;
  }; // struct KeyValue

  void init(Allocator* p_Allocator, uint64_t p_InitialCapacity);
  void shutdown();

  // Main interface
  FlatHashMapIterator find(const K& key);
  void insert(const K& key, const V& value);
  uint32_t remove(const K& key);
  uint32_t remove(const FlatHashMapIterator& it);

  V& get(const K& key);
  V& get(const FlatHashMapIterator& it);

  KeyValue& getStructure(const K& key);
  KeyValue& getStructure(const FlatHashMapIterator& it);

  void setDefaultValue(const V& value);

  // Iterators
  FlatHashMapIterator iteratorBegin();
  void iteratorAdvance(FlatHashMapIterator& iterator);

  void clear();
  void reserve(uint64_t p_NewSize);

  // Internal methods
  void eraseMeta(const FlatHashMapIterator& p_Iterator);

  FindResult findOrPrepareInsert(const K& key);
  FindInfo findFirstNonFull(uint64_t hash);

  uint64_t prepareInsert(uint64_t hash);

  ProbeSequence probe(uint64_t hash);
  void rehashAndGrowIfNecessary();

  void dropDeletesWithoutResize();
  uint64_t calculateSize(uint64_t p_NewCapacity);

  void initializeSlots();

  void resize(uint64_t p_NewCapacity);

  void iteratorSkipEmptyOrDeleted(FlatHashMapIterator& p_Iterator);

  // Sets the control byte, and if `i < Group::kWidth - 1`, set the cloned byte
  // at the end too.
  void setCtrl(uint64_t i, int8_t h);
  void resetCtrl();
  void resetGrowthLeft();

  int8_t* m_ControlBytes = groupInitEmpty();
  KeyValue* m_Slots = nullptr;

  uint64_t m_Size = 0;       // Occupied size
  uint64_t m_Capacity = 0;   // Allocated capacity
  uint64_t m_GrowthLeft = 0; // Number of empty space we can fill.

  Allocator* m_Allocator = nullptr;
  KeyValue m_DefaultKeyValue = {(K)-1, 0};

}; // struct FlatHashMap

// Implementation
//
template <typename T> inline uint64_t hashCalculate(const T& value, size_t seed = 0)
{
  return wyhash(&value, sizeof(T), seed, _wyp);
}

template <size_t N> inline uint64_t hashCalculate(const char (&value)[N], size_t seed = 0)
{
  return wyhash(value, strlen(value), seed, _wyp);
}

typedef const char* cstring;
template <> inline uint64_t hashCalculate(const cstring& value, size_t seed)
{
  return wyhash(value, strlen(value), seed, _wyp);
}

// Method to hash memory itself.
inline uint64_t hashBytes(void* data, size_t length, size_t seed = 0)
{
  return wyhash(data, length, seed, _wyp);
}

// https://gankra.github.io/blah/hashbrown-tldr/
// https://blog.waffles.space/2018/12/07/deep-dive-into-hashbrown/
// https://abseil.io/blog/20180927-swisstables
//

/// Control byte
// Following Google's abseil library convetion - based on performance.
static const int8_t kControlBitmaskEmpty = -128;  // 0b10000000;
static const int8_t kControlBitmaskDeleted = -2;  // 0b11111110;
static const int8_t kControlBitmaskSentinel = -1; // 0b11111111;

static bool controlIsEmpty(int8_t control) { return control == kControlBitmaskEmpty; }
static bool controlIsFull(int8_t control) { return control >= 0; }
static bool controlIsDeleted(int8_t control) { return control == kControlBitmaskDeleted; }
static bool controlIsEmptyOrDeleted(int8_t control) { return control < kControlBitmaskSentinel; }

/// Hashing

// Returns a hash seed.
//
// The seed consists of the ctrl_ pointer, which adds enough entropy to ensure
// non-determinism of iteration order in most cases.
// Implementation details: the low bits of the pointer have little or no entropy because of
// alignment. We shift the pointer to try to use higher entropy bits. A
// good number seems to be 12 bits, because that aligns with page size.
static uint64_t hashSeed(const int8_t* control)
{
  return reinterpret_cast<uintptr_t>(control) >> 12;
}

static uint64_t hash1(uint64_t hash, const int8_t* ctrl) { return (hash >> 7) ^ hashSeed(ctrl); }
static int8_t hash2(uint64_t hash) { return hash & 0x7F; }

struct GroupSse2Impl
{
  static constexpr size_t kWidth = 16; // the number of slots per group

  explicit GroupSse2Impl(const int8_t* pos)
  {
    ctrl = _mm_loadu_si128(reinterpret_cast<const __m128i*>(pos));
  }

  // Returns a bitmask representing the positions of slots that match hash.
  BitMask<uint32_t, kWidth> match(int8_t hash) const
  {
    auto match = _mm_set1_epi8(hash);
    return BitMask<uint32_t, kWidth>(_mm_movemask_epi8(_mm_cmpeq_epi8(match, ctrl)));
  }

  // Returns a bitmask representing the positions of empty slots.
  BitMask<uint32_t, kWidth> matchEmpty() const
  {
    return match(static_cast<int8_t>(kControlBitmaskEmpty));
  }

  // Returns a bitmask representing the positions of empty or deleted slots.
  BitMask<uint32_t, kWidth> matchEmptyOrDeleted() const
  {
    auto special = _mm_set1_epi8(kControlBitmaskSentinel);
    return BitMask<uint32_t, kWidth>(_mm_movemask_epi8(_mm_cmpgt_epi8(special, ctrl)));
  }

  // Returns the number of trailing empty or deleted elements in the group.
  uint32_t countLeadingEmptyOrDeleted() const
  {
    auto special = _mm_set1_epi8(kControlBitmaskSentinel);
    return trailingZerosU32(
        static_cast<uint32_t>(_mm_movemask_epi8(_mm_cmpgt_epi8(special, ctrl)) + 1));
  }

  void convertSpecialToEmptyAndFullToDeleted(int8_t* dst) const
  {
    auto msbs = _mm_set1_epi8(static_cast<char>(-128));
    auto x126 = _mm_set1_epi8(126);

    auto zero = _mm_setzero_si128();
    auto special_mask = _mm_cmpgt_epi8(zero, ctrl);
    auto res = _mm_or_si128(msbs, _mm_andnot_si128(special_mask, x126));

    _mm_storeu_si128(reinterpret_cast<__m128i*>(dst), res);
  }

  __m128i ctrl;
};

/// Capacity

//
static bool capacityIsValid(size_t n);

// Rounds up the capacity to the next power of 2 minus 1, with a minimum of 1.
static uint64_t capacityNormalize(uint64_t n);

// General notes on capacity/growth methods below:
// - We use 7/8th as maximum load factor. For 16-wide groups, that gives an
//   average of two empty slots per group.
// - For (capacity+1) >= Group::kWidth, growth is 7/8*capacity.
// - For (capacity+1) < Group::kWidth, growth == capacity. In this case, we
//   never need to probe (the whole table fits in one group) so we don't need a
//   load factor less than 1.

// Given `capacity` of the table, returns the size (i.e. number of full slots)
// at which we should grow the capacity.
// if ( Group::kWidth == 8 && capacity == 7 ) { return 6 }
// x-x/8 does not work when x==7.
static uint64_t capacityToGrowth(uint64_t capacity);
static uint64_t capacityGrowthToLowerBound(uint64_t growth);

static void convertDeletedToEmptyAndFullToDeleted(int8_t* ctrl, size_t capacity)
{
  // assert( ctrl[ capacity ] == k_control_bitmask_sentinel );
  // assert( IsValidCapacity( capacity ) );
  for (int8_t* pos = ctrl; pos != ctrl + capacity + 1; pos += GroupSse2Impl::kWidth)
  {
    GroupSse2Impl{pos}.convertSpecialToEmptyAndFullToDeleted(pos);
  }
  // Copy the cloned ctrl bytes.
  Framework::memoryCopy(ctrl + capacity + 1, ctrl, GroupSse2Impl::kWidth);
  ctrl[capacity] = kControlBitmaskSentinel;
}

// FlatHashMap
template <typename K, typename V> void FlatHashMap<K, V>::resetCtrl()
{
  memset(m_ControlBytes, kControlBitmaskEmpty, m_Capacity + GroupSse2Impl::kWidth);
  m_ControlBytes[m_Capacity] = kControlBitmaskSentinel;
  // SanitizerPoisonMemoryRegion( slots_, sizeof( slot_type ) * capacity_ );
}

template <typename K, typename V> void FlatHashMap<K, V>::resetGrowthLeft()
{
  m_GrowthLeft = capacityToGrowth(m_Capacity) - m_Size;
}

template <typename K, typename V> ProbeSequence FlatHashMap<K, V>::probe(uint64_t hash)
{
  return ProbeSequence(hash1(hash, m_ControlBytes), m_Capacity);
}

template <typename K, typename V>
inline void FlatHashMap<K, V>::init(Allocator* allocator_, uint64_t initial_capacity)
{
  m_Allocator = allocator_;
  m_Size = m_Capacity = m_GrowthLeft = 0;
  m_DefaultKeyValue = {(K)-1, (V)0};

  m_ControlBytes = groupInitEmpty();
  m_Slots = nullptr;
  reserve(initial_capacity < 4 ? 4 : initial_capacity);
}

template <typename K, typename V> inline void FlatHashMap<K, V>::shutdown()
{
  FRAMEWORK_FREE(m_ControlBytes, m_Allocator);
}

template <typename K, typename V> FlatHashMapIterator FlatHashMap<K, V>::find(const K& key)
{

  const uint64_t hash = hashCalculate(key);
  ProbeSequence sequence = probe(hash);

  while (true)
  {
    const GroupSse2Impl group{m_ControlBytes + sequence.getOffset()};
    const int8_t h2 = hash2(hash);
    for (int i : group.match(h2))
    {
      const KeyValue& key_value = *(m_Slots + sequence.getOffset(i));
      if (key_value.key == key)
        return {sequence.getOffset(i)};
    }

    if (group.matchEmpty())
    {
      break;
    }

    sequence.next();
  }

  return {g_IteratorEnd};
}

template <typename K, typename V> void FlatHashMap<K, V>::insert(const K& key, const V& value)
{
  const FindResult find_result = findOrPrepareInsert(key);
  if (find_result.freeIndex)
  {
    // Emplace
    m_Slots[find_result.index].key = key;
    m_Slots[find_result.index].value = value;
  }
  else
  {
    // Substitute value index
    m_Slots[find_result.index].value = value;
  }
}

template <typename K, typename V>
void FlatHashMap<K, V>::eraseMeta(const FlatHashMapIterator& iterator)
{
  --m_Size;

  const uint64_t index = iterator.m_Index;
  const uint64_t index_before = (index - GroupSse2Impl::kWidth) & m_Capacity;
  const auto empty_after = GroupSse2Impl(m_ControlBytes + index).matchEmpty();
  const auto empty_before = GroupSse2Impl(m_ControlBytes + index_before).matchEmpty();

  // We count how many consecutive non empties we have to the right and to the
  // left of `it`. If the sum is >= kWidth then there is at least one probe
  // window that might have seen a full group.
  const uint64_t trailing_zeros = empty_after.trailingZeros();
  const uint64_t leading_zeros = empty_before.leadingZeros();
  const uint64_t zeros = trailing_zeros + leading_zeros;
  // printf( "%x, %x", empty_after.TrailingZeros(), empty_before.LeadingZeros() );
  bool was_never_full = empty_before && empty_after;
  was_never_full = was_never_full && (zeros < GroupSse2Impl::kWidth);

  setCtrl(index, was_never_full ? kControlBitmaskEmpty : kControlBitmaskDeleted);
  m_GrowthLeft += was_never_full;
}

template <typename K, typename V> uint32_t FlatHashMap<K, V>::remove(const K& key)
{
  FlatHashMapIterator iterator = find(key);
  if (iterator.m_Index == g_IteratorEnd)
    return 0;

  eraseMeta(iterator);
  return 1;
}

template <typename K, typename V>
inline uint32_t FlatHashMap<K, V>::remove(const FlatHashMapIterator& iterator)
{
  if (iterator.m_Index == g_IteratorEnd)
    return 0;

  eraseMeta(iterator);
  return 1;
}

template <typename K, typename V> FindResult FlatHashMap<K, V>::findOrPrepareInsert(const K& key)
{
  uint64_t hash = hashCalculate(key);
  ProbeSequence sequence = probe(hash);

  while (true)
  {
    const GroupSse2Impl group{m_ControlBytes + sequence.getOffset()};
    for (int i : group.match(hash2(hash)))
    {
      const KeyValue& key_value = *(m_Slots + sequence.getOffset(i));
      if (key_value.key == key)
        return {sequence.getOffset(i), false};
    }

    if (group.matchEmpty())
    {
      break;
    }

    sequence.next();
  }
  return {prepareInsert(hash), true};
}

template <typename K, typename V> FindInfo FlatHashMap<K, V>::findFirstNonFull(uint64_t hash)
{
  ProbeSequence sequence = probe(hash);

  while (true)
  {
    const GroupSse2Impl group{m_ControlBytes + sequence.getOffset()};
    auto mask = group.matchEmptyOrDeleted();

    if (mask)
    {
      return {sequence.getOffset(mask.lowestBitSet()), sequence.getIndex()};
    }

    sequence.next();
  }

  return FindInfo();
}

template <typename K, typename V> uint64_t FlatHashMap<K, V>::prepareInsert(uint64_t hash)
{
  FindInfo find_info = findFirstNonFull(hash);
  if (m_GrowthLeft == 0 && !controlIsDeleted(m_ControlBytes[find_info.offset]))
  {
    rehashAndGrowIfNecessary();
    find_info = findFirstNonFull(hash);
  }
  ++m_Size;

  m_GrowthLeft -= controlIsEmpty(m_ControlBytes[find_info.offset]) ? 1 : 0;
  setCtrl(find_info.offset, hash2(hash));
  return find_info.offset;
}

template <typename K, typename V> void FlatHashMap<K, V>::rehashAndGrowIfNecessary()
{
  if (m_Capacity == 0)
  {
    resize(1);
  }
  else if (m_Size <= capacityToGrowth(m_Capacity) / 2)
  {
    // Squash DELETED without growing if there is enough capacity.
    dropDeletesWithoutResize();
  }
  else
  {
    // Otherwise grow the container.
    resize(m_Capacity * 2 + 1);
  }
}

template <typename K, typename V> void FlatHashMap<K, V>::dropDeletesWithoutResize()
{
  alignas(KeyValue) unsigned char raw[sizeof(KeyValue)];
  size_t total_probe_length = 0;
  KeyValue* slot = reinterpret_cast<KeyValue*>(&raw);
  for (size_t i = 0; i != m_Capacity; ++i)
  {
    if (!controlIsDeleted(m_ControlBytes[i]))
    {
      continue;
    }

    const KeyValue* current_slot = m_Slots + i;
    size_t hash = hashCalculate(current_slot->key);
    auto target = findFirstNonFull(hash);
    size_t new_i = target.offset;
    total_probe_length += target.probeLength;

    // Verify if the old and new i fall within the same group wrt the hash.
    // If they do, we don't need to move the object as it falls already in the
    // best probe we can.
    const auto probe_index = [&](size_t pos) {
      return ((pos - probe(hash).getOffset()) & m_Capacity) / GroupSse2Impl::kWidth;
    };

    // Element doesn't move.
    if ((probe_index(new_i) == probe_index(i)))
    {
      setCtrl(i, hash2(hash));
      continue;
    }
    if (controlIsEmpty(m_ControlBytes[new_i]))
    {
      // Transfer element to the empty spot.
      // set_ctrl poisons/unpoisons the slots so we have to call it at the
      // right time.
      setCtrl(new_i, hash2(hash));
      memcpy(m_Slots + new_i, m_Slots + i, sizeof(KeyValue));
      setCtrl(i, kControlBitmaskEmpty);
    }
    else
    {
      setCtrl(new_i, hash2(hash));
      // Until we are done rehashing, DELETED marks previously FULL slots.
      // Swap i and new_i elements.
      memcpy(slot, m_Slots + i, sizeof(KeyValue));
      memcpy(m_Slots + i, m_Slots + new_i, sizeof(KeyValue));
      memcpy(m_Slots + new_i, slot, sizeof(KeyValue));
      --i; // repeat
    }
  }

  resetGrowthLeft();
}

template <typename K, typename V> uint64_t FlatHashMap<K, V>::calculateSize(uint64_t p_NewCapacity)
{
  return (p_NewCapacity + GroupSse2Impl::kWidth + p_NewCapacity * (sizeof(KeyValue)));
}

template <typename K, typename V> void FlatHashMap<K, V>::initializeSlots()
{

  char* new_memory = (char*)FRAMEWORK_ALLOCA(calculateSize(m_Capacity), m_Allocator);

  m_ControlBytes = reinterpret_cast<int8_t*>(new_memory);
  m_Slots = reinterpret_cast<KeyValue*>(new_memory + m_Capacity + GroupSse2Impl::kWidth);

  resetCtrl();
  resetGrowthLeft();
}

template <typename K, typename V> void FlatHashMap<K, V>::resize(uint64_t p_NewCapacity)
{
  // assert( IsValidCapacity( p_NewCapacity ) );
  int8_t* oldControlBytes = m_ControlBytes;
  KeyValue* old_slots = m_Slots;
  const uint64_t old_capacity = m_Capacity;

  m_Capacity = p_NewCapacity;

  initializeSlots();

  size_t total_probe_length = 0;
  for (size_t i = 0; i != old_capacity; ++i)
  {
    if (controlIsFull(oldControlBytes[i]))
    {
      const KeyValue* old_value = old_slots + i;
      uint64_t hash = hashCalculate(old_value->key);

      FindInfo find_info = findFirstNonFull(hash);

      uint64_t new_i = find_info.offset;
      total_probe_length += find_info.probeLength;

      setCtrl(new_i, hash2(hash));

      Framework::memoryCopy(m_Slots + new_i, old_slots + i, sizeof(KeyValue));
    }
  }

  if (old_capacity)
  {
    FRAMEWORK_FREE(oldControlBytes, m_Allocator);
  }
}

// Sets the control byte, and if `i < Group::kWidth - 1`, set the cloned byte
// at the end too.
template <typename K, typename V> void FlatHashMap<K, V>::setCtrl(uint64_t i, int8_t h)
{
  /*assert( i < capacity_ );

  if ( IsFull( h ) ) {
      SanitizerUnpoisonObject( slots_ + i );
  } else {
      SanitizerPoisonObject( slots_ + i );
  }*/

  m_ControlBytes[i] = h;
  constexpr size_t kClonedBytes = GroupSse2Impl::kWidth - 1;
  m_ControlBytes[((i - kClonedBytes) & m_Capacity) + (kClonedBytes & m_Capacity)] = h;
}

template <typename K, typename V> V& FlatHashMap<K, V>::get(const K& key)
{
  FlatHashMapIterator iterator = find(key);
  if (iterator.m_Index != g_IteratorEnd)
    return m_Slots[iterator.m_Index].value;
  return m_DefaultKeyValue.value;
}

template <typename K, typename V> V& FlatHashMap<K, V>::get(const FlatHashMapIterator& iterator)
{
  if (iterator.m_Index != g_IteratorEnd)
    return m_Slots[iterator.m_Index].value;
  return m_DefaultKeyValue.value;
}

template <typename K, typename V>
typename FlatHashMap<K, V>::KeyValue& FlatHashMap<K, V>::getStructure(const K& key)
{
  FlatHashMapIterator iterator = find(key);
  if (iterator.m_Index != g_IteratorEnd)
    return m_Slots[iterator.m_Index];
  return m_DefaultKeyValue;
}

template <typename K, typename V>
typename FlatHashMap<K, V>::KeyValue&
FlatHashMap<K, V>::getStructure(const FlatHashMapIterator& iterator)
{
  return m_Slots[iterator.m_Index];
}

template <typename K, typename V> inline void FlatHashMap<K, V>::setDefaultValue(const V& value)
{
  m_DefaultKeyValue.value = value;
}

template <typename K, typename V> FlatHashMapIterator FlatHashMap<K, V>::iteratorBegin()
{
  FlatHashMapIterator it{0};

  iteratorSkipEmptyOrDeleted(it);

  return it;
}

template <typename K, typename V>
void FlatHashMap<K, V>::iteratorAdvance(FlatHashMapIterator& iterator)
{

  iterator.m_Index++;

  iteratorSkipEmptyOrDeleted(iterator);
}

template <typename K, typename V>
inline void FlatHashMap<K, V>::iteratorSkipEmptyOrDeleted(FlatHashMapIterator& it)
{
  int8_t* ctrl = m_ControlBytes + it.m_Index;

  while (controlIsEmptyOrDeleted(*ctrl))
  {
    uint32_t shift = GroupSse2Impl{ctrl}.countLeadingEmptyOrDeleted();
    ctrl += shift;
    it.m_Index += shift;
  }
  if (*ctrl == kControlBitmaskSentinel)
    it.m_Index = g_IteratorEnd;
}

template <typename K, typename V> inline void FlatHashMap<K, V>::clear()
{
  m_Size = 0;
  resetCtrl();
  resetGrowthLeft();
}

template <typename K, typename V> inline void FlatHashMap<K, V>::reserve(uint64_t p_NewSize)
{
  if (p_NewSize > m_Size + m_GrowthLeft)
  {
    size_t m = capacityGrowthToLowerBound(p_NewSize);
    resize(capacityNormalize(m));
  }
}

// Capacity
bool capacityIsValid(size_t n) { return ((n + 1) & n) == 0 && n > 0; }

inline uint64_t lzcnt_soft(uint64_t n)
{
  // NOTE: the __lzcnt intrisics require at least haswell
#if defined(_MSC_VER)
  unsigned long index = 0;
  _BitScanReverse64(&index, n);
  uint64_t cnt = index ^ 63;
#else
  uint64_t cnt = __builtin_clzl(n);
#endif
  return cnt;
}

// Rounds up the capacity to the next power of 2 minus 1, with a minimum of 1.
uint64_t capacityNormalize(uint64_t n) { return n ? ~uint64_t{} >> lzcnt_soft(n) : 1; }

//
uint64_t capacityToGrowth(uint64_t capacity) { return capacity - capacity / 8; }

//
uint64_t capacityGrowthToLowerBound(uint64_t growth)
{
  return growth + static_cast<uint64_t>((static_cast<int64_t>(growth) - 1) / 7);
}

// Grouping: implementation
inline int8_t* groupInitEmpty()
{
  alignas(16) static constexpr int8_t empty_group[] = {
      kControlBitmaskSentinel,
      kControlBitmaskEmpty,
      kControlBitmaskEmpty,
      kControlBitmaskEmpty,
      kControlBitmaskEmpty,
      kControlBitmaskEmpty,
      kControlBitmaskEmpty,
      kControlBitmaskEmpty,
      kControlBitmaskEmpty,
      kControlBitmaskEmpty,
      kControlBitmaskEmpty,
      kControlBitmaskEmpty,
      kControlBitmaskEmpty,
      kControlBitmaskEmpty,
      kControlBitmaskEmpty,
      kControlBitmaskEmpty};
  return const_cast<int8_t*>(empty_group);
}

/// Probing: impl
inline ProbeSequence::ProbeSequence(uint64_t p_Hash, uint64_t p_Mask)
{
  // assert( ( ( p_Mask + 1 ) & p_Mask ) == 0 && "not a mask" );
  m_Mask = p_Mask;
  m_Offset = p_Hash & p_Mask;
}

inline uint64_t ProbeSequence::getOffset() const { return m_Offset; }

inline uint64_t ProbeSequence::getOffset(uint64_t i) const { return (m_Offset + i) & m_Mask; }

inline uint64_t ProbeSequence::getIndex() const { return m_Index; }

inline void ProbeSequence::next()
{
  m_Index += kWidth;
  m_Offset += m_Index;
  m_Offset &= m_Mask;
}

} // namespace Framework
