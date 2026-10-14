/* Original Wintosh fixture; GPL-3.0-only. */
extern int open(const char *, int, ...);
extern int write(int, const void *, unsigned long);
extern int close(int); extern void exit(int);
int main(void) {
    const int fd = open("fixture-ladder.tmp", 0x0002 | 0x0200, 0600);
    if (fd < 0) exit(40); if (write(fd, "wintosh", 7) != 7) exit(41);
    close(fd); exit(0);
}
