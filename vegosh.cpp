
#include <assert.h>
#include "vegosh.hpp"
// Resets the global table to empty and returns a pointer to it.
// Call once at startup before using insert/get/delete_key.
void init(Vegosh *table) {
    assert(table != nullptr);
    table->count = 0;
    memset(table->slots, 0, sizeof(Slot) * TABLE_SIZE);
}

// Inserts a key/value pair, or updates the value if the key already exists.
// Uses Robin Hood hashing with linear probing: when probing finds a slot
// that's "richer" (closer to its ideal index) than the incoming entry,
// the two swap places, so no single key ever ends up displaced too far.
//
// Returns 0 on fresh insert, 1 on update (key already existed), -1 if the
// table is full.
int insert(Vegosh *table, const uint8_t *key, const uint8_t *value, uint8_t value_len) {

    assert(table != nullptr);
    assert(key != nullptr);
    assert(value != nullptr);
    assert(value_len <= VALUE_LEN);

    if (table->count >= MAX_KEYS) return -1;

    uint64_t hash = hash_key(key);
    uint32_t index = hash & MASK; // ideal slot for this key, before any probing

    // Build the entry we're trying to place. This may get swapped out for
    // a "poorer" entry mid-probe (see the Robin Hood swap below), so it's
    // a mutable local, not written directly into the table yet.
    Slot incoming = {};
        memcpy(incoming.key, key, KEY_LEN);
        memcpy(incoming.value, value, VALUE_LEN);
        incoming.hash           = hash;
        incoming.value_len      = value_len;
        incoming.status         = OCCUPIED;
        incoming.probe_distance = 0;

    while (true) {
        // Prefetch two slots ahead so the next probe iteration's cache line
        // is already in flight by the time we need it.
        __builtin_prefetch(&table->slots[(index + 2) & MASK], 1, 3);
        Slot *slot = &table->slots[index];

        // Found an empty slot: place the entry here and we're done.
        if (slot->status == EMPTY) {
            *slot = incoming;
            table->count++;
            return 0;
        }

        // Check hash first (cheap), only fall through to the full key
        // comparison (memcmp) if the hash actually matches.
        if (slot->hash == incoming.hash) {
            if (memcmp(slot->key, incoming.key, KEY_LEN) == 0) {
                // Same key already exists: update its value in place.
                memcpy(slot->value, incoming.value, VALUE_LEN);
                slot->value_len = incoming.value_len;
                return 1;
            }
        }

        // Robin Hood swap: if the entry we're carrying has traveled further
        // from its ideal slot than the one currently occupying this slot,
        // steal this slot and carry the displaced entry onward instead.
        // This keeps probe distances balanced across the whole table.
        if (incoming.probe_distance > slot->probe_distance) {
            Slot tmp = *slot;
            *slot = incoming;
            incoming = tmp;
        }

        index = (index + 1) & MASK;
        incoming.probe_distance++;
    }
}

// Looks up a key and copies its value into out_value / out_value_len.
// Returns 0 on success, -1 if the key isn't present.
//
// The probe_distance early-exit below is what makes Robin Hood lookups
// fast: if we've probed further than the slot in front of us has ever
// been displaced, the key we're looking for cannot exist anywhere later
// in the probe sequence, so we can stop instead of scanning the whole table.
int get(Vegosh *table, const uint8_t *key, uint8_t *out_value, uint8_t *out_value_len) {

    assert(table != nullptr);
    assert(key != nullptr);
    assert(out_value != nullptr);
    assert(out_value_len != nullptr);
    uint64_t hash = hash_key(key);
    uint32_t index = hash & MASK;
    uint16_t probe_distance = 0;

    while (true) {
        __builtin_prefetch(&table->slots[(index + 2) & MASK], 0, 3);
        Slot *slot = &table->slots[index];

        // Hit an empty slot before finding the key: it's not in the table.
        if (slot->status == EMPTY) return -1;

        if (slot->hash == hash) {
            if (memcmp(slot->key, key, KEY_LEN) == 0) {
                // Found it — copy out the value and how much of it is valid.
                memcpy(out_value, slot->value, VALUE_LEN);
                *out_value_len = slot->value_len;
                return 0;
            }
        }

        // Robin Hood early-exit: this slot is "richer" than we've traveled,
        // so our key can't be further down the probe sequence.
        if (probe_distance > slot->probe_distance) return -1;

        index = (index + 1) & MASK;
        probe_distance++;
    }
}

// Removes a key from the table using backward-shift deletion, which keeps
// the Robin Hood probe-distance invariant intact without needing tombstones.
//
// Phase 1: probe forward to find the slot holding this key (same logic as get()).
// Phase 2: shift each subsequent slot backward by one, decrementing its
// probe_distance, until we hit an empty slot or a slot that's already at
// its ideal position (probe_distance == 0) — that's where the "hole" closes.
//
// Returns 0 on success, -1 if the key isn't present.
int delete_key(Vegosh *table, const uint8_t *key) {

    assert(table != nullptr);
    assert(key != nullptr);

    uint64_t hash = hash_key(key);
    uint32_t index = hash & MASK;
    uint16_t probe_distance = 0;

    // Phase 1: locate the slot holding this key.
    while (true) {
        __builtin_prefetch(&table->slots[(index + 2) & MASK], 1, 3);
        Slot *slot = &table->slots[index];

        if (slot->status == EMPTY) return -1;

        if (slot->hash == hash) {
            if (memcmp(slot->key, key, KEY_LEN) == 0) break; // found it, stop probing
        }

        if (slot->probe_distance < probe_distance) return -1; // Robin Hood early-exit

        index = (index + 1) & MASK;
        probe_distance++;
    }

    // Phase 2: backward-shift every following displaced entry into the gap
    // we just opened, one slot at a time, until the chain naturally ends.
    while (true) {
        uint32_t next = (index + 1) & MASK;
        Slot *next_slot = &table->slots[next];

        // Next slot is empty, or already at its ideal position: nothing more
        // to shift, so this is where the gap closes. Clear it and stop.
        if (next_slot->status == EMPTY || next_slot->probe_distance == 0) {
            memset(&table->slots[index], 0, sizeof(Slot));
            break;
        }

        // Pull the next entry back one slot and reduce its displacement by one.
        table->slots[index] = *next_slot;
        table->slots[index].probe_distance--;
        index = next;
    }

    table->count--;
    return 0;
}

// Returns the current number of occupied entries in the table.
uint32_t size(Vegosh *table) {
    assert(table != nullptr);
    return table->count;
}

// Wipes every slot back to EMPTY and resets the entry count to zero.
// Table remains usable afterward — this doesn't free or reallocate anything.
void clear(Vegosh *table) {
    assert(table != nullptr);
    memset(table->slots, 0, sizeof(Slot) * TABLE_SIZE);
    table->count = 0;
}
