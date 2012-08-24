CPPFLAGS+=-Wall
CPPFLAGS+=-Werror

all: msgque-test allocator-test

allocator-test:
	mkdir -p bin
	g++ -Iinclude ${CPPFLAGS} -O2 -o bin/allocator-test src/bin/allocator-test.cc -lrt

msgque-test:
	mkdir -p bin
	g++ -Iinclude ${CPPFLAGS} -O2 -o bin/msgque-test src/bin/msgque-test.cc -lrt


mac: msgque-test-mac allocator-test-mac

allocator-test-mac:
	mkdir -p bin
	g++ -Iinclude ${CPPFLAGS} -O2 -o bin/allocator-test src/bin/allocator-test.cc

msgque-test-mac:
	mkdir -p bin
	g++ -Iinclude ${CPPFLAGS} -O2 -o bin/msgque-test src/bin/msgque-test.cc
