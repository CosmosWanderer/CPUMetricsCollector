#pragma once

#include <time.h>

#define PERF_EVENT_COUNT 9 // Сколько собираем метрик

/*
Если нужно, минимальный пример использования 
void func() {
    perf_ctx ctx;
    perf_start(&ctx);

    ...

    if (something) {

        // Нужно заканчивать сбор метрик и сохранять значения перед каждым выходом из функции
        perf_stop(&ctx);
        perf_write(&ctx);
        
        return;
    }

    ...

    perf_stop(&ctx);
    perf_write(&ctx);
}

int main() {
    perf_global_init("metrics.csv");

    for (int i = 0; i < 10; i++) {
        func();
    }
}
*/


// Контекст измерения метрик (вся информация, что нам нужна)
typedef struct
{
    // fd - "file descriptors" событий
    // Дескрипторы события создаём через perf_event_open(...)

    // Главный счетчик группы, нужен, чтобы читать все метрики одним read(ctx->fd_leader, ...)
    int fd_leader;

    int fd_instructions;
    int fd_branch_instr;
    int fd_branch_miss;

    int fd_l1_access;
    int fd_l1_miss;

    int fd_llc_access;
    int fd_llc_miss;

    // Массив, в который сохраняются измеренные метрики (после вызова perf_stop(&ctx))
    long long values[PERF_EVENT_COUNT];

    // Время начала и конца измерения для получения времени выполнения вызова функции
    struct timespec start;
    struct timespec end;

} perf_ctx;

// Создаётся файл, в который будут сохранены значения метрик
void perf_global_init(const char* filename);

// Запуск измерения (создаются perf события, сохраняется время старта, обнуляются и включаются счётчики)
void perf_start(perf_ctx* ctx);

// Конец измерения (Счётчики выключаются, сохраняется время окончания, значения счетчиков записываются в values[])
void perf_stop(perf_ctx* ctx);

// Запись в файл (используются values[], start, end)
void perf_write(perf_ctx* ctx);