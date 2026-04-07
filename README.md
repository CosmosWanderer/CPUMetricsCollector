# CPUMetricsCollector

Для работы инструмента требуется:
1. Подключить в файлы с main-функцией и тестируемой функцией заголовочный файл "__perf_metrics.h"
2. В начале main-функции инициализоварть работу счетчиков
```c
#include "__perf_metrics.h"

int main() {
    perf_global_init("metrics.csv");

    ...
}
```
3. Проинструментировать тестируемую функцию в точке входа и в точках выхода методами из заголовочного файла
```c
#include "__perf_metrics.h"

void function() {
    static perf_ctx ctx;
    perf_start(&ctx);
 
    ...

    if (exit_early) {
        perf_stop(&ctx);
        perf_write(&ctx);
        return;
    }

    ...


    perf_stop(&ctx);
    perf_write(&ctx);
}
```

gcc -DSPEC_CPU -DSPEC_CPU_LINUX *.c -o Test -lm
sudo taskset -c 0-3 ./Test 513