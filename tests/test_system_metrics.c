#include "msys_shell_native/system_metrics.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, #expression); \
        return 1; \
    } \
} while (0)

int main(void)
{
    msys_native_system_metrics metrics;
    char text[64];
    msys_native_system_metrics_init(&metrics);
    CHECK(msys_native_system_metrics_parse_cpu(
        &metrics, "cpu  100 20 30 800 10 5 5 0 12 4\n"
    ));
    CHECK(!metrics.cpu_available);
    CHECK(msys_native_system_metrics_parse_cpu(
        &metrics, "cpu  130 20 40 850 10 5 5 0 15 4\n"
    ));
    CHECK(metrics.cpu_available);
    CHECK(metrics.cpu_percent == 44u);
    CHECK(!msys_native_system_metrics_parse_cpu(&metrics, "cpu bad\n"));
    CHECK(msys_native_system_metrics_parse_memory(
        &metrics,
        "MemTotal:        1000 kB\nMemFree:          50 kB\n"
        "MemAvailable:     375 kB\nBuffers:           10 kB\n"
    ));
    CHECK(metrics.memory_available);
    CHECK(metrics.memory_total_kib == 1000u);
    CHECK(metrics.memory_available_kib == 375u);
    CHECK(metrics.memory_percent == 63u);
    CHECK(!msys_native_system_metrics_parse_memory(
        &metrics, "MemTotal: 1000 kB\n"
    ));
    CHECK(msys_native_system_metrics_format(
        &metrics, "CPU", "MEM", text, sizeof(text)
    ));
    CHECK(strcmp(text, "CPU 44%  MEM 63%") == 0);
    metrics.cpu_available = 0;
    metrics.memory_available = 0;
    CHECK(msys_native_system_metrics_format(
        &metrics, "CPU", "MEM", text, sizeof(text)
    ));
    CHECK(strcmp(text, "CPU --  MEM --") == 0);
    CHECK(msys_native_system_metrics_sample(&metrics));
    puts("native shell system metrics tests passed");
    return 0;
}
