#ifndef MSYS_SHELL_NATIVE_SYSTEM_METRICS_H
#define MSYS_SHELL_NATIVE_SYSTEM_METRICS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct msys_native_system_metrics {
    uint64_t previous_cpu_total;
    uint64_t previous_cpu_idle;
    uint64_t memory_total_kib;
    uint64_t memory_available_kib;
    unsigned int cpu_percent;
    unsigned int memory_percent;
    int cpu_baseline_available;
    int cpu_available;
    int memory_available;
} msys_native_system_metrics;

void msys_native_system_metrics_init(msys_native_system_metrics *metrics);

/* Testable parsers used by the fixed-size /proc sampler. */
int msys_native_system_metrics_parse_cpu(
    msys_native_system_metrics *metrics,
    const char *stat_text
);

int msys_native_system_metrics_parse_memory(
    msys_native_system_metrics *metrics,
    const char *meminfo_text
);

/* Read /proc/stat and /proc/meminfo without allocation or helper processes. */
int msys_native_system_metrics_sample(msys_native_system_metrics *metrics);

/* Format a bounded localized two-value summary for the Overview header. */
int msys_native_system_metrics_format(
    const msys_native_system_metrics *metrics,
    const char *cpu_label,
    const char *memory_label,
    char *output,
    size_t capacity
);

#ifdef __cplusplus
}
#endif

#endif
