all: msgque-test allocator-test

allocator-test:
	mkdir -p bin
	g++ -Iinclude -g -o bin/allocator-test src/bin/allocator-test.cc

msgque-test:

	g++ -Iinclude -O2 -o src/ipc_msgque.o -c src/ipc_msgque.cc
	g++ -Iinclude -O2 -o bin/msgque-test src/ipc_msgque.o src/bin/msgque-test.cc
