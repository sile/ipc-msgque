all: msgque-test block-allocator-test allocator-test mt-malloc-test mt-alloc-test

allocator-test:
	mkdir -p bin
	g++ -Iinclude -O2 -o bin/allocator-test src/bin/allocator-test.cc

block-allocator-test:
	mkdir -p bin
	g++ -Iinclude -O2 -o bin/block-allocator-test src/bin/block-allocator-test.cc

msgque-test:
	mkdir -p bin
	g++ -Iinclude -O2 -o bin/msgque-test src/bin/msgque-test.cc

mt-malloc-test:
	mkdir -p bin
	g++ -Iinclude -O2 -o bin/mt-malloc-test src/bin/mt-malloc-test.cc -lpthread

mt-alloc-test:
	mkdir -p bin
	g++ -Iinclude -O2 -o bin/mt-alloc-test src/bin/mt-alloc-test.cc -lpthread