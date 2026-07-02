#ifndef VEGOSH_H
#define VEGOSH_H

#include <cstddef>
#include <cstdint>
#include "rapidhash.h"

constexpr uint8_t KEY_LEN = 16;
constexpr uint8_t VALUE_LEN = 32;
constexpr uint32_t TABLE_SIZE = 1<<21;
constexpr uint32_t MASK = TABLE_SIZE - 1;
constexpr uint32_t MAX_KEYS = 1000000;
constexpr uint8_t EMPTY = 0x00;
constexpr uint8_t OCCUPIED = 0x01;

struct alignas(64) Slot {
    uint8_t key[KEY_LEN];
    uint8_t value[VALUE_LEN];
    uint64_t hash;
    uint8_t value_len;
    uint8_t status;
    uint16_t probe_distance;
    uint8_t padding[4];
};

struct Vegosh {
    Slot slots[TABLE_SIZE];
    uint32_t count;
};

static_assert(sizeof(Slot) == 64, "Slot must be 64 bytes");
static_assert(alignof(Slot) == 64, "Slot must be 64 byte aligned");

inline uint64_t hash_key(const uint8_t *key) {
    return rapidhash(key, KEY_LEN);
}

// init() no longer allocates or returns anything — the caller already owns
// the memory (typically a static Vegosh instance). This just resets it.
void init(Vegosh *table);
int insert(Vegosh *table, const uint8_t *key, const uint8_t *value, uint8_t value_len);
int get(Vegosh *table, const uint8_t *key, uint8_t *out_value, uint8_t *out_value_len);
int delete_key(Vegosh *table, const uint8_t *key);
uint32_t size(Vegosh *table);
void clear(Vegosh *table);

#endif // VEGOSH_H
