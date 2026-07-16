#include "msys_shell_native/clock.h"

#include <string.h>

unsigned int msys_native_clock_changed_slots(
    const char *previous,
    const char *current
)
{
    unsigned int changed = 0u;
    size_t index;
    if (!current || strlen(current) != MSYS_NATIVE_CLOCK_SLOT_COUNT) return 0u;
    if (!previous || strlen(previous) != MSYS_NATIVE_CLOCK_SLOT_COUNT) {
        return MSYS_NATIVE_CLOCK_DIGIT_MASK;
    }
    for (index = 0u; index < MSYS_NATIVE_CLOCK_SLOT_COUNT; index++) {
        unsigned int bit = 1u << index;
        if (
            (MSYS_NATIVE_CLOCK_DIGIT_MASK & bit) != 0u &&
            previous[index] != current[index]
        ) changed |= bit;
    }
    return changed;
}

int msys_native_clock_slot_bounds(
    int clock_x,
    int clock_width,
    size_t slot,
    int *slot_x,
    int *slot_width
)
{
    int left;
    int right;
    if (
        clock_width < 1 || slot >= MSYS_NATIVE_CLOCK_SLOT_COUNT ||
        !slot_x || !slot_width
    ) return 0;
    left = clock_width * (int)slot / (int)MSYS_NATIVE_CLOCK_SLOT_COUNT;
    right = clock_width * ((int)slot + 1) /
        (int)MSYS_NATIVE_CLOCK_SLOT_COUNT;
    if (right <= left) return 0;
    *slot_x = clock_x + left;
    *slot_width = right - left;
    return 1;
}

unsigned int msys_native_clock_slot_count(unsigned int mask)
{
    unsigned int count = 0u;
    mask &= MSYS_NATIVE_CLOCK_DIGIT_MASK;
    while (mask != 0u) {
        count += mask & 1u;
        mask >>= 1u;
    }
    return count;
}
