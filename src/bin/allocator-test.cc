#include <iostream>
//#include <ipc_allocator.hh>
#include <ipc_mmap.hh>

#include <imque/allocator.hh>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>

typedef imque::Allocator allocator;

const int CHILD_NUM = 500;//0;
const int LOOP_COUNT = 1000;//0;

void sigsegv_handler(int sig) {
  std::cerr << "#" << getpid() << ":" << sig << std::endl;
  exit(1);
}

void child_start(allocator& alc) {
  std::cout << "# child: " << getpid() << std::endl;
  srand(time(NULL));

  for(int i=0; i < LOOP_COUNT; i++) {
    unsigned size = (rand() % 1024) + 1;

    uint32_t idx = alc.allocate(size);
    //std::cout << "[" << getpid() << "] " << size << " => " << idx << std::endl;
    usleep(rand() % 200); 
    assert(alc.release(idx));

    /*
    char *buf = new char[size];
    usleep(rand() % 200);
    delete [] buf;
    */  
  }
  std::cout << "# exit: " << getpid() << std::endl;
}


int main() {
  mmap_t mm(1024*CHILD_NUM);
  if(! mm) {
    std::cerr << "mmap() failed" << std::endl;
    return 1;
  }
  allocator alc(mm.ptr<void>(), mm.size());
  alc.init();
  signal(SIGSEGV, sigsegv_handler);

  for(int i=0; i < CHILD_NUM; i++) {
    if(fork() == 0) {
      child_start(alc);
      return 0;
    }
  }

  //child_start(alc);

  waitid(P_ALL, 0, NULL, WEXITED);
  // alc.allocate(10);
  return 0;
}
