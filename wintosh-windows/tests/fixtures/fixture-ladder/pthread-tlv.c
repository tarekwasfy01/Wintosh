/* Original Wintosh fixture; GPL-3.0-only. */
#include <stdint.h>
typedef unsigned long pthread_t;
extern int pthread_create(pthread_t *, const void *, void *(*)(void *), void *);
extern int pthread_join(pthread_t, void **); extern void exit(int);
static __thread int thread_value;
static void *worker(void *argument) { thread_value = *(int *)argument; return (void *)(intptr_t)thread_value; }
int main(void) {
    int value = 37; pthread_t thread = 0; void *result = 0;
    if (pthread_create(&thread, 0, worker, &value) || pthread_join(thread, &result)) exit(50);
    exit((intptr_t)result == 37 ? 0 : 51);
}
