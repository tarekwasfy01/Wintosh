/* Original Wintosh fixture; GPL-3.0-only. */
extern char **environ;
extern char *getenv(const char *name);
extern int puts(const char *value);
extern void exit(int status);
int main(int argc, char **argv, char **envp) {
    if (argc != 1 || argv == 0 || envp == 0 || environ == 0) exit(11);
    if (getenv("WINTOSH_FIXTURE") == 0) exit(12);
    puts("fixture argv-env"); exit(0);
}
