/*
 * Copyright (c) 2013 Conrad Meyer <cse.cem@gmail.com>
 *
 * Feel free to use, modify, distribute, etc, this work under the terms of the
 * MIT license (file 'LICENSE').
 */

/*
 * Build-time configuration options.
 *
 * E.g., to build without CURL:
 *     make EXTRAFLAGS="-DHAVE_CURL=0" LIBFLAGS=""
 */

#ifdef __linux__
# define _GNU_SOURCE
# ifndef HAVE_GETOPT_LONG
#  define HAVE_GETOPT_LONG 1
# endif
#endif

#ifndef HAVE_GETOPT_LONG
# define HAVE_GETOPT_LONG 0
#endif

#ifdef UNROLL_FACTOR
# if UNROLL_FACTOR != 0 && (10 % UNROLL_FACTOR) != 0
#  error Unroll factor must be 0, 2, 5, or 10
# endif
#else  /* !defined(UNROLL_FACTOR) */
# define UNROLL_FACTOR 10
#endif

#ifndef HAVE_CURL
# define HAVE_CURL 1
#endif

#ifndef USE_BITHACKS
# define USE_BITHACKS 0
#endif


#ifdef __FreeBSD__
#include <sys/endian.h>
#endif
#include <ctype.h>
#ifdef __linux__
#include <endian.h>
#endif
#include <err.h>
#include <getopt.h>
#include <inttypes.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/queue.h>

#if HAVE_CURL == 1
# include <curl/curl.h>
#endif

#include "skein.h"
#if 1
# define SKEIN_UNROLL_1024 UNROLL_FACTOR
# include "skein_block.c"
#endif
#include "skein.c"

#define NELEM(arr) ((sizeof(arr)) / (sizeof((arr)[0])))
#define MAX_STRING 512

#ifdef __NO_INLINE__
# define TRY_INLINE static
#else
# define TRY_INLINE static inline
#endif

static unsigned default_last_best = 393;

struct hash_worker_ctx {
	uint64_t	 hash_limit;
	uint8_t		*hash;
	size_t		 hash_len;
};

/*
 * Subs!
 */

TRY_INLINE void
ASSERT(uintptr_t i)
{

	if (i == 0)
		abort();
}

TRY_INLINE void
gettime(struct timespec *t)
{
	int r;

#ifdef __FreeBSD__
	r = clock_gettime(CLOCK_MONOTONIC, t);
#elif defined(__linux__)
	r = clock_gettime(CLOCK_MONOTONIC_RAW, t);
#endif
	ASSERT(r == 0);
}

TRY_INLINE void
read_hex(const char *hs, uint8_t *out)
{
	size_t slen = strlen(hs);

	ASSERT(slen % (2*sizeof(uint32_t)) == 0);

	for (size_t i = 0; i < slen; i += 2*sizeof(uint32_t)) {
		uint32_t x;

		sscanf(hs, "%8"SCNx32, &x);
		*(uint32_t *)out = htobe32(x);


		out += sizeof(uint32_t);
		hs += 2*sizeof(uint32_t);
	}
}

