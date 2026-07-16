#include "msys_shell_native/system_metrics.h"

#include <stdio.h>
#include <string.h>

#define PROC_STAT_PATH "/proc/stat"
#define PROC_MEMINFO_PATH "/proc/meminfo"
#define PROC_SAMPLE_CAPACITY 4096u

static int read_bounded_file(
    const char *path,
    char *buffer,
    size_t capacity
)
{
    FILE *stream;
    size_t length;
    if (path == NULL || buffer == NULL || capacity < 2u) return 0;
    stream = fopen(path, "r");
    if (stream == NULL) return 0;
    length = fread(buffer, 1u, capacity - 1u, stream);
    if (ferror(stream) != 0) {
        (void)fclose(stream);
        return 0;
    }
    if (fclose(stream) != 0) return 0;
    buffer[length] = '\0';
    return 1;
}

void msys_native_system_metrics_init(msys_native_system_metrics *metrics)
{
    if (metrics == NULL) return;
    memset(metrics, 0, sizeof(*metrics));
}

int msys_native_system_metrics_parse_cpu(
    msys_native_system_metrics *metrics,
    const char *stat_text
)
{
    unsigned long long user;
    unsigned long long nice;
    unsigned long long system;
    unsigned long long idle;
    unsigned long long iowait;
    unsigned long long irq;
    unsigned long long softirq;
    unsigned long long steal;
    uint64_t total;
    uint64_t idle_total;
    uint64_t delta_total;
    uint64_t delta_idle;
    if (metrics == NULL || stat_text == NULL) return 0;
    if (sscanf(
            stat_text,
            "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
            &user,
            &nice,
            &system,
            &idle,
            &iowait,
            &irq,
            &softirq,
            &steal
        ) != 8) return 0;
    total = (uint64_t)user + (uint64_t)nice + (uint64_t)system +
        (uint64_t)idle + (uint64_t)iowait + (uint64_t)irq +
        (uint64_t)softirq + (uint64_t)steal;
    idle_total = (uint64_t)idle + (uint64_t)iowait;
    if (metrics->cpu_baseline_available != 0 &&
        total >= metrics->previous_cpu_total &&
        idle_total >= metrics->previous_cpu_idle) {
        delta_total = total - metrics->previous_cpu_total;
        delta_idle = idle_total - metrics->previous_cpu_idle;
        if (delta_total > 0u && delta_idle <= delta_total) {
            metrics->cpu_percent = (unsigned int)(
                ((delta_total - delta_idle) * 100u + delta_total / 2u) /
                delta_total
            );
            if (metrics->cpu_percent > 100u) metrics->cpu_percent = 100u;
            metrics->cpu_available = 1;
        }
    }
    metrics->previous_cpu_total = total;
    metrics->previous_cpu_idle = idle_total;
    metrics->cpu_baseline_available = 1;
    return 1;
}

static int meminfo_value(
    const char *text,
    const char *key,
    uint64_t *value
)
{
    const char *line = text;
    size_t key_length = strlen(key);
    while (line != NULL && *line != '\0') {
        const char *next = strchr(line, '\n');
        if (strncmp(line, key, key_length) == 0 && line[key_length] == ':') {
            unsigned long long parsed;
            if (sscanf(line + key_length + 1u, " %llu kB", &parsed) == 1) {
                *value = (uint64_t)parsed;
                return 1;
            }
            return 0;
        }
        line = next == NULL ? NULL : next + 1;
    }
    return 0;
}

int msys_native_system_metrics_parse_memory(
    msys_native_system_metrics *metrics,
    const char *meminfo_text
)
{
    uint64_t total;
    uint64_t available;
    uint64_t used;
    if (metrics == NULL || meminfo_text == NULL ||
        !meminfo_value(meminfo_text, "MemTotal", &total) || total == 0u ||
        !meminfo_value(meminfo_text, "MemAvailable", &available)) {
        return 0;
    }
    if (available > total) available = total;
    used = total - available;
    metrics->memory_total_kib = total;
    metrics->memory_available_kib = available;
    metrics->memory_percent = (unsigned int)(
        (used * 100u + total / 2u) / total
    );
    if (metrics->memory_percent > 100u) metrics->memory_percent = 100u;
    metrics->memory_available = 1;
    return 1;
}

int msys_native_system_metrics_sample(msys_native_system_metrics *metrics)
{
    char buffer[PROC_SAMPLE_CAPACITY];
    int sampled = 0;
    if (metrics == NULL) return 0;
    if (read_bounded_file(PROC_STAT_PATH, buffer, sizeof(buffer)) != 0) {
        sampled |= msys_native_system_metrics_parse_cpu(metrics, buffer);
    }
    if (read_bounded_file(PROC_MEMINFO_PATH, buffer, sizeof(buffer)) != 0) {
        sampled |= msys_native_system_metrics_parse_memory(metrics, buffer);
    }
    return sampled != 0;
}

int msys_native_system_metrics_format(
    const msys_native_system_metrics *metrics,
    const char *cpu_label,
    const char *memory_label,
    char *output,
    size_t capacity
)
{
    char cpu[8];
    char memory[8];
    int written;
    if (metrics == NULL || cpu_label == NULL || memory_label == NULL ||
        output == NULL || capacity == 0u) return 0;
    if (metrics->cpu_available != 0) {
        (void)snprintf(cpu, sizeof(cpu), "%u%%", metrics->cpu_percent);
    } else {
        (void)snprintf(cpu, sizeof(cpu), "--");
    }
    if (metrics->memory_available != 0) {
        (void)snprintf(memory, sizeof(memory), "%u%%", metrics->memory_percent);
    } else {
        (void)snprintf(memory, sizeof(memory), "--");
    }
    written = snprintf(
        output,
        capacity,
        "%s%s %s%s",
        cpu_label,
        cpu,
        memory_label,
        memory
    );
    return written >= 0 && (size_t)written < capacity;
}
