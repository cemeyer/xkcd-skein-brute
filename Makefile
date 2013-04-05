OPTFLAGS = -O3 -march=native -mtune=native

main: main.c skein.c
	gcc -std=gnu99 -pthread -Wall -Wextra \
		-fno-strict-aliasing -Wno-strict-aliasing \
		-lcurl -lrt \
		${OPTFLAGS} \
		$< \
		-o $@

clean:
	rm -f main
