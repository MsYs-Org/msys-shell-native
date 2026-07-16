#include "msys_shell_native/clock.h"

#include <stdio.h>

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, #expression); \
        return 1; \
    } \
} while (0)

int main(void)
{
    int x;
    int width;
    size_t slot;
    int previous_right = 9;

    CHECK(msys_native_clock_changed_slots("12:34:56", "12:34:57") == (1u << 7));
    CHECK(msys_native_clock_changed_slots("12:34:59", "12:35:00") ==
        ((1u << 4) | (1u << 6) | (1u << 7)));
    CHECK(msys_native_clock_changed_slots("12:59:59", "13:00:00") ==
        ((1u << 1) | (1u << 3) | (1u << 4) | (1u << 6) | (1u << 7)));
    CHECK(msys_native_clock_changed_slots("12-34-56", "12:34:56") == 0u);
    CHECK(msys_native_clock_changed_slots(NULL, "12:34:56") ==
        MSYS_NATIVE_CLOCK_DIGIT_MASK);
    CHECK(msys_native_clock_changed_slots("short", "12:34:56") ==
        MSYS_NATIVE_CLOCK_DIGIT_MASK);
    CHECK(msys_native_clock_changed_slots("12:34:56", "bad") == 0u);
    CHECK(msys_native_clock_slot_count(MSYS_NATIVE_CLOCK_DIGIT_MASK) == 6u);
    CHECK(msys_native_clock_slot_count((1u << 2) | (1u << 7)) == 1u);

    for (slot = 0u; slot < MSYS_NATIVE_CLOCK_SLOT_COUNT; slot++) {
        CHECK(msys_native_clock_slot_bounds(9, 112, slot, &x, &width));
        CHECK(x == previous_right);
        CHECK(width == 14);
        previous_right = x + width;
    }
    CHECK(previous_right == 121);
    CHECK(!msys_native_clock_slot_bounds(0, 112, 8u, &x, &width));
    CHECK(!msys_native_clock_slot_bounds(0, 0, 0u, &x, &width));
    puts("test_clock: ok");
    return 0;
}
