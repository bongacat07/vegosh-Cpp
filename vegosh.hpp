#include <cstdint>
#include "rapidhash.h"
#include <assert.h>
// Fixed sizes for keys and values. Every key is exactly KEY_LEN bytes,
// every value buffer is exactly VALUE_LEN bytes (callers pad as needed).
// value_len stored per slot records how many of those bytes are meaningful.
constexpr uint8_t KEY_LEN = 16;
constexpr uint8_t VALUE_LEN = 32;

// Table capacity. MAX_KEYS is the max number of entries we promise to hold.
// TABLE_SIZE is the next power of two above MAX_KEYS, giving a ~47.6% max
// load factor, which keeps Robin Hood probe sequences short in practice.
constexpr uint32_t TABLE_SIZE = 1<<21; //Since the max key limit is 1_000_000. The closest 2 power bit mask is 1<<21
constexpr uint32_t MASK = TABLE_SIZE-1 ; //bit mask, used instead of % for fast index wraparound
constexpr uint32_t MAX_KEYS = 1000000;

// Slot occupancy states.
constexpr uint8_t EMPTY = 0x00;
constexpr uint8_t OCCUPIED = 0X01;

// A single hash table slot, sized and aligned to exactly one cache line (64 bytes).
// This means each lookup touches only one cache line for the slot's metadata,
// which matters a lot for probing performance.
struct alignas(64)Slot {
    uint8_t key[KEY_LEN];          // full key, always KEY_LEN bytes
    uint8_t value[VALUE_LEN];      // full value buffer, always VALUE_LEN bytes
    uint64_t hash;                 // cached hash of key, avoids recomputing during probing
    uint8_t value_len;             // how many bytes of `value` are actually meaningful
    uint8_t status;                // EMPTY or OCCUPIED
    uint16_t probe_distance;       // Robin Hood displacement: how far this slot is from its ideal index
    uint8_t padding[4];            // pad out to 64 bytes total

};

// The hashmap itself: a flat array of slots plus a running count of occupied entries.
struct Vegosh {
    Slot slots[TABLE_SIZE];
    uint32_t count;
};
