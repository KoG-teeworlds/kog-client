#include "monocypher-ed25519.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void to_hex(const unsigned char *in, size_t n, char *out)
{
	static const char digits[] = "0123456789abcdef";
	for(size_t i = 0; i < n; i++)
	{
		out[2 * i] = digits[in[i] >> 4];
		out[2 * i + 1] = digits[in[i] & 15];
	}
	out[2 * n] = '\0';
}

static int from_hex(const char *s, unsigned char *out, size_t n)
{
	if(strlen(s) != 2 * n)
		return -1;
	for(size_t i = 0; i < 2 * n; i++)
	{
		const char c = s[i];
		int v = -1;
		if(c >= '0' && c <= '9')
			v = c - '0';
		else if(c >= 'a' && c <= 'f')
			v = c - 'a' + 10;
		else if(c >= 'A' && c <= 'F')
			v = c - 'A' + 10;
		if(v < 0)
			return -1;
		out[i / 2] = (unsigned char)(i % 2 ? out[i / 2] | v : v << 4);
	}
	return 0;
}

static unsigned char *read_file(const char *path, size_t *size)
{
	FILE *f = fopen(path, "rb");
	if(!f)
		return NULL;
	size_t cap = 4096, len = 0;
	unsigned char *buf = malloc(cap);
	size_t got;
	while(buf && (got = fread(buf + len, 1, cap - len, f)) > 0)
	{
		len += got;
		if(len == cap)
		{
			cap *= 2;
			buf = realloc(buf, cap);
		}
	}
	fclose(f);
	*size = len;
	return buf;
}

static int base64_value(char c)
{
	if(c >= 'A' && c <= 'Z')
		return c - 'A';
	if(c >= 'a' && c <= 'z')
		return c - 'a' + 26;
	if(c >= '0' && c <= '9')
		return c - '0' + 52;
	if(c == '+')
		return 62;
	if(c == '/')
		return 63;
	return -1;
}

static int seed_from_pem(const unsigned char *text, size_t size, unsigned char seed[32])
{
	static const char BEGIN[] = "-----BEGIN PRIVATE KEY-----";
	static const unsigned char PREFIX[16] = {0x30, 0x2e, 0x02, 0x01, 0x00, 0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x70, 0x04, 0x22, 0x04, 0x20};
	unsigned char der[64];
	size_t length = 0;
	unsigned value = 0;
	int bits = 0;
	if(size < sizeof(BEGIN) || memcmp(text, BEGIN, sizeof(BEGIN) - 1) != 0)
		return -1;
	for(size_t i = sizeof(BEGIN) - 1; i < size && text[i] != '-'; i++)
	{
		const int v = base64_value((char)text[i]);
		if(v < 0)
			continue;
		value = (value << 6) | (unsigned)v;
		bits += 6;
		if(bits >= 8)
		{
			bits -= 8;
			if(length >= sizeof(der))
				return -1;
			der[length++] = (unsigned char)(value >> bits);
		}
	}
	if(length != 48 || memcmp(der, PREFIX, sizeof(PREFIX)) != 0)
		return -1;
	memcpy(seed, der + 16, 32);
	crypto_wipe(der, sizeof(der));
	return 0;
}

static int load_seed(const char *path, unsigned char seed[32])
{
	size_t size;
	unsigned char *text = read_file(path, &size);
	if(!text)
		return -1;
	int result = seed_from_pem(text, size, seed);
	if(result != 0)
	{
		char hex[65] = "";
		if(size >= 64)
		{
			memcpy(hex, text, 64);
			hex[64] = '\0';
		}
		result = from_hex(hex, seed, 32);
		crypto_wipe(hex, sizeof(hex));
	}
	crypto_wipe(text, size);
	free(text);
	return result;
}

static void public_key(const unsigned char seed_in[32], unsigned char pub[32])
{
	unsigned char seed[32], secret[64];
	memcpy(seed, seed_in, 32);
	crypto_ed25519_key_pair(secret, pub, seed);
	crypto_wipe(secret, sizeof(secret));
}

