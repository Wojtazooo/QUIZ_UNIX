CC=gcc
CFLAGS= -std=gnu99 -Wall
LDLIBS = -lpthread -lm
all: Quiz signaltest
Quiz:
	${CC} ${CFLAGS} Quiz.c ${LDLIBS} -o Quiz
signaltest:
	${CC} ${CFLAGS} signaltest.c ${LDLIBS} -o signaltest
clean:
	rm Quiz
