EXTRAFLAGS =
OPTFLAGS = -O3 -march=native -mtune=native -flto
LIBFLAGS = -lcurl
LIBS = ${LIBFLAGS} -lrt -lm
MYFLAGS = -std=gnu99 -pthread -Wall -Wextra -fno-strict-aliasing \
		-Wno-strict-aliasing ${EXTRAFLAGS} ${OPTFLAGS} \
		-Wno-missing-field-initializers

all: main

main: main.c skein.c
	$(CC) ${MYFLAGS} -fprofile-generate $< -o $@ ${LIBS}
	./main --benchmark 5000000 --trials 1 --threads 1
	$(CC) ${MYFLAGS} -fprofile-use      $< -o $@ ${LIBS}

clean:
	rm -f main *.gcda
