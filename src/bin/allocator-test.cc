#include <imque/ipc/shared_memory.hh>
#include <imque/allocator/variable_allocator.hh>

#include <iostream>
#include <string>
#include <vector>
#include <string.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>

/*
 * パラメータ:
 * - プロセス数:
 * - 各プロセスのスレッド数:
 * - ループ数
 * - 割当サイズの幅(min .. max)
 * - interval
 * - アロケート方法(malloc or variable_allocator or block_allocator)
 */

struct Parameter {
  std::string method; // "variable" | "fix" | "malloc"
  int process_count;
  int thread_count;
  int loop_count;
  int max_hold_micro_sec;
  int alloc_size_min;
  int alloc_size_max;
};

void child_start(const Parameter& param) {
  sleep(3);
}

int main(int argc, char** argv) {
  if(argc != 8) {
    std::cerr << "Usage: allocator-test ALLOCATION_METHOD(variable|fix|malloc) PROCESS_COUNT PER_PROCESS_THREAD_COUNT LOOP_COUNT MAX_HOLD_TIME(micro sec) ALLOC_SIZE_MIN ALLOC_SIZE_MAX" << std::endl;
    return 1;
  }

  Parameter param = {
    argv[1],
    atoi(argv[2]),
    atoi(argv[3]),
    atoi(argv[4]),
    atoi(argv[5]),
    atoi(argv[6]),
    atoi(argv[7])
  };

  if(param.method != "variable" &&
     param.method != "fix" &&
     param.method != "malloc") {
    std::cerr << "Usage: allocator-test ALLOCATION_METHOD(variable|fix|malloc) PROCESS_COUNT PER_PROCESS_THREAD_COUNT LOOP_COUNT MAX_HOLD_TIME(micro sec) ALLOC_SIZE_MIN ALLOC_SIZE_MAX" << std::endl;    
    return 1;
  }

  std::vector<pid_t> children(param.process_count);
  for(int i=0; i < param.process_count; i++) {
    children[i] = fork();
    switch(children[i]) {
    case 0:
      child_start(param);
      return 0;
    case -1:
      std::cerr << "ERROR: fork() failed: " << strerror(errno) << std::endl;
      return 1;
    }
  }
  
  for(int i=0; i < param.process_count; i++) {
    waitpid(children[i], NULL, 0);
  }

  return 0;
}

/*
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
  }
  std::cout << "# exit: " << getpid() << std::endl;
}


int main() {
  pid_t children[CHILD_NUM];

  imque::ipc::SharedMemory mm(1024*CHILD_NUM);
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
*/
