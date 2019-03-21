

LDFLAGS +=  -lGLU -lGL -lX11 -lm -lpthread

test: main.c sview.c
	${CC} -Wall -Werror -O2 -o $@ main.c sview.c ${LDFLAGS}
