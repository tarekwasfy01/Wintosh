/* Original Wintosh fixture; GPL-3.0-only. */
#include <stddef.h>
extern void qsort(void *, size_t, size_t, int (*)(const void *, const void *));
extern void exit(int status);
static int compare_ints(const void *left, const void *right) {
    const int a = *(const int *)left, b = *(const int *)right;
    return (a > b) - (a < b);
}
int main(void) {
    int values[] = {4, 1, 3, 2}; qsort(values, 4, sizeof(int), compare_ints);
    exit(values[0] == 1 && values[3] == 4 ? 0 : 31);
}
