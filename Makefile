main: main.c skein.c
	gcc -std=gnu99 -g -pthread -Wall -Wextra -O3 \
		-fno-strict-aliasing -Wno-strict-aliasing \
		-o $@ $<
