#include <am.h>
#include <klib.h>
#include <klib-macros.h>
#include <stdarg.h>

#if !defined(__ISA_NATIVE__) || defined(__NATIVE_USE_KLIB__)

enum {
  LEN_NONE = 0,
  LEN_HH,
  LEN_H,
  LEN_L,
  LEN_LL,
};

static void putch_buf(char *out, size_t n, size_t *pos, char ch) {
  if (out && *pos + 1 < n) {
    out[*pos] = ch;
  }
  (*pos)++;
}

static void putn_buf(char *out, size_t n, size_t *pos, const char *s, size_t len) {
  for (size_t i = 0; i < len; i++) {
    putch_buf(out, n, pos, s[i]);
  }
}

static void put_repeat(char *out, size_t n, size_t *pos, char ch, size_t count) {
  while (count-- > 0) {
    putch_buf(out, n, pos, ch);
  }
}

static size_t utoa_buf(unsigned long long val, unsigned base, bool upper, char *buf) {
  const char *digits = upper ? "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                             : "0123456789abcdefghijklmnopqrstuvwxyz";
  size_t len = 0;

  if (val == 0) {
    buf[len++] = '0';
  } else {
    while (val > 0) {
      buf[len++] = digits[val % base];
      val /= base;
    }
  }

  for (size_t i = 0, j = len ? len - 1 : 0; i < j; i++, j--) {
    char tmp = buf[i];
    buf[i] = buf[j];
    buf[j] = tmp;
  }

  return len;
}

static unsigned long long get_unsigned_arg(va_list ap, int length) {
  switch (length) {
    case LEN_HH: return (unsigned char)va_arg(ap, unsigned int);
    case LEN_H:  return (unsigned short)va_arg(ap, unsigned int);
    case LEN_L:  return va_arg(ap, unsigned long);
    case LEN_LL: return va_arg(ap, unsigned long long);
    default:     return va_arg(ap, unsigned int);
  }
}

static long long get_signed_arg(va_list ap, int length) {
  switch (length) {
    case LEN_HH: return (signed char)va_arg(ap, int);
    case LEN_H:  return (short)va_arg(ap, int);
    case LEN_L:  return va_arg(ap, long);
    case LEN_LL: return va_arg(ap, long long);
    default:     return va_arg(ap, int);
  }
}

