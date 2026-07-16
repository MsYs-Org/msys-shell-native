#ifndef MSYS_SHELL_NATIVE_CLOCK_H
#define MSYS_SHELL_NATIVE_CLOCK_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MSYS_NATIVE_CLOCK_SLOT_COUNT 8u
#define MSYS_NATIVE_CLOCK_TEXT_CAPACITY 9u
#define MSYS_NATIVE_CLOCK_DIGIT_MASK 0xdbu

/* Returns one bit per changed numeric HH:MM:SS slot. Colon bits are never set. */
unsigned int msys_native_clock_changed_slots(
    const char *previous,
    const char *current
);

/* Partitions one fixed clock box into stable character slots. */
int msys_native_clock_slot_bounds(
    int clock_x,
    int clock_width,
    size_t slot,
    int *slot_x,
    int *slot_width
);

unsigned int msys_native_clock_slot_count(unsigned int mask);

#ifdef __cplusplus
}
#endif

#endif
