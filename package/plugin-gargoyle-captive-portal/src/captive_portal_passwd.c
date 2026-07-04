/*
 * captive_portal_passwd - salted SHA-512 crypt() hash/verify helper for the
 * captive portal guest password. Uses musl's built-in crypt() ($6$ SHA-512)
 * so no extra package (openssl-util, mkpasswd) is required at runtime -
 * matches the same primitive /etc/shadow already relies on on this platform.
 *
 * Usage:
 *   captive_portal_passwd -H <password>          print a fresh $6$ hash
 *   captive_portal_passwd -V <password> <hash>    exit 0 if it matches, 1 if not
 */

#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <crypt.h>

/* musl implements crypt() (SHA-256/SHA-512) directly in libc - no separate
 * libcrypt/openssl-util package needed at runtime on the router. */

static const char salt_chars[] =
	"./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

static int make_salt(char *out, size_t out_len)
{
	unsigned char raw[16];
	FILE *f;
	size_t i;

	f = fopen("/dev/urandom", "rb");
	if (!f)
		return -1;
	if (fread(raw, 1, sizeof(raw), f) != sizeof(raw)) {
		fclose(f);
		return -1;
	}
	fclose(f);

	if (out_len < sizeof(raw) + 1)
		return -1;

	for (i = 0; i < sizeof(raw); i++)
		out[i] = salt_chars[raw[i] % (sizeof(salt_chars) - 1)];
	out[sizeof(raw)] = '\0';

	return 0;
}

static int do_hash(const char *password)
{
	char salt[32];
	char full_salt[40];
	char *hash;

	if (make_salt(salt, sizeof(salt)) != 0) {
		fprintf(stderr, "captive_portal_passwd: failed to generate salt\n");
		return 1;
	}
	snprintf(full_salt, sizeof(full_salt), "$6$%s$", salt);

	hash = crypt(password, full_salt);
	if (!hash) {
		fprintf(stderr, "captive_portal_passwd: crypt() failed\n");
		return 1;
	}

	printf("%s\n", hash);
	return 0;
}

static int do_verify(const char *password, const char *stored_hash)
{
	char *hash;

	if (strncmp(stored_hash, "$6$", 3) != 0) {
		fprintf(stderr, "captive_portal_passwd: unsupported hash format\n");
		return 1;
	}

	hash = crypt(password, stored_hash);
	if (!hash)
		return 1;

	if (strcmp(hash, stored_hash) == 0)
		return 0;

	return 1;
}

int main(int argc, char **argv)
{
	if (argc == 3 && strcmp(argv[1], "-H") == 0)
		return do_hash(argv[2]);

	if (argc == 4 && strcmp(argv[1], "-V") == 0)
		return do_verify(argv[2], argv[3]);

	fprintf(stderr, "usage: %s -H <password>\n", argv[0]);
	fprintf(stderr, "       %s -V <password> <hash>\n", argv[0]);
	return 2;
}
