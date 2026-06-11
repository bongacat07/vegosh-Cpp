#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>
#include "rapidhash.h"
constexpr size_t TABLE_SIZE = 1<<21;
constexpr size_t MASK = TABLE_SIZE-1 ;
constexpr size_t MAX_KEYS = 1000000;
constexpr uint8_t EMPTY = 0x00;
constexpr uint8_t OCCUPIED = 0X01;
struct alignas(64)Slot {
    uint8_t key[16];
    uint8_t value[32];
    uint64_t hash;
    uint8_t value_len;
    uint8_t status;
    uint16_t probe_distance;
    uint8_t padding[4];

};
struct Vegosh {
    Slot slots [TABLE_SIZE];
    size_t count;
};
static_assert(sizeof(Slot) == 64, "Slot must be 64 bytes");
static_assert(alignof(Slot) == 64, "Slot must be 64 byte aligned");


Vegosh* init(){
    Vegosh *table = new Vegosh();
    table->count = 0;
    memset(table->slots, 0, sizeof(Slot) * TABLE_SIZE);
    return table;
}
inline uint64_t hash_key(const uint8_t *key) {
    return rapidhash(key, 16);
}

int insert(Vegosh *table, const uint8_t *key, const uint8_t *value, uint8_t value_len) {
    if (table->count >= MAX_KEYS) return -1;

    uint64_t hash = hash_key(key);
    size_t index = hash & MASK;

    uint8_t v[32] = {};
    uint8_t n = value_len < 32 ? value_len : 32;
    memcpy(v, value, n);
    __builtin_prefetch(&table[index + 2], 1, 3);
    Slot incoming = {};
    memcpy(incoming.key, key, 16);
    memcpy(incoming.value, v, 32);
    incoming.hash           = hash;
    incoming.value_len      = value_len;
    incoming.status         = OCCUPIED;
    incoming.probe_distance = 0;

    while (true) {
        __builtin_prefetch(&table->slots[index + 2], 1, 3);
        Slot *slot = &table->slots[index];

        if (slot->status == EMPTY) {
            *slot = incoming;
            table->count++;
            return 0;
        }

        if (slot->hash == incoming.hash && memcmp(slot->key, incoming.key, 16) == 0) {
            memcpy(slot->value, incoming.value, 32);
            slot->value_len = incoming.value_len;
            return 1;
        }

        if (incoming.probe_distance > slot->probe_distance) {
            Slot tmp = *slot;
            *slot = incoming;
            incoming = tmp;
        }

        index = (index + 1) & MASK;
        incoming.probe_distance++;
    }
}

int get(Vegosh *table, const uint8_t *key, uint8_t *out_value, uint8_t *out_value_len) {
    uint64_t hash = hash_key(key);
    size_t index = hash & MASK;
    uint16_t dist = 0;

    while (true) {
        __builtin_prefetch(&table->slots[index + 2], 0, 3);
        Slot *slot = &table->slots[index];

        if (slot->status == EMPTY) return -1;

        if (slot->hash == hash && memcmp(slot->key, key, 16) == 0) {
            memcpy(out_value, slot->value, 32);
            *out_value_len = slot->value_len;
            return 0;
        }

        if (dist > slot->probe_distance) return -1;

        index = (index + 1) & MASK;
        dist++;
    }
}

int delete_key(Vegosh *table, const uint8_t *key) {
    uint64_t hash = hash_key(key);
    size_t index = hash & MASK;
    uint16_t dist = 0;

    while (true) {
        __builtin_prefetch(&table->slots[index + 2], 1, 3);
        Slot *slot = &table->slots[index];

        if (slot->status == EMPTY) return -1;
        if (slot->hash == hash && memcmp(slot->key, key, 16) == 0) break;
        if (slot->probe_distance < dist) return -1;

        index = (index + 1) & MASK;
        dist++;
    }

    while (true) {
        size_t next = (index + 1) & MASK;
        Slot *next_slot = &table->slots[next];

        if (next_slot->status == EMPTY || next_slot->probe_distance == 0) {
            memset(&table->slots[index], 0, sizeof(Slot));
            break;
        }

        table->slots[index] = *next_slot;
        table->slots[index].probe_distance--;
        index = next;
    }

    table->count--;
    return 0;
}

size_t size(Vegosh *table) {
    return table->count;
}

void clear(Vegosh *table) {
    memset(table->slots, 0, sizeof(Slot) * TABLE_SIZE);
    table->count = 0;
}


int main() {
    std::cout << "Hello, World!" << std::endl;
    std::vector<uint8_t> raw_bytes = {
        0x12, 0x34, 0x56, 0x78,
        0x99, 0xAA, 0xBB, 0xCC,
        0xDD, 0xEE, 0xFF, 0x11,
        0x22, 0x33, 0x44, 0x55
    };
    uint64_t hash = hash_key(raw_bytes.data());
    std::cout<<"Hash: "<<hash<<std::endl;
    return 0;
}
