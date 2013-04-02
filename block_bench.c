#include <stdbool.h>
#include <ctype.h>
#include <endian.h>
#include <pthread.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include <sys/queue.h>


#include "skein.h"
/*
 * To bench C implementation, change to '1'; to bench ASM, compile against
 * skein_block_<whatever>.s
 *
 * The problem with the ASM implementation is that it produces incorrect
 * hashes. Maybe pre-1.3?
 */
#if 1
# define SKEIN_UNROLL_1024 10
# include "skein_block.c"
#endif
#include "skein.c"

inline void
ASSERT(intptr_t i)
{

	if (!i)
		abort();
}

inline void
hash(const uint8_t *trial, size_t len, void *out)
{
	Skein1024_Ctxt_t c;
	int r;

	r = Skein1024_Init(&c, 1024);
	ASSERT(r == SKEIN_SUCCESS);

	r = Skein1024_Update(&c, (void*)trial, len);
	ASSERT(r == SKEIN_SUCCESS);

	r = Skein1024_Final(&c, out);
	ASSERT(r == SKEIN_SUCCESS);
}

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

int
main(void)
{
	unsigned len = 1;
	char status[4096] = { 0 };
	bool overflow;
	const uint64_t NTRIALS = 1000000;
	int rc;
	struct timespec begin, end;
	uint8_t trhash[1024/8];

	status[0] = 'A';

	for (unsigned i = 0; i < 5; i++) {
		double hps = (double)NTRIALS,
		       seconds;

		rc = clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &begin);
		ASSERT(rc == 0);

		for (uint64_t t = 0; t < NTRIALS; t++) {
			hash((uint8_t*)status, len, trhash);

			overflow = ascii_incr(status);
			if (overflow) {
				len++;
				if (len >= sizeof status)
					abort();
				memset(status, 'A', len);
			}
		}

		rc = clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &end);
		ASSERT(rc == 0);

		seconds = ((double)end.tv_sec - begin.tv_sec) +
		    (1./1000000000.) * ((double)end.tv_nsec - begin.tv_nsec);

		hps /= seconds;
		printf("Trial %u Skeins/sec: %.02f\n", i, hps);
	}

	hash((void*)"HnFZBl", 6, trhash);
	printf("Hash(HnFZBl) = ");
	dump_hex(trhash, sizeof trhash);

	return 0;
}
