#include <iostream>
#include <ipc_allocator.hh>
#include <ipc_mmap.hh>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>

const int CHILD_NUM = 100;
const int LOOP_COUNT = 200;

void sigsegv_handler(int sig) {
  std::cerr << "#" << getpid() << ":" << sig << std::endl;
  exit(1);
}

void child_start(allocator& alc) {
  std::cout << "# child: " << getpid() << std::endl;
  srand(time(NULL));
  
  for(int i=0; i < LOOP_COUNT; i++) {
    unsigned size = (rand() % 1024) + 1;
    unsigned idx = alc.allocate(size);
    std::cout << "[" << getpid() << "] " << size << " => " << idx << std::endl;

    if(idx != 0) {
    //if(idx != 0 && rand() % 10 == 0) {
      usleep(rand() % 100);
      alc.release(idx);
    }
  }
  std::cout << "# exit: " << getpid() << std::endl;
}

int main() {
  mmap_t mm(1024*512*10);
  if(! mm) {
    std::cerr << "mmap() failed" << std::endl;
    return 1;
  }
  
  allocator alc(mm.ptr<void>(), mm.size());
  signal(SIGSEGV, sigsegv_handler);

  for(int i=0; i < CHILD_NUM; i++) {
    if(fork() == 0) {
      child_start(alc);
      return 0;
    }
  }
  //child_start(alc);

  /*
  int i = alc.allocate(307);
  alc.dump();
  alc.release(i);
  alc.dump();
  int j = alc.allocate(297);
  alc.dump();
  alc.release(j);
  alc.dump();
  int k = alc.allocate(837);
  alc.dump();
  alc.release(k);
  */

  /*
  int i = alc.allocate(10);
  int j = alc.allocate(10);
  int k = alc.allocate(10);
  std::cout << "@ " << i << std::endl;
  std::cout << "@ " << j << std::endl;
  std::cout << "@ " << k << std::endl;
  alc.dump();
  alc.release(j);
  alc.dump();
  std::cout << "@ " << alc.allocate(10) << std::endl;
  alc.dump();
  std::cout << "@ " << alc.allocate(10) << std::endl;
  */

  waitid(P_ALL, 0, NULL, WEXITED);
  // alc.allocate(10);
  return 0;
}
