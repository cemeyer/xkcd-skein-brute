/*
 * Copyright (c) 2013 Conrad Meyer <cse.cem@gmail.com>
 *
 * Feel free to use, modify, distribute, etc, this work under the terms of the
 * MIT license (file 'LICENSE').
 */

#include <stdbool.h>
#include <ctype.h>
#include <endian.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include <sys/queue.h>

#include <curl/curl.h>

#include "skein.h"
#if 1
# define SKEIN_UNROLL_1024 10
# include "skein_block.c"
#endif
#include "skein.c"

#define NTHREADS 16
#define NELEM(arr) ((sizeof(arr)) / (sizeof((arr)[0])))
#define MAX_STRING 256

#ifdef LOCK_OVERHEAD_DEBUG
uint64_t rlock_taken = 0;
struct timespec g_begin;
#endif

#ifdef __NO_INLINE__
# define TRY_INLINE
#else
# define TRY_INLINE inline
#endif

TRY_INLINE void
ASSERT(uintptr_t i)
{

	if (i == 0)
		abort();
}

const char *target =
"5b4da95f5fa08280fc9879df44f418c8f9f12ba424b7757de02bbdfbae0d4c4fdf9317c80cc5fe04c6429073466cf29706b8c25999ddd2f6540d4475cc977b87f4757be023f19b8f4035d7722886b78869826de916a79cf9c94cc79cd4347d24b567aa3e2390a573a373a48a5e676640c79cc70197e1c5e7f902fb53ca1858b6";
uint8_t target_bytes[1024/8];

void
read_hex(const char *hs, uint8_t *out)
{
	size_t slen = strlen(hs);

	printf("\n"); /* wtf, -O3 */

	ASSERT(slen % 8 == 0);

	for (unsigned i = 0; i < slen; i += 2) {
		uint32_t x;
		sscanf(hs, "%08x", &x);

		*(uint32_t *)out = htobe32(x);


		out += sizeof(uint32_t);
		hs += 2*sizeof(uint32_t);
	}
}

TRY_INLINE void
plock(pthread_mutex_t *l)
{
	int r;
	r = pthread_mutex_lock(l);
	ASSERT(r == 0);
#ifdef LOCK_OVERHEAD_DEBUG
	rlock_taken++;
#endif
}

TRY_INLINE void
punlock(pthread_mutex_t *l)
{
	int r;
#ifdef LOCK_OVERHEAD_DEBUG
	struct timespec cur;
	double lps;

	r = clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &cur);
	ASSERT(r == 0);

	lps = (double)rlock_taken / ((double)cur.tv_sec - g_begin.tv_sec +
	    0.000000001 * ((double)cur.tv_nsec - g_begin.tv_nsec));
	printf("lock taken %"PRIu64" times (%.03f locks/sec)\n", rlock_taken,
	    lps);
#endif

	r = pthread_mutex_unlock(l);
	ASSERT(r == 0);
}

TRY_INLINE void
condwait(pthread_cond_t *c, pthread_mutex_t *l)
{
	int r;
	r = pthread_cond_wait(c, l);
	ASSERT(r == 0);
}

TRY_INLINE void
wakeup(pthread_cond_t  *c)
{
	int r;
	r = pthread_cond_broadcast(c);
	ASSERT(r == 0);
}

TRY_INLINE void
ascii_incr_char(char *c, bool *carry_inout)
{
	if (*carry_inout) {
		if (*c != 'z') {
			if (*c != 'Z') {
				if (*c != '9')
					*c += 1;
				else
					*c = 'A';
			} else
				*c = 'a';
			*carry_inout = false;
		} else
			*c = '0';
	}
}

TRY_INLINE bool
ascii_incr(char *str)
{
	char *eos = str + strlen(str) - 1;
	bool carry = true;

	while (true) {
		ascii_incr_char(eos, &carry);

		if (eos == str && carry)
			return true;

		if (!carry)
			return false;

		eos--;
	}
}

TRY_INLINE unsigned
xor_dist(uint8_t *a8, uint8_t *b8, size_t len)
{
	unsigned tot = 0;
	uint64_t *a = (void*)a8,
		 *b = (void*)b8;

	ASSERT(len % (sizeof *a) == 0);

	while (len > 0) {
		tot += __builtin_popcountll(*a ^ *b);
		a++;
		b++;
		len -= sizeof(*a);
	}

	return tot;
}

TRY_INLINE unsigned
hash_dist(const char *trial, size_t len, uint8_t *hash)
{
	uint8_t trhash[1024/8];
	Skein1024_Ctxt_t c;
	int r;

	r = Skein1024_Init(&c, 1024);
	ASSERT(r == SKEIN_SUCCESS);

	r = Skein1024_Update(&c, (void*)trial, len);
	ASSERT(r == SKEIN_SUCCESS);

	r = Skein1024_Final(&c, trhash);
	ASSERT(r == SKEIN_SUCCESS);

	return xor_dist(trhash, hash, sizeof trhash);
}

unsigned rbestdist = 2000;
char beststring[256] = { 0 };
pthread_mutex_t rlock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t rcond = PTHREAD_COND_INITIALIZER;

