#include <stdio.h>
#include <string.h>
#include <stdint.h>

static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void base64(const unsigned char *in, size_t n, char *out) {
	size_t i;
	for (i = 0; i + 2 < n; i += 3) {
		uint32_t v = (uint32_t)in[i] << 16 | (uint32_t)in[i + 1] << 8 | in[i + 2];
		*out++ = b64[v >> 18];
		*out++ = b64[v >> 12 & 63];
		*out++ = b64[v >> 6 & 63];
		*out++ = b64[v & 63];
	}
	if (i < n) {
		uint32_t v = (uint32_t)in[i] << 16 | (i + 1 < n ? (uint32_t)in[i + 1] << 8 : 0);
		*out++ = b64[v >> 18];
		*out++ = b64[v >> 12 & 63];
		*out++ = i + 1 < n ? b64[v >> 6 & 63] : '=';
		*out++ = '=';
	}
	*out = 0;
}

static size_t unbase64(const char *in, unsigned char *out) {
	size_t n = 0;
	uint32_t v = 0;
	int bits = 0;
	for (; *in && *in != '='; in++) {
		const char *p = strchr(b64, *in);
		if (!p) continue;
		v = v << 6 | (uint32_t)(p - b64);
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			out[n++] = (unsigned char)(v >> bits);
		}
	}
	return n;
}

static uint32_t crc32(const unsigned char *p, size_t n) {
	static uint32_t table[256];
	if (!table[1])
		for (uint32_t i = 0; i < 256; i++) {
			uint32_t c = i;
			for (int k = 0; k < 8; k++) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
			table[i] = c;
		}
	uint32_t c = 0xffffffffu;
	while (n--) c = table[(c ^ *p++) & 0xff] ^ (c >> 8);
	return c ^ 0xffffffffu;
}

static void rot13(char *s) {
	for (; *s; s++)
		if ((*s >= 'a' && *s <= 'z')) *s = (char)('a' + (*s - 'a' + 13) % 26);
		else if (*s >= 'A' && *s <= 'Z') *s = (char)('A' + (*s - 'A' + 13) % 26);
}

int main(void) {
	const char *samples[] = { "", "f", "fo", "foo", "foob", "fooba", "foobar", "The quick brown fox jumps over the lazy dog" };
	char enc[128];
	unsigned char dec[128];
	for (int i = 0; i < 8; i++) {
		const char *s = samples[i];
		base64((const unsigned char *)s, strlen(s), enc);
		size_t n = unbase64(enc, dec);
		printf("crc32 %08x  base64 \"%s\"  roundtrip %s\n", crc32((const unsigned char *)s, strlen(s)), enc,
			n == strlen(s) && memcmp(dec, s, n) == 0 ? "ok" : "BAD");
	}
	char msg[] = "Hello from a self-hosting compiler";
	rot13(msg);
	printf("rot13: %s\n", msg);
	rot13(msg);
	printf("rot13 again: %s\n", msg);
	return 0;
}
