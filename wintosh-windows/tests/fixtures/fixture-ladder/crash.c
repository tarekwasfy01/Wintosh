/* Original Wintosh fixture; GPL-3.0-only. */
extern void exit(int);
int main(void) {
    volatile int *null_pointer = (volatile int *)0; *null_pointer = 1;
    exit(60);
}
