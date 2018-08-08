EXTRAFLAGS =
OPTFLAGS = -O3 -march=native -mtune=native -flto
LIBFLAGS = -lcurl
LIBS = ${LIBFLAGS} -lrt -lm
MYFLAGS = -std=gnu99 -pthread -Wall -Wextra -fno-strict-aliasing \
		-Wno-strict-aliasing ${EXTRAFLAGS} ${OPTFLAGS} \
		-Wno-missing-field-initializers

all: main

main: main.c skein.c
	rm -f "${@}.gcda"
	$(CC) ${MYFLAGS} -fprofile-generate $< -o $@ ${LIBS}
	# --benchmark, --trials, --threads
	./main -B 2000000 -t 1 -T 1
	$(CC) ${MYFLAGS} -fprofile-use      $< -o $@ ${LIBS}

clean:
	rm -f main *.gcda
