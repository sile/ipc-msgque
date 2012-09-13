CPPFLAGS+= -Wall
CPPFLAGS+= -Werror
CPPFLAGS+= -O2

all: sample test

sample: anonymous-sample named-sample

test: allocator-test msgque-test

anonymous-sample:
	g++ -Iinclude ${CPPFLAGS} -o bin/${@} src/bin/${@}.cc

named-sample:
	g++ -Iinclude ${CPPFLAGS} -o bin/${@} src/bin/${@}.cc

allocator-test:
	g++ -Iinclude ${CPPFLAGS} -o bin/${@} src/bin/${@}.cc

msgque-test:
	g++ -Iinclude ${CPPFLAGS} -o bin/${@} src/bin/${@}.cc
