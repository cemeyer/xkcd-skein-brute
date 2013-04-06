EXTRAFLAGS =
OPTFLAGS = -O3 -march=native -mtune=native
LIBFLAGS = -lcurl

main: main.c skein.c
	gcc -std=gnu99 -pthread -Wall -Wextra \
		-fno-strict-aliasing -Wno-strict-aliasing \
		-lrt -lm \
		${EXTRAFLAGS} \
		${OPTFLAGS} \
		${LIBFLAGS} \
		$< \
		-o $@

clean:
	rm -f main
