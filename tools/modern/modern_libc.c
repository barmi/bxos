#include <stdarg.h>
#include <stddef.h>

static char *emit_char(char *out, char c)
{
	*out++ = c;
	return out;
}

static char *emit_repeat(char *out, char c, int count)
{
	while (count-- > 0) {
		out = emit_char(out, c);
	}
	return out;
}

static int format_uint(char *buf, unsigned int value, unsigned int base, int upper)
{
	static const char lower_digits[] = "0123456789abcdef";
	static const char upper_digits[] = "0123456789ABCDEF";
	const char *digits = upper ? upper_digits : lower_digits;
	int len = 0;

	do {
		buf[len++] = digits[value % base];
		value /= base;
	} while (value != 0);
	return len;
}

static char *emit_number(char *out, unsigned int value, int negative,
		unsigned int base, int upper, int width, int left, int zero)
{
	char tmp[32];
	int len = format_uint(tmp, value, base, upper);
	int total = len + negative;
	char pad = zero && !left ? '0' : ' ';

	if (!left && pad == ' ') {
		out = emit_repeat(out, ' ', width - total);
	}
	if (negative) {
		out = emit_char(out, '-');
	}
	if (!left && pad == '0') {
		out = emit_repeat(out, '0', width - total);
	}
	while (len-- > 0) {
		out = emit_char(out, tmp[len]);
	}
	if (left) {
		out = emit_repeat(out, ' ', width - total);
	}
	return out;
}

int vsprintf(char *s, const char *format, va_list ap)
{
	char *out = s;

	while (*format) {
		int left = 0;
		int zero = 0;
		int width = 0;

		if (*format != '%') {
			out = emit_char(out, *format++);
			continue;
		}
		format++;
		if (*format == '-') {
			left = 1;
			format++;
		}
		if (*format == '0') {
			zero = 1;
			format++;
		}
		while ('0' <= *format && *format <= '9') {
			width = width * 10 + (*format++ - '0');
		}

		if (*format == 'd') {
			int v = va_arg(ap, int);
			unsigned int uv = v < 0 ? 0u - (unsigned int) v : (unsigned int) v;
			out = emit_number(out, uv, v < 0, 10, 0, width, left, zero);
		} else if (*format == 'u') {
			unsigned int v = va_arg(ap, unsigned int);
			out = emit_number(out, v, 0, 10, 0, width, left, zero);
		} else if (*format == 'x' || *format == 'X') {
			unsigned int v = va_arg(ap, unsigned int);
			out = emit_number(out, v, 0, 16, *format == 'X', width, left, zero);
		} else if (*format == 's') {
			const char *p = va_arg(ap, const char *);
			int len = 0;
			if (p == 0) {
				p = "(null)";
			}
			while (p[len]) {
				len++;
			}
			if (!left) {
				out = emit_repeat(out, ' ', width - len);
			}
			while (*p) {
				out = emit_char(out, *p++);
			}
			if (left) {
				out = emit_repeat(out, ' ', width - len);
			}
		} else if (*format == 'c') {
			out = emit_char(out, (char) va_arg(ap, int));
		} else if (*format == '%') {
			out = emit_char(out, '%');
		} else {
			out = emit_char(out, '%');
			if (*format) {
				out = emit_char(out, *format);
			}
		}
		if (*format) {
			format++;
		}
	}
	*out = '\0';
	return out - s;
}

int sprintf(char *s, const char *format, ...)
{
	int len;
	va_list ap;

	va_start(ap, format);
	len = vsprintf(s, format, ap);
	va_end(ap);
	return len;
}

char *strcpy(char *s, const char *ct)
{
	char *ret = s;
	while ((*s++ = *ct++) != '\0') {
	}
	return ret;
}

char *strncpy(char *s, const char *ct, size_t n)
{
	char *ret = s;
	while (n != 0 && *ct != '\0') {
		*s++ = *ct++;
		n--;
	}
	while (n-- != 0) {
		*s++ = '\0';
	}
	return ret;
}

int strcmp(const char *cs, const char *ct)
{
	while (*cs == *ct) {
		if (*cs == '\0') {
			return 0;
		}
		cs++;
		ct++;
	}
	return (unsigned char) *cs - (unsigned char) *ct;
}

int strncmp(const char *cs, const char *ct, size_t n)
{
	while (n != 0) {
		if (*cs != *ct || *cs == '\0') {
			return (unsigned char) *cs - (unsigned char) *ct;
		}
		cs++;
		ct++;
		n--;
	}
	return 0;
}

size_t strlen(const char *cs)
{
	const char *p = cs;
	while (*p) {
		p++;
	}
	return p - cs;
}

void *memcpy(void *s, const void *ct, size_t n)
{
	unsigned char *d = s;
	const unsigned char *p = ct;
	while (n-- != 0) {
		*d++ = *p++;
	}
	return s;
}

void *memmove(void *s, const void *ct, size_t n)
{
	unsigned char *d = s;
	const unsigned char *p = ct;
	if (d < p) {
		while (n-- != 0) {
			*d++ = *p++;
		}
	} else {
		d += n;
		p += n;
		while (n-- != 0) {
			*--d = *--p;
		}
	}
	return s;
}

int memcmp(const void *cs, const void *ct, size_t n)
{
	const unsigned char *a = cs;
	const unsigned char *b = ct;
	while (n-- != 0) {
		if (*a != *b) {
			return *a - *b;
		}
		a++;
		b++;
	}
	return 0;
}

void *memset(void *s, int c, size_t n)
{
	unsigned char *p = s;
	while (n-- != 0) {
		*p++ = (unsigned char) c;
	}
	return s;
}
