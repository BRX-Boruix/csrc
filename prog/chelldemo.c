/* chelldemo.c (T2-0): first real C program on BORUIX via x86-64 clang cross chain.
 * Proves: crt0.S(_start) -> main(argc,argv); write/puts via int 0x80; exit(0).
 * Links only crt0.o + crtrt.o (no Rust libc) => genuine freestanding C, S09/S35.
 */
long boruix_puts(const char* s);
long boruix_write(long fd, const void* buf, unsigned long len);
void exit(int code);
void* memset(void* s, int c, unsigned long n);
void* memcpy(void* d, const void* s, unsigned long n);

int main(int argc, char** argv) {
    char buf[64];
    const char* tag = "[chelldemo] hello from real freestanding C! argc=";
    boruix_write(1, tag, 42);
    /* argc -> decimal */
    int v = argc, i = 30, neg = 0;
    unsigned char tmp[32];
    if (v < 0) { neg = 1; v = -v; }
    int x = v;
    int len = 0;
    if (x == 0) tmp[len++] = '0';
    while (x > 0) { tmp[len++] = (unsigned char)('0' + (x % 10)); x /= 10; }
    char obuf[40];
    int oi = 0;
    if (neg) obuf[oi++] = '-';
    while (len > 0) obuf[oi++] = (char)tmp[--len];
    obuf[oi] = '\n';
    boruix_write(1, obuf, (unsigned long)oi);
    if (argv && argc >= 1 && argv[0]) {
        boruix_write(1, "[chelldemo] argv[0]=", 19);
        boruix_puts(argv[0]);
        boruix_write(1, "\n", 1);
    }
    exit(0);
    return 0;
}
