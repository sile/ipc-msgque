all: msgque-test allocator-test

allocator-test:
	mkdir -p bin
	g++ -Iinclude -O2 -o bin/allocator-test src/bin/allocator-test.cc

msgque-test:
	mkdir -p bin
	g++ -Iinclude -O2 -o bin/msgque-test src/bin/msgque-test.cc
