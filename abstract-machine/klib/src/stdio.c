#include <am.h>
#include <klib.h>
#include <klib-macros.h>
#include <stdarg.h>

#if !defined(__ISA_NATIVE__) || defined(__NATIVE_USE_KLIB__)

int printf(const char *fmt, ...) {
  char buf[1024];
  va_list ap;
  va_start(ap, fmt);
  int len = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  for (int i = 0; i < len; i++) {
    putch(buf[i]);
  }

  return len;
}

int vsprintf(char *out, const char *fmt, va_list ap) {
  return vsnprintf(out, (size_t)-1, fmt, ap);
}

int sprintf(char *out, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int len = vsnprintf(out, (size_t)-1, fmt, ap);
  va_end(ap);
  return len;
}

int snprintf(char *out, size_t n, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int len = vsnprintf(out, n, fmt, ap);
  va_end(ap);
  return len;
}

int vsnprintf(char *out, size_t n, const char *fmt, va_list ap) {
  size_t pos = 0;         // 已写入的字符数（不含结尾 0）
  const char *p = fmt;

  if (n == 0) {
    // 不能写入任何字符，但仍要计算理论长度
    out = NULL;
  }

  // 向缓冲区写一个字符（如果还有空间）
  auto void putch_buf(char ch) {
    if (out && pos + 1 < n) {
      out[pos] = ch;
    }
    pos++;
  }

  // 输出一个无符号整数，给定进制
  auto void print_unsigned(unsigned long long val, int base, bool upper) {
    char buf[32];
    const char *digits = upper ? "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                               : "0123456789abcdefghijklmnopqrstuvwxyz";
    int idx = 0;
    if (val == 0) {
      buf[idx++] = '0';
    } else {
      while (val > 0 && idx < (int)sizeof(buf)) {
        buf[idx++] = digits[val % base];
        val /= base;
      }
    }
    // 逆序输出
    while (idx-- > 0) {
      putch_buf(buf[idx]);
    }
  }

  while (*p) {
    if (*p != '%') {
      putch_buf(*p++);
      continue;
    }

    // 处理格式说明
    p++;  // 跳过 '%'
    if (*p == '%') {  // "%%" -> '%'
      putch_buf('%');
      p++;
      continue;
    }

    // 简化实现：忽略 flags/width/precision/length，只处理基本转换
    char ch = *p++;
    switch (ch) {
      case 'c': {
        int c = va_arg(ap, int);
        putch_buf((char)c);
        break;
      }
      case 's': {
        const char *str = va_arg(ap, const char *);
        if (!str) str = "(null)";
        while (*str) {
          putch_buf(*str++);
        }
        break;
      }
      case 'd':
      case 'i': {
        int v = va_arg(ap, int);
        unsigned int uv;
        if (v < 0) {
          putch_buf('-');
          uv = (unsigned int)(-v);
        } else {
          uv = (unsigned int)v;
        }
        print_unsigned(uv, 10, false);
        break;
      }
      case 'u': {
        unsigned int v = va_arg(ap, unsigned int);
        print_unsigned(v, 10, false);
        break;
      }
      case 'x': {
        unsigned int v = va_arg(ap, unsigned int);
        print_unsigned(v, 16, false);
        break;
      }
      case 'X': {
        unsigned int v = va_arg(ap, unsigned int);
        print_unsigned(v, 16, true);
        break;
      }
      case 'p': {
        uintptr_t v = (uintptr_t)va_arg(ap, void *);
        putch_buf('0');
        putch_buf('x');
        print_unsigned((unsigned long long)v, 16, false);
        break;
      }
      default: {
        // 未知的格式，按字面输出
        putch_buf('%');
        putch_buf(ch);
        break;
      }
    }
  }

  // 结尾 0
  if (out && n > 0) {
    size_t term_pos = (pos < n) ? pos : (n - 1);
    out[term_pos] = '\0';
  }

  return (int)pos;
}

#endif
