EXTRAFLAGS =
OPTFLAGS = -O3 -march=native -mtune=native -flto
LIBFLAGS = -lcurl
FLAGS = -std=gnu99 -pthread -Wall -Wextra -fno-strict-aliasing \
		-Wno-strict-aliasing -lrt -lm ${EXTRAFLAGS} ${OPTFLAGS} \
		-Wno-missing-field-initializers

all: main

main: main.c skein.c
	$(CC) ${FLAGS} ${LIBFLAGS} -fprofile-generate $< -o $@
	./main --benchmark 5000000 --trials 1 --threads 1
	$(CC) ${FLAGS} ${LIBFLAGS} -fprofile-use      $< -o $@

clean:
	rm -f main *.gcda
