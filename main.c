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


#include <ctype.h>
#include <endian.h>
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
#define MAX_STRING 2048

#ifdef __NO_INLINE__
# define TRY_INLINE
#else
# define TRY_INLINE inline
#endif

/*
 * Globals
 */

#ifdef LOCK_OVERHEAD_DEBUG
uint64_t rlock_taken = 0;
struct timespec g_lock_begin;
#endif

/*
 * For some reason -O3 tries to read waaaaaaay beyond the end of this string.
 * Optimization bug in GCC or Glibc sscanf?
 */
char target[2048] = "5b4da95f5fa08280fc9879df44f418c8f9f12ba424b7757de02bbdfbae"
"0d4c4fdf9317c80cc5fe04c6429073466cf29706b8c25999ddd2f6540d4475cc977b87f4757be0"
"23f19b8f4035d7722886b78869826de916a79cf9c94cc79cd4347d24b567aa3e2390a573a373a4"
"8a5e676640c79cc70197e1c5e7f902fb53ca1858b6\0";
uint8_t target_bytes[/*1024/8*/ 2048];

unsigned rbestdist = 440;
char beststring[MAX_STRING] = { 0 };
pthread_mutex_t rlock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t rcond = PTHREAD_COND_INITIALIZER;
uint64_t t_benchlimit = UINT64_MAX;

struct timespec g_bench_begin;

/*
 * Subs!
 */

TRY_INLINE void
ASSERT(uintptr_t i)
{

	if (i == 0)
		abort();
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

TRY_INLINE double
elapsed(struct timespec *begin)
{
	struct timespec cur;
	double el;
	int r;

	r = clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &cur);
	ASSERT(r == 0);

	el = (double)cur.tv_sec - begin->tv_sec + 0.000000001 *
	    ((double)cur.tv_nsec - begin->tv_nsec);
	return el;
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
	double lps;

	lps = (double)rlock_taken / elapsed(&g_lock_begin);
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

__thread FILE *frand = NULL;
void
init_random(char initvalue[MAX_STRING], unsigned *len_out)
{
	const char *cs = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
	    "abcdefghijklmnopqrstuvwxyz";
	const unsigned cslen = strlen(cs);

	uint64_t rnd[4];
	size_t rd;
	unsigned r, i;

	if (frand == NULL) {
		frand = fopen("/dev/urandom", "rb");
		ASSERT(frand != NULL);
	}

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

void *
submit(void *un)
{
	unsigned best = 384;
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
#endif  /* HAVE_CURL */

void *
hash_worker(void *unused)
{
	char string[MAX_STRING] = { 0 };
	uint8_t loc_target_hash[sizeof(target_bytes)];
	uint64_t nhashes_wrap = 0, nhashes = 0, my_limit;
	size_t str_len;
	unsigned last_best = 4000, len;
	bool overflow;

	(void)unused;

	memcpy(loc_target_hash, target_bytes, sizeof(loc_target_hash));
	my_limit = t_benchlimit;

	init_random(string, &len);

	str_len = strlen(string);
	while (true) {
		unsigned hdist = hash_dist(string, str_len, loc_target_hash);

		if (my_limit == UINT64_MAX && hdist < last_best) {
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
		nhashes_wrap++;

		if (my_limit != UINT64_MAX && nhashes >= my_limit)
			return NULL;

		if (nhashes_wrap > 4000000ULL) {
			init_random(string, &len);
			str_len = strlen(string);
#if 0
			printf("Working skipping to: %s\n", string);
#endif
			nhashes_wrap = 0;
			continue;
		}

		overflow = ascii_incr(string);
		if (overflow) {
			len++;
			memset(string, 'A', len);
			str_len = strlen(string);
		}
	}
}

void
usage(const char *prg0)
{

#if HAVE_GETOPT_LONG == 1
# define HELP_EX  ", --help\t\t\t"
# define BENCH_EX ", --benchmark=LIMIT\t\t"
# define HASH_EX  ", --hash=HASH\t\t"
# define HASH_EXX "\t\t"
# define TRIAL_EX ", --trials=TRIALS\t\t"
# define THRED_EX ", --threads=THREADS\t\t"
#else
# define HELP_EX  "\t\t"
# define BENCH_EX " LIMIT\t"
# define HASH_EX  " HASH\t"
# define HASH_EXX ""
# define TRIAL_EX " TRIALS\t"
# define THRED_EX " THREADS\t"
#endif

	fprintf(stderr, "Usage: %s [OPTIONS]\n", prg0);
	fprintf(stderr, "\n");
	fprintf(stderr, "  -h" HELP_EX  "This help\n");
	fprintf(stderr, "  -B" BENCH_EX "Benchmark LIMIT hashes\n");
	fprintf(stderr, "  -H" HASH_EX  "Brute-force HASH (1024-bit hex string)\n");
	fprintf(stderr, "\t\t" HASH_EXX "(HASH defaults to XKCD 1193)\n");
	fprintf(stderr, "  -t" TRIAL_EX "Run TRIALS in benchmark mode\n");
	fprintf(stderr, "  -T" THRED_EX "Use THREADS concurrent workers\n");
}

int
main(int argc, char **argv)
{
	pthread_attr_t pdetached;
	pthread_t *threads = NULL;
	uint64_t benchlimit = UINT64_MAX;
#if HAVE_CURL == 1
	pthread_t curl_thread;
#endif
	unsigned i, trial = 0, ntrials = 3, nthreads = 0;
	int r, opt, exit_code = EXIT_FAILURE;

	const char *optstring = "hH:B:t:T:";
#if HAVE_GETOPT_LONG == 1
	const struct option options[] = {
		{ "help", no_argument, NULL, 'h' },
		{ "hash", required_argument, NULL, 'H' },
		{ "benchmark", required_argument, NULL, 'B' },
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
			/* FALLTHROUGH */
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

retrial:
#ifdef LOCK_OVERHEAD_DEBUG
	r = clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &g_lock_begin);
	ASSERT(r == 0);
#endif

	r = pthread_attr_init(&pdetached);
	ASSERT(r == 0);

	if (benchlimit != UINT64_MAX) {
		/* benchmark mode */
		t_benchlimit = benchlimit / nthreads;

		r = pthread_attr_setdetachstate(&pdetached, PTHREAD_CREATE_JOINABLE);
		ASSERT(r == 0);

		r = clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &g_bench_begin);
		ASSERT(r == 0);
	} else {
		/* non-benchmark mode */
		r = pthread_attr_setdetachstate(&pdetached, PTHREAD_CREATE_DETACHED);
		ASSERT(r == 0);

#if HAVE_CURL == 1
		r = pthread_create(&curl_thread, &pdetached, submit, NULL);
		ASSERT(r == 0);
#endif
	}

	if (nthreads > 1) {
		for (i = 0; i < nthreads; i++) {
			r = pthread_create(&threads[i], &pdetached, hash_worker, NULL);
			ASSERT(r == 0);
		}
	} else {
		(void)hash_worker(NULL);
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

		el = elapsed(&g_bench_begin);
		el_int = llrint(el);
		n_hashes = t_benchlimit * nthreads;
		hps = (double)n_hashes / el;

		if (trial == 0) {
			printf("TRIAL TIME_FLOAT TIME_INT HASHES "
			    "HASHES_PER_THREAD HASHES_PER_SECOND\n");
		}
		printf("%u %f %"PRIi64" %"PRIu64" %"PRIu64" %.2f\n", trial, el,
		    el_int, n_hashes, t_benchlimit, hps);
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
