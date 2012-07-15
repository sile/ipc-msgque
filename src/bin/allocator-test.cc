#include <iostream>
#include <ipc_allocator.hh>
#include <ipc_mmap.hh>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>

const int CHILD_NUM = 20;
const int LOOP_COUNT = 100;

void child_start(allocator& alc) {
  std::cout << "# child: " << getpid() << std::endl;
  srand(time(NULL));
  
  for(int i=0; i < LOOP_COUNT; i++) {
    unsigned size = (rand() % 1024) + 1;
    unsigned idx = alc.allocate(size);
    std::cout << "[" << getpid() << "] " << size << " => " << idx << std::endl;
    
    usleep(rand() % 100);
    alc.release(idx);
  }
  std::cout << "# exit: " << getpid() << std::endl;
}

int main() {
  mmap_t mm(1024*512);
  if(! mm) {
    std::cerr << "mmap() failed" << std::endl;
    return 1;
  }
  
  allocator alc(mm.ptr<void>(), mm.size());

  for(int i=0; i < CHILD_NUM; i++) {
    if(fork() == 0) {
      child_start(alc);
      return 0;
    }
  }

  waitid(P_ALL, 0, NULL, WEXITED);
  return 0;
}
