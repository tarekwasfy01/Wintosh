/* Original Wintosh fixture; GPL-3.0-only. */
#include <stddef.h>
extern void *malloc(size_t size);
extern void free(void *pointer);
extern void *memcpy(void *destination, const void *source, size_t size);
extern void exit(int status);
__attribute__((noinline)) static int protected_copy(char *out) {
    const char text[] = "fixture memory";
    memcpy(out, text, sizeof(text)); return out[0] == 'f' ? 0 : 21;
}
int main(void) {
    char *buffer = (char *)malloc(32); if (buffer == 0) exit(20);
    const int result = protected_copy(buffer); free(buffer); exit(result);
}
