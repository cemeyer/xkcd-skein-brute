EXTRAFLAGS =
LTOFLAGS = -flto
OPTFLAGS = -O3 -march=native -mtune=native
SKEINFLAGS = -DSKEIN_LOOP=995 -DSKEIN_ASM -DSKEIN_USE_ASM=1792
ACFLAGS = -DELF -Wa,--noexecstack -Wa,--defsym,SKEIN_LOOP=0
LIBFLAGS = -lcurl

LIBS = ${LIBFLAGS} -lrt -lm
MYFLAGS = -std=gnu11 -g -pthread -Wall -Wextra -fno-strict-aliasing \
		-Wno-strict-aliasing ${EXTRAFLAGS} ${OPTFLAGS} ${LTOFLAGS} \
		-Wno-missing-field-initializers ${SKEINFLAGS} ${ACFLAGS}

all: main

main: main.c skein.c skein_block_asm.s
	rm -f "${@}.gcda"
	$(CC) ${MYFLAGS} -fprofile-generate $< skein_block_asm.s -o $@ ${LIBS}
	# --benchmark, --trials, --threads
	./main -B 2000000 -t 1 -T 1
	$(CC) ${MYFLAGS} -fprofile-use      $< skein_block_asm.s -o $@ ${LIBS}

clean:
	rm -f main *.gcda
