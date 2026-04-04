# CPUMetricsCollector

gcc -DSPEC_CPU -DSPEC_CPU_LINUX *.c -o Test -lm
sudo taskset -c 0-3 ./Test 513