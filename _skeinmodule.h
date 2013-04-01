/*
    _skeinmodule.h
    Copyright 2008, 2009, 2010 Hagen Fürstenau <hagen@zhuliguan.net>
    Some of this code evolved from an implementation by Doug Whiting,
    which was released to the public domain.

    This file is part of PySkein.

    PySkein is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#define  SKEIN_256_STATE_WORDS  (4)
#define  SKEIN_512_STATE_WORDS  (8)
#define  SKEIN_1024_STATE_WORDS (16)
#define  SKEIN_MAX_STATE_WORDS  SKEIN_1024_STATE_WORDS

#define  SKEIN_256_BLOCK_BYTES  (8*SKEIN_256_STATE_WORDS)
#define  SKEIN_512_BLOCK_BYTES  (8*SKEIN_512_STATE_WORDS)
#define  SKEIN_1024_BLOCK_BYTES (8*SKEIN_1024_STATE_WORDS)
#define  SKEIN_MAX_BLOCK_BYTES  SKEIN_1024_BLOCK_BYTES


/* Python object definitions */

static PyTypeObject skeinType;

struct _skein_state_s;

typedef int (*processor_t)(struct _skein_state_s *,
                           const u08b_t *, size_t, u08b_t);

typedef struct _skein_state_s {
    /* chaining variables and tweak value */
    u64b_t                X[SKEIN_MAX_STATE_WORDS+1];
    u64b_t                T[3];
    processor_t           block_processor;

    /* tree hashing state */
    u64b_t                tree_blocks;
    u64b_t                remaining_tree_blocks;
    u08b_t                remaining_tree_levels;
    struct _skein_state_s *next_tree_level;
} skein_state_t;

typedef struct {
    PyObject_HEAD

    skein_state_t state;

    /* count bytes hashed so far */
    u64b_t        hashed_bytes;

    /* state and digest sizes */
    u64b_t        digestBits;
    u08b_t        stateBytes;

    /* unprocessed bytes buffer */
    u08b_t        b[SKEIN_MAX_BLOCK_BYTES];
    u08b_t        bCnt;
    u08b_t        missing_bits;
        /* number of bits missing from the last byte in b */
} skeinObject;


typedef struct {
    PyObject_HEAD

    void(*encryptor)(u64b_t *, u64b_t *, const u64b_t *, u64b_t *, int);
    void(*decryptor)(u64b_t *, u64b_t *, const u64b_t *, u64b_t *);
    u64b_t kw[SKEIN_MAX_STATE_WORDS+4];  /* precomputed key schedule */
    size_t blockBytes;
} threefishObject;


/* abbreviations */

#define HASH_INIT(sk, type) \
{ \
    sk->state.T[0] = 0; \
    sk->state.T[1] = SKEIN_T1_FLAG_FIRST | ((u64b_t)SKEIN_BLOCK_TYPE_##type<<56); \
    sk->bCnt = 0; \
}

#define HASH_FINALIZE(sk) \
{ \
    sk->state.T[1] |= SKEIN_T1_FLAG_FINAL; \
    if (sk->missing_bits) \
        sk->state.T[1] |= SKEIN_T1_FLAG_BITPAD; \
    if (sk->bCnt < sk->stateBytes) \
        memset(&sk->b[sk->bCnt], 0, sk->stateBytes - sk->bCnt); \
    sk->state.block_processor(&sk->state, sk->b, 1, sk->bCnt); \
}

#define HASH_BLOCK(sk, p, len, type) \
{ \
    sk->state.T[0] = 0; \
    sk->state.T[1] = SKEIN_T1_FLAG_FIRST | SKEIN_T1_FLAG_FINAL | \
                     ((u64b_t)SKEIN_BLOCK_TYPE_##type<<56); \
    if (!sk->state.block_processor(&sk->state, p, 1, len)) \
        goto error; \
}

#define HASH_BLOCKS(sk, p, len, type) \
{ \
    HASH_INIT(sk, type); \
    if (!hash_bytes(sk, p, len)) \
        goto error; \
    HASH_FINALIZE(sk); \
}


/* numerical constants */

#define U64B_CONST(high, low)  (((u64b_t)high << 32) | (u64b_t)low)
#define SKEIN_KS_PARITY        U64B_CONST(0x1BD11BDA, 0xA9FC1A22)
#define SKEIN_T1_FLAG_FIRST    U64B_CONST(0x40000000, 0x00000000)
#define SKEIN_T1_FLAG_FINAL    U64B_CONST(0x80000000, 0x00000000)
#define SKEIN_T1_FLAG_BITPAD   U64B_CONST(0x00800000, 0x00000000)
#define SKEIN_T1_POS_LEVEL     (112-64)

#define SKEIN_BLOCK_TYPE_KEY    0 /* key, for MAC and KDF */
#define SKEIN_BLOCK_TYPE_CFG    4 /* configuration block */
#define SKEIN_BLOCK_TYPE_PERS   8 /* personalization string */
#define SKEIN_BLOCK_TYPE_PK    12 /* public key */
#define SKEIN_BLOCK_TYPE_KID   16 /* key identifier for KDF */
#define SKEIN_BLOCK_TYPE_NONCE 20 /* nonce */
#define SKEIN_BLOCK_TYPE_MSG   48 /* message processing */
#define SKEIN_BLOCK_TYPE_OUT   63 /* output stage */


/* conversions between bytes and 64-bit words */

#ifdef WORDS_BIGENDIAN  /* compatible with big endian platforms, but slow */
void WORDS_TO_BYTES(u08b_t *dst, const u64b_t *src, size_t bCnt) {
    size_t n;

    for (n=0;n<bCnt;n++)
        dst[n] = (u08b_t) (src[n>>3] >> (8*(n&7)));
}

void BYTES_TO_WORDS(u64b_t *dst, const u08b_t *src, size_t wCnt) {
    size_t n;

    for (n=0;n<8*wCnt;n+=8)
        dst[n/8] = (((u64b_t) src[n  ])      ) +
                   (((u64b_t) src[n+1]) <<  8) +
                   (((u64b_t) src[n+2]) << 16) +
                   (((u64b_t) src[n+3]) << 24) +
                   (((u64b_t) src[n+4]) << 32) +
                   (((u64b_t) src[n+5]) << 40) +
                   (((u64b_t) src[n+6]) << 48) +
                   (((u64b_t) src[n+7]) << 56) ;
}
#else  /* fast versions for little endian platforms */
#define WORDS_TO_BYTES(dst08, src64, bCnt) memcpy(dst08, src64, bCnt)
#define BYTES_TO_WORDS(dst64, src08, wCnt) memcpy(dst64, src08, 8*(wCnt))
#endif