static int cmd_keygen(const char *path)
{
	unsigned char seed[32], pub[32];
	FILE *random = fopen("/dev/urandom", "rb");
	if(!random || fread(seed, 1, 32, random) != 32)
	{
		fprintf(stderr, "cannot read /dev/urandom\n");
		return 1;
	}
	fclose(random);
	const int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
	if(fd < 0)
	{
		fprintf(stderr, "cannot create %s: %s\n", path, strerror(errno));
		return 1;
	}
	char hex[65];
	to_hex(seed, 32, hex);
	if(write(fd, hex, 64) != 64 || write(fd, "\n", 1) != 1 || close(fd) != 0)
	{
		fprintf(stderr, "cannot write %s\n", path);
		return 1;
	}
	public_key(seed, pub);
	to_hex(pub, 32, hex);
	printf("%s\n", hex);
	crypto_wipe(seed, sizeof(seed));
	return 0;
}

static int cmd_pubkey(const char *path)
{
	unsigned char seed[32], pub[32];
	if(load_seed(path, seed) != 0)
	{
		fprintf(stderr, "cannot read a key from %s\n", path);
		return 1;
	}
	public_key(seed, pub);
	char hex[65];
	to_hex(pub, 32, hex);
	printf("%s\n", hex);
	return 0;
}

static int cmd_sign(const char *key_path, const char *data_path)
{
	unsigned char seed[32], secret[64], pub[32], signature[64];
	if(load_seed(key_path, seed) != 0)
	{
		fprintf(stderr, "cannot read a key from %s\n", key_path);
		return 1;
	}
	size_t size;
	unsigned char *data = read_file(data_path, &size);
	if(!data)
	{
		fprintf(stderr, "cannot read %s\n", data_path);
		return 1;
	}
	crypto_ed25519_key_pair(secret, pub, seed);
	crypto_ed25519_sign(signature, secret, data, size);
	crypto_wipe(secret, sizeof(secret));
	char key_id[9], sig_hex[129];
	to_hex(pub, 4, key_id);
	to_hex(signature, 64, sig_hex);
	printf("kog-update-sig-v1 %s %s\n", key_id, sig_hex);
	free(data);
	return 0;
}

static int cmd_verify(const char *pub_hex, const char *data_path, const char *sig_path)
{
	unsigned char pub[32], signature[64];
	if(from_hex(pub_hex, pub, 32) != 0)
	{
		fprintf(stderr, "invalid public key\n");
		return 2;
	}
	size_t size, sig_size;
	unsigned char *data = read_file(data_path, &size);
	unsigned char *sig_text = read_file(sig_path, &sig_size);
	if(!data || !sig_text)
	{
		fprintf(stderr, "cannot read the inputs\n");
		return 2;
	}
	char line[256] = "";
	memcpy(line, sig_text, sig_size < 255 ? sig_size : 255);
	char key_id[9] = "", sig_hex[129] = "";
	char expected_id[9];
	to_hex(pub, 4, expected_id);
	if(sscanf(line, "kog-update-sig-v1 %8s %128s", key_id, sig_hex) != 2 || strcmp(key_id, expected_id) != 0 || from_hex(sig_hex, signature, 64) != 0)
	{
		fprintf(stderr, "malformed signature file or wrong key id\n");
		return 1;
	}
	const int result = crypto_ed25519_check(signature, pub, data, size);
	printf(result == 0 ? "ok\n" : "BAD SIGNATURE\n");
	return result == 0 ? 0 : 1;
}

int main(int argc, char **argv)
{
	if(argc == 3 && strcmp(argv[1], "keygen") == 0)
		return cmd_keygen(argv[2]);
	if(argc == 3 && strcmp(argv[1], "pubkey") == 0)
		return cmd_pubkey(argv[2]);
	if(argc == 4 && strcmp(argv[1], "sign") == 0)
		return cmd_sign(argv[2], argv[3]);
	if(argc == 5 && strcmp(argv[1], "verify") == 0)
		return cmd_verify(argv[2], argv[3], argv[4]);
	fprintf(stderr, "usage: kogsign keygen KEYFILE | pubkey KEYFILE | sign KEYFILE FILE | verify PUBHEX FILE SIGFILE\n");
	return 2;
}
