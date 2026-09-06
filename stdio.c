/* stdio.c - minimal freestanding printf-style formatter (T2-5 substrate helper).
 * Supports %s %c %d %u %x %% (and %l variants). Uses <stdarg.h> (compiler-builtin, works
 * in freestanding; not a libc dependency). bx_printf writes to fd 1. Not a full libc
 * (S09: integer/string conversions only, no floats/width precision). */
#include "boruix.h"
#include <stdarg.h>

static void fmt_str(char* out, int* n, const char* s) {
    if (!s) s = "(null)";
    while (*s) { if (out) out[(*n)] = *s; (*n)++; s++; }
}
static void fmt_ulong(char* out, int* n, unsigned long v, int base, int upper) {
    char tmp[24]; int tn = 0; int d;
    if (v == 0) tmp[tn++] = '0';
    while (v > 0) { d = (int)(v % (unsigned long)base); v /= (unsigned long)base;
        tmp[tn++] = (char)(d < 10 ? '0' + d : (upper ? 'A' : 'a') + d - 10); }
    while (tn > 0) { if (out) out[(*n)] = tmp[--tn]; (*n)++; }
}
static void vfmt(char* out, const char* fmt, va_list ap) {
    int n = 0, i = 0;
    while (fmt[i]) {
        char c = fmt[i++];
        if (c != '%') { if (out) out[n] = c; n++; continue; }
        c = fmt[i++];
        int islong = 0;
        if (c == 'l') { islong = 1; c = fmt[i++]; }  /* %ld %lu %lx */
        switch (c) {
            case '%': if(out) out[n]='%'; n++; break;
            case 'c': if(out) out[n]=(char)va_arg(ap,int); n++; break;
            case 's': fmt_str(out,&n,va_arg(ap,const char*)); break;
            case 'd': { long v = islong ? va_arg(ap,long) : (long)va_arg(ap,int);
                         if(v<0){ if(out)out[n]='-'; n++; v=-v;} fmt_ulong(out,&n,(unsigned long)v,10,0); break; }
            case 'u': { unsigned long v = islong ? (unsigned long)va_arg(ap,unsigned long)
                         : (unsigned long)va_arg(ap,unsigned int); fmt_ulong(out,&n,v,10,0); break; }
            case 'x': { unsigned long v = islong ? (unsigned long)va_arg(ap,unsigned long)
                         : (unsigned long)va_arg(ap,unsigned int); fmt_ulong(out,&n,v,16,0); break; }
            default: break;
        }
    }
    if (out) out[n] = 0;
}
int bx_printf(const char* fmt, ...) {
    va_list ap; char buf[160]; int n;
    va_start(ap, fmt);
    vfmt(buf, fmt, ap);
    va_end(ap);
    n = 0; while (buf[n]) n++;
    boruix_write(1, buf, (ulong)n);
    return n;
}
int bx_snprintf(char* out, unsigned long cap, const char* fmt, ...) {
    va_list ap; int n; (void)cap;
    va_start(ap, fmt);
    vfmt(out, fmt, ap);
    va_end(ap);
    n = 0; while (out[n]) n++;
    return n;
}
