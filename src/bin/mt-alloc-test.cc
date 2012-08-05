#include <iostream>
#include <string.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>

#include <pthread.h>

#include <imque/allocator.hh>
#include <imque/block_allocator.hh>
#include <imque/shared_memory.hh>

typedef imque::Allocator allocator;

const int CHILD_NUM  = 500;
const int LOOP_COUNT = 1000;

void sigsegv_handler(int sig) {
  std::cerr << "#" << getpid() << ":" << sig << std::endl;
  exit(1);
}

void* child_start(void* data) {
  allocator& alc = *reinterpret_cast<allocator*>(data);
  int id = 10;
  
  std::cout << "# child: " << id << std::endl;
  srand(time(NULL));

  for(int i=0; i < LOOP_COUNT; i++) {
    unsigned size = (rand() % 1024) + 1;

    uint32_t idx = alc.allocate(size);
    if(idx != 0) {
      memset(alc.ptr<char>(idx), rand()%0x100, size);
    }
    usleep(rand() % 400); 
    assert(alc.release(idx));
  }
  std::cout << "# exit: " << id << std::endl;
  return NULL;
}

int main() {
  pthread_t children[CHILD_NUM];
  signal(SIGSEGV, sigsegv_handler);

  imque::SharedMemory mm(1024*CHILD_NUM);
  if(! mm) {
    std::cerr << "mmap() failed" << std::endl;
    return 1;
  }
  allocator alc(mm.ptr<void>(), mm.size());
  alc.init();

  for(int i=0; i < CHILD_NUM; i++) {
    pthread_create(&children[i], NULL, child_start, reinterpret_cast<void*>(&alc));
  }
  for(int i=0; i < CHILD_NUM; i++) {
    pthread_join(children[i], NULL);
  }

  return 0;
}
