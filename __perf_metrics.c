#define _GNU_SOURCE

#include "__perf_metrics.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/perf_event.h>
#include <sys/syscall.h>

static FILE* perf_file = NULL;

/*
perf_event_open
- делаем системный вызов, который создаёт аппратный счетчик нужной нам метрики 

Возвращает дескриптор файла (fd), который используется для: 
- запуска счетчика ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
- остановки счетчика ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);
- чтения его значения read(fd, &value, sizeof(value));

Аргументы:
perf_event_attr* attr - описывает событие, которое измеряем
pid - для какого процесса
cpu - на каком cpu (cpu = -1 означает, что измеряем независимо от ядра)
group_fd - группировка событий, чтобы собирать метрики вместе (созда1м лидера как fd_group = -1, остальные fd_group = ctx->leader)
flags - флаги, но я их не использую, у меня все 0
*/
static long perf_event_open(struct perf_event_attr *hw_event, pid_t pid, int cpu, int group_fd, unsigned long flags)
{
    return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}


/*
Создание перф-ивентов для кешей CPU

Аргументы:
perf_event_attr* attr - описывает событие, которое измеряем
cache - какой кеш: 
PERF_COUNT_HW_CACHE_L1D
PERF_COUNT_HW_CACHE_L1I
PERF_COUNT_HW_CACHE_L2
PERF_COUNT_HW_CACHE_LL
PERF_COUNT_HW_CACHE_DTLB
PERF_COUNT_HW_CACHE_ITLB
op - какая операция:
PERF_COUNT_HW_CACHE_OP_READ
PERF_COUNT_HW_CACHE_OP_WRITE
PERF_COUNT_HW_CACHE_OP_PREFETCH
result:
PERF_COUNT_HW_CACHE_RESULT_ACCESS
PERF_COUNT_HW_CACHE_RESULT_MISS
*/
static int add_cache_event(struct perf_event_attr* pe, int cache, int op, int result, int group_fd)
{
    pe->type = PERF_TYPE_HW_CACHE;

    // cache, op и result должны быть указаны одним числом
    pe->config = cache | (op << 8) | (result << 16);

    return perf_event_open(pe, 0, -1, group_fd, 0);
}



// Инициализация всех счетчиков, выполняется один раз в начале.
static void perf_init_ctx(perf_ctx* ctx)
{
    struct perf_event_attr pe; // Структура, описывающая событие CPU
    memset(&pe, 0, sizeof(pe)); // Обнуление структуры, иначе возникают ошибки
    pe.size = sizeof(pe); // Нужно, т.к. в разных версиях линукс эта структура может меняться в размере
    pe.disabled = 1; // Создаём счётчики выключенными
    pe.exclude_kernel = 1; // Это и сл. нужно, чтобы считать только код, иначе будет шум в измерениях
    pe.exclude_hv = 1; 
    pe.read_format = PERF_FORMAT_GROUP; // Групповой формат чтения, чтобы считывать всё вместе

    // leader = cycles
    pe.type = PERF_TYPE_HARDWARE;
    pe.config = PERF_COUNT_HW_CPU_CYCLES;
    ctx->fd_leader = perf_event_open(&pe, 0, -1, -1, 0);

    // instructions
    pe.config = PERF_COUNT_HW_INSTRUCTIONS;
    ctx->fd_instructions = perf_event_open(&pe, 0, -1, ctx->fd_leader, 0);

    // branch instructions
    pe.config = PERF_COUNT_HW_BRANCH_INSTRUCTIONS;
    ctx->fd_branch_instr = perf_event_open(&pe, 0, -1, ctx->fd_leader, 0);

    // branch misses
    pe.config = PERF_COUNT_HW_BRANCH_MISSES;
    ctx->fd_branch_miss = perf_event_open(&pe, 0, -1, ctx->fd_leader, 0);

    // L1 data cache accesses
    ctx->fd_l1_access = add_cache_event(&pe, PERF_COUNT_HW_CACHE_L1D, PERF_COUNT_HW_CACHE_OP_READ, PERF_COUNT_HW_CACHE_RESULT_ACCESS, ctx->fd_leader);

    // L1 misses
    ctx->fd_l1_miss = add_cache_event(&pe, PERF_COUNT_HW_CACHE_L1D, PERF_COUNT_HW_CACHE_OP_READ, PERF_COUNT_HW_CACHE_RESULT_MISS, ctx->fd_leader);

    // LLC accesses (L3)
    ctx->fd_llc_access = add_cache_event(&pe, PERF_COUNT_HW_CACHE_LL, PERF_COUNT_HW_CACHE_OP_READ, PERF_COUNT_HW_CACHE_RESULT_ACCESS, ctx->fd_leader);

    // LLC misses
    ctx->fd_llc_miss = add_cache_event(&pe, PERF_COUNT_HW_CACHE_LL, PERF_COUNT_HW_CACHE_OP_READ, PERF_COUNT_HW_CACHE_RESULT_MISS, ctx->fd_leader);
}

// Создаётся файл, в котором будут храниться значения метрик
void perf_global_init(const char* filename)
{
    perf_file = fopen(filename, "w"); // Файл перезаписывается при каждом запуске
    fprintf(
        perf_file,
        "time_ns,"
        "cycles,"
        "instructions,"
        "branch_instr,"
        "branch_misses,"
        "l1_access,"
        "l1_miss,"
        "llc_access,"
        "llc_miss\n"
    ); 
}


// Запуск измерения 
void perf_start(perf_ctx* ctx)
{
    static int initialized = 0; // Если первый запуск, то инициализируем 
    if(!initialized)
    {
        perf_init_ctx(ctx);
        initialized = 1;
    }

    clock_gettime(CLOCK_MONOTONIC, &ctx->start); // Время начала 
    ioctl(ctx->fd_leader, PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP); // Сброс счетчиков
    ioctl(ctx->fd_leader, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP); // Включение счётчиков
}


// Конец физмерения
void perf_stop(perf_ctx* ctx)
{
    ioctl(ctx->fd_leader, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP); // Выключение счётчиков
    clock_gettime(CLOCK_MONOTONIC, &ctx->end); // Время конца

    // Структура, в которую записываются данные
    struct {
        long long nr;
        long long values[PERF_EVENT_COUNT];
    } data;

    read(ctx->fd_leader, &data, sizeof(data)); // Считываем данные с счётчиков, переносим данные в контекст
    for(int i = 0; i < data.nr; i++)
    {
        ctx->values[i] = data.values[i];
    }
}

// Запись данных в файл
void perf_write(perf_ctx* ctx)
{
    long long time_ns = (ctx->end.tv_sec - ctx->start.tv_sec) * 1000000000LL + (ctx->end.tv_nsec - ctx->start.tv_nsec);

    fprintf(
        perf_file,
        "%lld,"
        "%lld,"
        "%lld,"
        "%lld,"
        "%lld,"
        "%lld,"
        "%lld,"
        "%lld,"
        "%lld\n",
        time_ns,
        ctx->values[0], // cycles
        ctx->values[1], // instructions
        ctx->values[2], // branch instr
        ctx->values[3], // branch miss
        ctx->values[4], // L1 access
        ctx->values[5], // L1 miss
        ctx->values[6], // LLC access
        ctx->values[7]  // LLC miss
    );
}