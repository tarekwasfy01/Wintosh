/* Wintosh-original i386 C++ TLS fixture; GPL-3.0-only. */
#include <stdint.h>

static int destructor_count;

typedef unsigned long pthread_t;
extern "C" {
extern int pthread_create(pthread_t *, const void *, void *(*)(void *), void *);
extern int pthread_join(pthread_t, void **);
}

struct ThreadValue final {
	int value;
	ThreadValue() : value(40) {}
	~ThreadValue() { ++destructor_count; }
};

thread_local ThreadValue thread_value;

static void *worker(void *argument)
{
	(void)argument;
	thread_value.value += 37;
	return (void *)(intptr_t)thread_value.value;
}

int main(int argc, char **argv, char **envp)
{
	(void)argc;
	(void)argv;
	(void)envp;
	pthread_t thread = 0;
	void *result = 0;
	if (pthread_create(&thread, 0, worker, 0) != 0 ||
		pthread_join(thread, &result) != 0)
		return 50;
	return (intptr_t)result == 77 && destructor_count == 1 ? 42 : 51;
}