static void emit_formatted(char *out, size_t n, size_t *pos, const char *prefix,
                           size_t prefix_len, const char *digits, size_t digit_len,
                           int width, int precision, bool precision_specified,
                           bool left_align, bool zero_pad) {
  size_t zero_len = 0;
  size_t total_len;
  size_t space_len;

  if (precision_specified) {
    if (precision > (int)digit_len) {
      zero_len = (size_t)precision - digit_len;
    }
  } else if (zero_pad && !left_align && width > 0) {
    size_t visible = prefix_len + digit_len;
    if ((size_t)width > visible) {
      zero_len = (size_t)width - visible;
    }
  }

  total_len = prefix_len + zero_len + digit_len;
  space_len = (width > (int)total_len) ? (size_t)width - total_len : 0;

  if (!left_align) {
    put_repeat(out, n, pos, ' ', space_len);
  }
  putn_buf(out, n, pos, prefix, prefix_len);
  put_repeat(out, n, pos, '0', zero_len);
  putn_buf(out, n, pos, digits, digit_len);
  if (left_align) {
    put_repeat(out, n, pos, ' ', space_len);
  }
}

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
  size_t pos = 0;  // 已写入的字符数（不含结尾 0）
  const char *p = fmt;

  if (n == 0) {
    out = NULL;
  }

  while (*p) {
    if (*p != '%') {
      putch_buf(out, n, &pos, *p++);
      continue;
    }

    p++;
    if (*p == '%') {
      putch_buf(out, n, &pos, '%');
      p++;
      continue;
    }

    bool left_align = false;
    bool zero_pad = false;
    bool precision_specified = false;
    int width = 0;
    int precision = 0;
    int length = LEN_NONE;

    while (*p == '-' || *p == '0') {
      if (*p == '-') {
        left_align = true;
      } else if (*p == '0') {
        zero_pad = true;
      }
      p++;
    }

    while (*p >= '0' && *p <= '9') {
      width = width * 10 + (*p - '0');
      p++;
    }

    if (*p == '.') {
      precision_specified = true;
      precision = 0;
      p++;
      while (*p >= '0' && *p <= '9') {
        precision = precision * 10 + (*p - '0');
        p++;
      }
    }

    if (*p == 'h') {
      p++;
      if (*p == 'h') {
        length = LEN_HH;
        p++;
      } else {
        length = LEN_H;
      }
    } else if (*p == 'l') {
      p++;
      if (*p == 'l') {
        length = LEN_LL;
        p++;
      } else {
        length = LEN_L;
      }
    }

    char ch = *p ? *p++ : '\0';
    switch (ch) {
      case 'c': {
        int c = va_arg(ap, int);
        char buf[1] = {(char)c};
        emit_formatted(out, n, &pos, "", 0, buf, 1, width, precision,
                       precision_specified, left_align, false);
        break;
      }
      case 's': {
        const char *str = va_arg(ap, const char *);
        if (!str) str = "(null)";
        size_t len = 0;
        while (str[len] != '\0') {
          len++;
        }
        if (precision_specified && precision < (int)len) {
          len = (size_t)precision;
        }
        emit_formatted(out, n, &pos, "", 0, str, len, width, precision,
                       precision_specified, left_align, false);
        break;
      }
      case 'd':
      case 'i': {
        long long v = get_signed_arg(ap, length);
        unsigned long long uv;
        char prefix[1] = {0};

        if (v < 0) {
          prefix[0] = '-';
          uv = (unsigned long long)(-(v + 1)) + 1;
        } else {
          uv = (unsigned long long)v;
        }
        char buf[32];
        size_t len = 0;
        if (!(precision_specified && precision == 0 && uv == 0)) {
          len = utoa_buf(uv, 10, false, buf);
        }
        emit_formatted(out, n, &pos, prefix, prefix[0] ? 1 : 0, buf, len,
                       width, precision, precision_specified, left_align,
                       zero_pad && !precision_specified);
        break;
      }
      case 'u': {
        unsigned long long v = get_unsigned_arg(ap, length);
        char buf[32];
        size_t len = 0;
        if (!(precision_specified && precision == 0 && v == 0)) {
          len = utoa_buf(v, 10, false, buf);
        }
        emit_formatted(out, n, &pos, "", 0, buf, len, width, precision,
                       precision_specified, left_align,
                       zero_pad && !precision_specified);
        break;
      }
      case 'x': {
        unsigned long long v = get_unsigned_arg(ap, length);
        char buf[32];
        size_t len = 0;
        if (!(precision_specified && precision == 0 && v == 0)) {
          len = utoa_buf(v, 16, false, buf);
        }
        emit_formatted(out, n, &pos, "", 0, buf, len, width, precision,
                       precision_specified, left_align,
                       zero_pad && !precision_specified);
        break;
      }
      case 'X': {
        unsigned long long v = get_unsigned_arg(ap, length);
        char buf[32];
        size_t len = 0;
        if (!(precision_specified && precision == 0 && v == 0)) {
          len = utoa_buf(v, 16, true, buf);
        }
        emit_formatted(out, n, &pos, "", 0, buf, len, width, precision,
                       precision_specified, left_align,
                       zero_pad && !precision_specified);
        break;
      }
      case 'p': {
        uintptr_t v = (uintptr_t)va_arg(ap, void *);
        char buf[32];
        size_t len = utoa_buf((unsigned long long)v, 16, false, buf);
        emit_formatted(out, n, &pos, "0x", 2, buf, len, width, precision,
                       precision_specified, left_align, false);
        break;
      }
      default: {
        putch_buf(out, n, &pos, '%');
        if (ch != '\0') {
          putch_buf(out, n, &pos, ch);
        }
        break;
      }
    }
  }

  if (out && n > 0) {
    size_t term_pos = (pos < n) ? pos : (n - 1);
    out[term_pos] = '\0';
  }

  return (int)pos;
}

#endif
