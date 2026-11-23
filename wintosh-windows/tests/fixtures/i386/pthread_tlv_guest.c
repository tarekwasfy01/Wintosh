/* Wintosh-original i386 pthread/TLV smoke fixture; GPL-3.0-only. */
#include <stdint.h>

typedef unsigned long pthread_t;
extern int pthread_create(pthread_t *, const void *, void *(*)(void *), void *);
extern int pthread_join(pthread_t, void **);

static _Thread_local int value = 40;

static void *worker(void *argument)
{
	(void)argument;
	if (value != 40)
		return (void *)(intptr_t)1;
	value += 37;
	return (void *)(intptr_t)value;
}

int main(int argc, char **argv, char **envp)
{
	(void)argc;
	(void)argv;
	(void)envp;
	value = 9;
	pthread_t thread = 0;
	void *result = 0;
	if (pthread_create(&thread, 0, worker, 0) != 0 ||
		pthread_join(thread, &result) != 0)
		return 50;
	if (value != 9 || (intptr_t)result != 77)
		return 51;
	return 42;
}