void
init_random(char initvalue[128], unsigned *len_out)
{
	size_t rd;
	FILE *f = fopen("/dev/urandom", "rb");
	const char *cs = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
	    "abcdefghijklmnopqrstuvwxyz";
	uint64_t rnd;

	rd = fread(&rnd, sizeof rnd, 1, f);
	ASSERT(rd == 1);
	fclose(f);

	unsigned clen = strlen(cs);
	for (unsigned i = 0; rnd > 0; i++) {
		initvalue[i] = cs[ rnd % clen ];
		rnd /= clen;
		*len_out += 1;
	}
}

size_t
curl_devnull(char *ptr, size_t size, size_t nmemb, void *userdata)
{

	(void)ptr;
	(void)userdata;

	return size*nmemb;
}

void *
submit(void *un)
{
	unsigned best = 420;
	char bests[MAX_STRING],
	     fmt[MAX_STRING + 64],
	     errbuf[CURL_ERROR_SIZE];
	CURL *ch;
	CURLcode r;

	(void)un;
	ch = curl_easy_init();
	ASSERT(ch != NULL);
	r = curl_easy_setopt(ch, CURLOPT_URL,
	    "http://almamater.xkcd.com/?edu=uw.edu");
	ASSERT(r == CURLE_OK);
	r = curl_easy_setopt(ch, CURLOPT_POST, 1);
	ASSERT(r == CURLE_OK);
	r = curl_easy_setopt(ch, CURLOPT_ERRORBUFFER, errbuf);
	ASSERT(r == CURLE_OK);
	r = curl_easy_setopt(ch, CURLOPT_WRITEFUNCTION, curl_devnull);
	ASSERT(r == CURLE_OK);

	while (true) {
		plock(&rlock);
		while (rbestdist >= best)
			condwait(&rcond, &rlock);

		best = rbestdist;
		memcpy(bests, beststring, sizeof beststring);
		punlock(&rlock);

		printf("Submit - got %s (%u) to submit!\n", bests, best);

retry:
		sprintf(fmt, "hashable=%s", bests);
		r = curl_easy_setopt(ch, CURLOPT_POSTFIELDS, (void*)fmt);
		ASSERT(r == CURLE_OK);

		r = curl_easy_perform(ch);
		if (r != CURLE_OK) {
			printf("An error occurred for %s: %s\n", bests, errbuf);
			sleep(15);
			goto retry;
		}

		printf("Submitted %s!\n", bests);
	}
}

void *
make_hash_sexy_time(void *v)
{
	char string[256] = { 0 },
	     *initstr = v;
	uint8_t loc_target_hash[1024/8];
	size_t str_len;
	unsigned last_best = 4000;
	bool overflow;
	unsigned len = strlen(initstr);
	uint64_t nhashes = 0;

	memcpy(loc_target_hash, target_bytes, sizeof(target_bytes));

	strcpy(string, initstr);
	free(initstr);

	str_len = strlen(string);
	while (true) {
		unsigned hdist = hash_dist(string, str_len, loc_target_hash);
		if (hdist < last_best) {
			bool improved = false;

			plock(&rlock);
			if (hdist < rbestdist) {
				rbestdist = hdist;
				memcpy(beststring, string, sizeof beststring);
				improved = true;
				wakeup(&rcond);
			}
			last_best = rbestdist;
			punlock(&rlock);

			if (improved) {
				printf("Found '%s' with distance %u\n", string,
				    hdist);
				fflush(stdout);
			}
		}

		nhashes++;
		if (nhashes > 4000000ULL) {
			init_random(string, &len);
			str_len = strlen(string);
#if 0
			printf("Working skipping to: %s\n", string);
#endif
			nhashes = 0;
			continue;
		}

		for (unsigned i = 0; i < NTHREADS; i++) {
			overflow = ascii_incr(string);
			if (overflow) {
				len++;
				memset(string, 'A', len);
				str_len = strlen(string);
			}
		}
	}
}

int
main(void)
{
	int r;
	pthread_attr_t pdetached;
	pthread_t thr;
	bool overflow;
	unsigned len = 0;
	char initvalue[128] = { 0 };

	init_random(initvalue, &len);
	printf("Starting with: %s\n", initvalue);

	read_hex(target, target_bytes);

#ifdef LOCK_OVERHEAD_DEBUG
	r = clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &g_begin);
	ASSERT(r == 0);
#endif

	r = pthread_attr_init(&pdetached);
	ASSERT(r == 0);
	r = pthread_attr_setdetachstate(&pdetached, PTHREAD_CREATE_DETACHED);
	ASSERT(r == 0);

	r = pthread_create(&thr, &pdetached, submit, NULL);
	ASSERT(r == 0);

	for (unsigned i = 0; i < NTHREADS; i++) {
		r = pthread_create(&thr, &pdetached, make_hash_sexy_time,
		    xstrdup(initvalue));
		ASSERT(r == 0);

		overflow = ascii_incr(initvalue);
		if (overflow) {
			len++;
			memset(initvalue, 'A', len);
		}
	}

	while (true)
		sleep(100000);

	return 0;
}
