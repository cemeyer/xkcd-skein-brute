#include <stdbool.h>
#include <ctype.h>
#include <endian.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include <sys/queue.h>

#include "skein.h"
#include "skein_block.c"
#include "skein.c"

void __attribute__((always_inline))
ASSERT(intptr_t i)
{

	if (!i)
		abort();
}

const char *target =
"5b4da95f5fa08280fc9879df44f418c8f9f12ba424b7757de02bbdfbae0d4c4fdf9317c80cc5fe04c6429073466cf29706b8c25999ddd2f6540d4475cc977b87f4757be023f19b8f4035d7722886b78869826de916a79cf9c94cc79cd4347d24b567aa3e2390a573a373a48a5e676640c79cc70197e1c5e7f902fb53ca1858b6";
uint8_t target_bytes[1024/8];

void
dump_hex(void *v, size_t sz)
{
	uint32_t *u32 = v;

	ASSERT(sz % sizeof(*u32) == 0);

	for (; sz > 0; sz -= sizeof *u32) {
		printf("%08x", be32toh(*u32));
		u32++;
	}

	printf("\n");
	fflush(stdout);
}

uint8_t
nibble(char c)
{

	c = tolower(c);
	if (c >= 'a' && c <= 'f')
		return c - 'a';
	else
		return c - '0';

}

void
read_hex(const char *hs, uint8_t *out)
{
	size_t slen = strlen(hs);

	ASSERT(slen % 8 == 0);

	for (unsigned i = 0; i < slen; i += 2) {
		uint32_t x;
		sscanf(hs, "%08x", &x);

		*(uint32_t *)out = htobe32(x);


		out += sizeof(uint32_t);
		hs += 2*sizeof(uint32_t);
	}
}

inline void
plock(pthread_mutex_t *l)
{
	int r;
	r = pthread_mutex_lock(l);
	ASSERT(r == 0);
}

inline void
punlock(pthread_mutex_t *l)
{
	int r;
	r = pthread_mutex_unlock(l);
	ASSERT(r == 0);
}

inline void
condwait(pthread_cond_t *c, pthread_mutex_t *l)
{
	int r;
	r = pthread_cond_wait(c, l);
	ASSERT(r == 0);
}

inline void *
xmalloc(size_t z)
{
	void *r;
	r = malloc(z);
	ASSERT(r != NULL);
	return r;
}

inline char *
xstrdup(const char *s)
{
	char *r;
	r = strdup(s);
	ASSERT(r != NULL);
	return r;
}

inline void
wakeup(pthread_cond_t  *c)
{
	int r;
	r = pthread_cond_broadcast(c);
	ASSERT(r == 0);
}

struct prefix_work {
	STAILQ_ENTRY(prefix_work) entry;
	char *prefix;
};

pthread_t threads[128];
pthread_mutex_t wlock = PTHREAD_MUTEX_INITIALIZER;
STAILQ_HEAD(prefix_work_hd, prefix_work) whead;
uint32_t wprefixes = 0;
pthread_cond_t wcond = PTHREAD_COND_INITIALIZER;

inline void
ascii_incr_char(char *c, bool *carry_inout)
{
	if (*carry_inout) {
		if (*c != 'z') {
			if (*c != 'Z')
				*c += 1;
			else
				*c = 'a';
			*carry_inout = false;
		} else
			*c = 'A';
	}
}

inline bool
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

void *
generate_fricking_prefixes(void *un)
{
	char prefix[] = "A";
	struct prefix_work *wi;
	bool overflow;

	(void)un;

	plock(&wlock);
	while (wprefixes < 32) {
		while (wprefixes >= 128)
			condwait(&wcond, &wlock);

		wi = xmalloc(sizeof *wi);
		wi->prefix = xstrdup(prefix);

		overflow = ascii_incr(prefix);
		if (overflow)
			break;

		STAILQ_INSERT_TAIL(&whead, wi, entry);
		wprefixes++;
	}

	punlock(&wlock);
	return NULL;
}

inline unsigned
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

inline unsigned
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
char beststring[128] = { 0 };
pthread_mutex_t rlock = PTHREAD_MUTEX_INITIALIZER;

void *
make_hash_sexy_time(void *un)
{
	char string[128] = { 0 };
	uint8_t loc_target_hash[1024/8];
	struct prefix_work *mywork;
	size_t pref_len, str_len;
	unsigned last_best = 4000;
	bool overflow;
	unsigned len = 1;

	(void)un;

	memcpy(loc_target_hash, target_bytes, sizeof(target_bytes));

	plock(&wlock);
	while (wprefixes == 0) {
		wakeup(&wcond);
		punlock(&wlock);
		plock(&wlock);
	}

	ASSERT(!STAILQ_EMPTY(&whead));
	mywork = STAILQ_FIRST(&whead);
	ASSERT(mywork != NULL);

	STAILQ_REMOVE_HEAD(&whead, entry);
	wprefixes--;

	punlock(&wlock);

	pref_len = strlen(mywork->prefix);
	memcpy(string, mywork->prefix, pref_len);
	memset(&string[pref_len], 'A', len);
	free(mywork);

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
			}
			last_best = rbestdist;
			punlock(&rlock);

			if (improved) {
				printf("Found '%s' with distance %u\n", string,
				    hdist);
				fflush(stdout);
			}
		}

		overflow = ascii_incr(&string[pref_len]);
		if (overflow) {
			len++;
			memset(&string[pref_len], 'A', len);
			str_len = strlen(string);
		}
	}
}

int
main(void)
{
#if 0
	Skein1024_Ctxt_t c;
	uint8_t out[1024/8],
		in[4096];
#endif
	int r;
	size_t rd;
	unsigned nthr = 0;
	pthread_attr_t pdetached;

	read_hex(target, target_bytes);

	STAILQ_INIT(&whead);

	r = pthread_attr_init(&pdetached);
	ASSERT(r == 0);
	r = pthread_attr_setdetachstate(&pdetached, PTHREAD_CREATE_DETACHED);
	ASSERT(r == 0);

	r = pthread_create(&threads[nthr++], &pdetached,
	    generate_fricking_prefixes, NULL);
	ASSERT(r == 0);

	for (unsigned i = 0; i < 16; i++) {
		r = pthread_create(&threads[nthr++], &pdetached,
		    make_hash_sexy_time, NULL);
		ASSERT(r == 0);
	}

	while (true)
		sleep(100000);

	(void)rd;

#if 0
	rd = fread(in, 1, sizeof in, stdin);

	r = Skein1024_Init(&c, 1024);
	ASSERT(r == SKEIN_SUCCESS);

	r = Skein1024_Update(&c, in, rd);
	ASSERT(r == SKEIN_SUCCESS);

	r = Skein1024_Final(&c, out);
	ASSERT(r == SKEIN_SUCCESS);

	dump_hex(out, sizeof out);
	dump_hex(target_bytes, sizeof target_bytes);
#endif

	return 0;
}