TRY_INLINE double
elapsed(struct timespec *begin)
{
	struct timespec cur;
	double el;

	gettime(&cur);

	el = (double)cur.tv_sec - begin->tv_sec + 0.000000001 *
	    ((double)cur.tv_nsec - begin->tv_nsec);
	return el;
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

/*
 * Borrowed from jhiesey/skeincrack, but presumably also available in a google
 * query for "bit-twiddling hacks"
 */
static const uint64_t m1  = 0x5555555555555555;
static const uint64_t m2  = 0x3333333333333333;
static const uint64_t m4  = 0x0f0f0f0f0f0f0f0f;
static const uint64_t h01 = 0x0101010101010101;

TRY_INLINE unsigned
bithacks_countbits(uint64_t x)
{
	x -= (x >> 1) & m1;		//put count of each 2 bits into those 2 bits
	x = (x & m2) + ((x >> 2) & m2);	//put count of each 4 bits into those 4 bits
	x = (x + (x >> 4)) & m4;	//put count of each 8 bits into those 8 bits
	return (x * h01)>>56;		//returns left 8 bits of x + (x<<8) + (x<<16) + (x<<24) + ...
}

#if USE_BITHACKS == 1
# define COUNTBITS(exp) bithacks_countbits(exp)
#else
# define COUNTBITS(exp) __builtin_popcountll(exp)
#endif

TRY_INLINE unsigned
xor_dist(uint8_t *a8, uint8_t *b8, size_t len)
{
	unsigned tot = 0;
	uint64_t *a = (void*)a8,
		 *b = (void*)b8;

	ASSERT(len % (sizeof *a) == 0);

	while (len > 0) {
		tot += COUNTBITS(*a ^ *b);
		a++;
		b++;
		len -= sizeof(*a);
	}

	return tot;
}

TRY_INLINE unsigned
hash_dist1024(const char *trial, size_t len, uint8_t *hash)
{
	uint8_t trhash[1024/8];
	Skein1024_Ctxt_t c;
	int r;

	r = Skein1024_Init(&c, 1024);
	ASSERT(r == SKEIN_SUCCESS);

	r = Skein1024_Update(&c, (void*)trial, len, false);
	ASSERT(r == SKEIN_SUCCESS);

	r = Skein1024_Final(&c, trhash);
	ASSERT(r == SKEIN_SUCCESS);

	return xor_dist(trhash, hash, sizeof trhash);
}

/*
 * TODO: Use a PRNG that doesn't use stdio/syscalls.
 */
TRY_INLINE void
init_random(FILE *frand, char initvalue[MAX_STRING], unsigned *len_out)
{
	const char *cs = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
	    "abcdefghijklmnopqrstuvwxyz";
	const unsigned cslen = strlen(cs);

	uint64_t rnd[4];
	size_t rd;
	unsigned r, i;

	rd = fread(rnd, sizeof rnd[0], NELEM(rnd), frand);
	ASSERT(rd == NELEM(rnd));

	i = 0;
	for (r = 0; r < NELEM(rnd); r++) {
		for (; rnd[r] > 0; i++) {
			ASSERT(i < MAX_STRING - 1);

			initvalue[i] = cs[ rnd[r] % cslen ];
			rnd[r] /= cslen;
		}
	}

	ASSERT(i < MAX_STRING);
	memset(&initvalue[i], 0, MAX_STRING - i);

	if (len_out != NULL)
		*len_out = i;
}

#if HAVE_CURL == 1
size_t
curl_devnull(char *ptr, size_t size, size_t nmemb, void *userdata)
{

	(void)ptr;
	(void)userdata;

	return size*nmemb;
}

TRY_INLINE void
submit(unsigned score, const char *str)
{
	char fmt[MAX_STRING + 64], errbuf[CURL_ERROR_SIZE];
	CURL *ch;
	CURLcode r;

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

	printf("Submit - got %s (%u) to submit!\n", str, score);

retry:
	sprintf(fmt, "hashable=%s", str);
	r = curl_easy_setopt(ch, CURLOPT_POSTFIELDS, (void*)fmt);
	ASSERT(r == CURLE_OK);

	r = curl_easy_perform(ch);
	if (r != CURLE_OK) {
		printf("An error occurred for %s: %s\n", str, errbuf);
		sleep(15);
		goto retry;
	}

	printf("Submitted %s!\n", str);

	curl_easy_cleanup(ch);
}
#endif  /* HAVE_CURL */

static void *
hash_worker(void *vctx)
{
	char string[MAX_STRING] = { 0 };
	struct hash_worker_ctx *ctx = vctx;
	FILE *fr;
	uint64_t nhashes_wrap = 0, nhashes = 0, my_limit;
	size_t str_len, tlen;
	unsigned last_best = default_last_best, len;
	bool overflow;
	uint8_t *target;

	my_limit = ctx->hash_limit;
	target = ctx->hash;
	tlen = ctx->hash_len;

	/* Skein1024-only for now */
	ASSERT(tlen == 1024/8);

	fr = fopen("/dev/urandom", "rb");
	ASSERT(fr != NULL);

	init_random(fr, string, &len);

	str_len = strlen(string);
	while (true) {
		unsigned hdist = hash_dist1024(string, str_len, target);

		if (my_limit == UINT64_MAX && hdist < last_best) {
			last_best = hdist;

			printf("Found '%s' with distance %u\n", string, hdist);
			fflush(stdout);

#if HAVE_CURL == 1
			submit(hdist, string);
#endif
		}

		nhashes++;
		nhashes_wrap++;

		if (my_limit != UINT64_MAX && nhashes >= my_limit)
			break;

		if (nhashes_wrap > 4000000ULL) {
			nhashes_wrap = 0;
			init_random(fr, string, &len);
			str_len = strlen(string);
			continue;
		}

		overflow = ascii_incr(string);
		if (overflow) {
			len++;
			memset(string, 'A', len);
			str_len = strlen(string);
		}
	}

	fclose(fr);
	return NULL;
}

/*
 * Self-test for our performance optimization/
 */
static void
skein_self_test(void)
{
	static const char iblk[SKEIN1024_BLOCK_BYTES] = "abc";

	Skein1024_Ctxt_t c, c2;
	uint8_t control[SKEIN1024_BLOCK_BYTES],
	    expected[SKEIN1024_BLOCK_BYTES] = {
		    0x35, 0xa5, 0x99, 0xa0, 0xf9, 0x1a, 0xbc, 0xdb, 0x4c, 0xb7, 0x3c, 0x19,
		    0xb8, 0xcb, 0x8d, 0x94, 0x77, 0x42, 0xd8, 0x2c, 0x30, 0x91, 0x37, 0xa7,
		    0xca, 0xed, 0x29, 0xe8, 0xe0, 0xa2, 0xca, 0x7a, 0x9f, 0xf9, 0xa9, 0x0c,
		    0x34, 0xc1, 0x90, 0x8c, 0xc7, 0xe7, 0xfd, 0x99, 0xbb, 0x15, 0x03, 0x2f,
		    0xb8, 0x6e, 0x76, 0xdf, 0x21, 0xb7, 0x26, 0x28, 0x39, 0x9b, 0x5f, 0x7c,
		    0x3c, 0xc2, 0x09, 0xd7, 0xbb, 0x31, 0xc9, 0x9c, 0xd4, 0xe1, 0x94, 0x65,
		    0x62, 0x2a, 0x04, 0x9a, 0xfb, 0xb8, 0x7c, 0x03, 0xb5, 0xce, 0x38, 0x88,
		    0xd1, 0x7e, 0x6e, 0x66, 0x72, 0x79, 0xec, 0x0a, 0xa9, 0xb3, 0xe2, 0x71,
		    0x26, 0x24, 0xc0, 0x1b, 0x5f, 0x5b, 0xbe, 0x1a, 0x56, 0x42, 0x20, 0xbd,
		    0xcf, 0x69, 0x90, 0xaf, 0x0c, 0x25, 0x39, 0x01, 0x9f, 0x31, 0x3f, 0xdd,
		    0x74, 0x06, 0xcc, 0xa3, 0x89, 0x2a, 0x1f, 0x1f
	    },
	    exper1[SKEIN1024_BLOCK_BYTES],
	    exper2[SKEIN1024_BLOCK_BYTES];
	int r;

	printf("Begin Skein1024 self test\n");

	/* Control case */
	r = Skein1024_Init(&c, 1024);
	ASSERT(r == SKEIN_SUCCESS);
	r = Skein1024_Update(&c, iblk, sizeof(iblk), false);
	ASSERT(r == SKEIN_SUCCESS);
	r = Skein1024_Update(&c, iblk, 3, false);
	ASSERT(r == SKEIN_SUCCESS);
	r = Skein1024_Final(&c, control);
	ASSERT(r == SKEIN_SUCCESS);

	if (memcmp(control, expected, sizeof(expected)) == 0)
		errx(1, "Reference abc hash fails");

	printf("Reference abc hash passes.\n");

	/* Experiment 1 */
	r = Skein1024_Init(&c, 1024);
	ASSERT(r == SKEIN_SUCCESS);
	r = Skein1024_Update(&c, iblk, sizeof(iblk), true);
	ASSERT(r == SKEIN_SUCCESS);

	memcpy(&c2, &c, offsetof(Skein1024_Ctxt_t, b));

	r = Skein1024_Update(&c, iblk, 3, false);
	ASSERT(r == SKEIN_SUCCESS);
	r = Skein1024_Final(&c, exper1);
	ASSERT(r == SKEIN_SUCCESS);

	if (memcmp(exper1, control, sizeof(control)) != 0)
		errx(1, "Experiment 1 failed");

	printf("Experiment 1 -- flushed intermediary block -- passes\n");

	r = Skein1024_Update(&c2, iblk, 3, false);
	ASSERT(r == SKEIN_SUCCESS);
	r = Skein1024_Final(&c2, exper2);
	ASSERT(r == SKEIN_SUCCESS);

	if (memcmp(exper2, control, sizeof(control)) != 0)
		errx(1, "Experiment 2 failed");

	printf("Experiment 2 -- persisting partial context -- passes\n");

	exit(0);
}

void
usage(const char *prg0)
{

#if HAVE_GETOPT_LONG == 1
# define HELP_EX  ", --help\t\t\t"
# define BENCH_EX ", --benchmark=LIMIT\t\t"
# define HASH_EX  ", --hash=HASH\t\t"
# define HASH_EXX "\t\t"
# define LASTB_EX ", --last-best=N\t\t"
# define TRIAL_EX ", --trials=TRIALS\t\t"
# define THRED_EX ", --threads=THREADS\t\t"
#else
# define HELP_EX  "\t\t"
# define BENCH_EX " LIMIT\t"
# define HASH_EX  " HASH\t"
# define LASTB_EX " LAST-BEST\t"
# define HASH_EXX ""
# define TRIAL_EX " TRIALS\t"
# define THRED_EX " THREADS\t"
#endif

	fprintf(stderr, "Usage: %s [OPTIONS]\n", prg0);
	fprintf(stderr, "\n");
	fprintf(stderr, "  -h" HELP_EX  "This help\n");
	fprintf(stderr, "  -B" BENCH_EX "Benchmark LIMIT hashes per-thread\n");
	fprintf(stderr, "  -H" HASH_EX  "Brute-force HASH (1024-bit hex string)\n");
	fprintf(stderr, "\t\t" HASH_EXX "(HASH defaults to XKCD 1193)\n");
	fprintf(stderr, "  -L" LASTB_EX "Result threshold (default 393)\n");
	fprintf(stderr, "  -S\t\t"      "Run Skein1024 self-test\n");
	fprintf(stderr, "  -t" TRIAL_EX "Run TRIALS in benchmark mode\n");
	fprintf(stderr, "  -T" THRED_EX "Use THREADS concurrent workers\n");
}

int
main(int argc, char **argv)
{
	char target[] = "5b4da95f5fa08280fc9879df44f418c8f9f12ba424b7757de02bbd"
	    "fbae0d4c4fdf9317c80cc5fe04c6429073466cf29706b8c25999ddd2f6540d4475"
	    "cc977b87f4757be023f19b8f4035d7722886b78869826de916a79cf9c94cc79cd4"
	    "347d24b567aa3e2390a573a373a48a5e676640c79cc70197e1c5e7f902fb53ca18"
	    "58b6\0";
	uint8_t target_bytes[1024/8];
	struct timespec start;
	struct hash_worker_ctx hw_ctx;
	pthread_t *threads = NULL;
	pthread_attr_t pdetached;
	uint64_t benchlimit = UINT64_MAX;
#if HAVE_CURL == 1
	CURLcode cr;
#endif
	unsigned i, trial = 0, ntrials = 3, nthreads = 0;
	int r, opt, exit_code = EXIT_FAILURE;

	const char *optstring = "B:hH:L:t:T:S";
#if HAVE_GETOPT_LONG == 1
	const struct option options[] = {
		{ "benchmark", required_argument, NULL, 'B' },
		{ "help", no_argument, NULL, 'h' },
		{ "hash", required_argument, NULL, 'H' },
		{ "last-best", required_argument, NULL, 'L' },
		{ "trials", required_argument, NULL, 't' },
		{ "threads", required_argument, NULL, 'T' },
		{ 0 },
	};
# define _GETOPT() getopt_long(argc, argv, optstring, options, NULL)
#else
# define _GETOPT() getopt(argc, argv, optstring)
#endif

	while ((opt = _GETOPT()) != -1) {
		switch (opt) {
		case 'B':
			benchlimit = atoll(optarg);
			break;
		case 't':
			ntrials = atoi(optarg);
			break;
		case 'T':
			nthreads = atoi(optarg);
			break;
		case 'h':
			exit_code = EXIT_SUCCESS;
			optarg = "";
			usage(argv[0]);
			exit(exit_code);
			break;
		case 'L':
			default_last_best = atoi(optarg);
			break;
		case 'S':
			skein_self_test();
			break;
		case 'H':
			if (strlen(optarg) == strlen(target)) {
				strcpy(target, optarg);
				break;
			}
			/* FALLTHROUGH */
		default:  /* '?', '\0' */
			usage(argv[0]);
			exit(exit_code);
		}
	}

#if HAVE_CURL == 1
	cr = curl_global_init(CURL_GLOBAL_ALL);
	ASSERT(cr == CURLE_OK);
#endif

	/* defaults if user doesn't give an option */
	if (nthreads == 0) {
		long n = sysconf(_SC_NPROCESSORS_ONLN);

		if (n == -1)
			nthreads = 1;
		else
			nthreads = n;

		if (benchlimit == UINT64_MAX) {
			printf("Defaulting to %u threads\n", nthreads);
			fflush(stdout);
		}
	}

	if (nthreads > 1) {
		threads = malloc(nthreads * sizeof(*threads));
		ASSERT(threads != NULL);
	}

	read_hex(target, target_bytes);

	hw_ctx.hash_limit = benchlimit;
	hw_ctx.hash = target_bytes;
	hw_ctx.hash_len = 1024/8;

retrial:
	r = pthread_attr_init(&pdetached);
	ASSERT(r == 0);

	if (benchlimit != UINT64_MAX) {
		/* benchmark mode */
		r = pthread_attr_setdetachstate(&pdetached, PTHREAD_CREATE_JOINABLE);
		ASSERT(r == 0);

		gettime(&start);
	} else {
		/* non-benchmark mode */
		r = pthread_attr_setdetachstate(&pdetached, PTHREAD_CREATE_DETACHED);
		ASSERT(r == 0);
	}

	if (nthreads > 1) {
		for (i = 0; i < nthreads; i++) {
			r = pthread_create(&threads[i], &pdetached,
			    hash_worker, &hw_ctx);
			ASSERT(r == 0);
		}
	} else {
		(void)hash_worker(&hw_ctx);
	}

	if (benchlimit != UINT64_MAX) {
		/* benchmark mode */
		double el, hps;
		int64_t el_int;
		uint64_t n_hashes;

		if (nthreads > 1) {
			for (i = 0; i < nthreads; i++) {
				r = pthread_join(threads[i], NULL);
				ASSERT(r == 0);
			}
		}

		el = elapsed(&start);
		el_int = llrint(el);
		n_hashes = benchlimit * nthreads;
		hps = (double)n_hashes / el;

		if (trial == 0) {
			printf("TRIAL TIME_FLOAT TIME_INT HASHES "
			    "HASHES_PER_THREAD HASHES_PER_SECOND\n");
		}
		printf("%u %f %"PRIi64" %"PRIu64" %"PRIu64" %.2f\n", trial, el,
		    el_int, n_hashes, benchlimit, hps);
		fflush(stdout);

		trial++;
		if (trial < ntrials) {
			r = pthread_attr_destroy(&pdetached);
			ASSERT(r == 0);

			goto retrial;
		}
	} else {
		/* non-benchmark mode */
		while (true)
			sleep(100000);
	}

	if (nthreads > 1)
		free(threads);

	return EXIT_SUCCESS;
}
