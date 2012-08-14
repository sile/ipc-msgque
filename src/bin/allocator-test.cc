#include <iostream>
#include <string.h>
#include <imque/allocator.hh>
#include <imque/ipc/shared_memory.hh>
#include <imque/allocator/variable_allocator.hh>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>

//typedef imque::Allocator allocator;
typedef imque::allocator::VariableAllocator allocator;

const int CHILD_NUM = 500;
const int LOOP_COUNT = 1000;

void sigsegv_handler(int sig) {
  std::cerr << "#" << getpid() << ":" << sig << std::endl;
  exit(1);
}

void child_start(allocator& alc) {
  std::cout << "# child: " << getpid() << std::endl;
  srand(time(NULL) + getpid());

  for(int i=0; i < LOOP_COUNT; i++) {
    unsigned size = (rand() % 1024) + 1;

    uint32_t idx = alc.allocate(size);
    //std::cout << "[" << getpid() << "] " << size << " => " << idx << std::endl;
    if(idx != 0) {
      memset(alc.ptr<char>(idx), rand()%0x100, size);
    } else {
      std::cerr << "# out of memory" << std::endl;
    }
    usleep(rand() % 400); 
    assert(alc.release(idx));

    /*
    char *buf = new char[size];
    memset(buf, rand()%0x100, size);
    usleep(rand() % 400);
    delete [] buf;
    */
  }
  std::cout << "# exit: " << getpid() << std::endl;
}


int main() {
  pid_t children[CHILD_NUM];

  imque::SharedMemory mm(1024*CHILD_NUM);
  if(! mm) {
    std::cerr << "mmap() failed" << std::endl;
    return 1;
  }

  allocator alc(mm.ptr<void>(), mm.size());
  alc.init();
  signal(SIGSEGV, sigsegv_handler);

  for(int i=0; i < CHILD_NUM; i++) {
    pid_t child = fork();
    if(child == 0) {
      child_start(alc);
      return 0;
    }
    children[i] = child;
  }

  //child_start(alc);

  for(int i=0; i < CHILD_NUM; i++) {
    waitpid(children[i], NULL, 0);
  }

  return 0;
}
