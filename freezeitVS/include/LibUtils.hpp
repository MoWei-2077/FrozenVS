#pragma once

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <cstdint>

namespace LibUtils {
    static inline unsigned int Fastatoi(const char* str) {
        unsigned int i = 0;
        while (*str) {
            i = i * 10u + (*str - '0');
            ++str;
        }
        return i;
    }

    static inline size_t Faststrlen(const char* str) {
        const char* s = str;
        while (*s) ++s;
        return s - str;
    }

    static inline char* emit_u32(char *buf, char *end, uint32_t val) {
        char tmp[10];
        char *out = tmp + sizeof(tmp);
    
        do {
            *--out = (char)('0' + (val % 10u));
            val /= 10u;
        } while (val);
        const size_t len = (size_t)(tmp + sizeof(tmp) - out);
    
        const size_t avail = (end > buf) ? (size_t)(end - buf) : 0;
        const size_t copy = len < avail ? len : avail;
    
        memcpy(buf, out, copy);
    
        return buf + copy;
    }

    static inline int FastVsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
        char *p = buf;
        char* end = size ? (buf + size - 1) : buf;
    
        while (*fmt) {
            if (*fmt != '%') {
                if (p < end) *p = *fmt;
                ++p; ++fmt;
                continue;
            }
            ++fmt;                          
            switch (*fmt++) {

            case 'd': {
                unsigned int v = va_arg(ap, int);
                p = emit_u32(p, end, (uint32_t)v);
                break;
            }

            case 's': {
                const char *s = va_arg(ap, const char *);
                if (!s) s = "(null)";
                while (*s && p < end) {
                    *p++ = *s++;
                }
                break;
            }

            case '%':
                if (p < end) *p = '%';
                ++p;
                break;

            default:
                break;
            }
        }
    
        if (size) {                       
            if (p > end) *end = '\0';
            else *p = '\0';
        }
        return (int)(p - buf);              
    }
    
    static inline int FastSnprintf(char* buf, size_t size, const char* fmt, ...) {
        va_list ap;
        va_start(ap, fmt);
        int r = FastVsnprintf(buf, size, fmt, ap);
        va_end(ap);
        return r;
    }
};