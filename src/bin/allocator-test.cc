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
#include <inttypes.h>

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
  int shm_size;
};

// TODO: マルチスレッド対応
// TODO: 各種統計値の取得
template<class Allocator>
void child_start(Allocator& alc, const Parameter& param) {
  srand(time(NULL) + getpid());
  
  int size_range = param.alloc_size_max-param.alloc_size_min;
  for(int i=0; i < param.loop_count; i++) {
    uint32_t size = static_cast<uint32_t>((rand() % size_range) + param.alloc_size_min);
    typename Allocator::DESCRIPTOR_TYPE md = alc.allocate(size);
    usleep(rand() % param.max_hold_micro_sec);
    
    if(md != 0) {
      memset(alc.template ptr<void>(md), rand()%0x100, size);
      alc.release(md); // TODO: リリース失敗数のカウント
    }
  }
}

template<class Allocator>
void parent_start(Allocator& alc, const Parameter& param) {
  std::vector<pid_t> children(param.process_count);
  
  for(int i=0; i < param.process_count; i++) {
    children[i] = fork();
    switch(children[i]) {
    case 0:
      child_start(alc, param);
      return;
    case -1:
      std::cerr << "ERROR: fork() failed: " << strerror(errno) << std::endl;
      return;
    }
  }
  
  for(int i=0; i < param.process_count; i++) {
    // TODO: 正常終了か異常終了(シグナル)かなどの情報を収集
    waitpid(children[i], NULL, 0);
  }
}

class MallocAllocator {
public:
  typedef void* DESCRIPTOR_TYPE;

  DESCRIPTOR_TYPE allocate(uint32_t size) {
    return malloc(size);
  }

  bool release(DESCRIPTOR_TYPE descriptor) {
    free(descriptor);
    return true;
  }

  template<typename T>
  T* ptr(DESCRIPTOR_TYPE descriptor) { return reinterpret_cast<T*>(descriptor); }
};

int main(int argc, char** argv) {
  if(argc != 9) {
  usage:
    std::cerr << "Usage: allocator-test ALLOCATION_METHOD(variable|fix|malloc) PROCESS_COUNT PER_PROCESS_THREAD_COUNT LOOP_COUNT MAX_HOLD_TIME(micro sec) ALLOC_SIZE_MIN ALLOC_SIZE_MAX SHM_SIZE" << std::endl;
    return 1;
  }

  Parameter param = {
    argv[1],
    atoi(argv[2]),
    atoi(argv[3]),
    atoi(argv[4]),
    atoi(argv[5]),
    atoi(argv[6]),
    atoi(argv[7]),
    atoi(argv[8])
  };

  imque::ipc::SharedMemory shm(param.shm_size);
  if(! shm) {
    std::cerr << "[ERROR] shared memory initialization failed" << std::endl;
    return 1;
  }

  if(param.method == "variable") {
    imque::allocator::VariableAllocator alc(shm.ptr<void>(), shm.size());
    if(! alc) {
      std::cerr << "[ERROR] allocator initialization failed" << std::endl;
      return 1;
    }
    alc.init();
    parent_start(alc, param);
  } else if (param.method == "fix") {
    // TODO:
  } else if (param.method == "malloc") {
    MallocAllocator alc;
    parent_start(alc, param);
  } else {
    goto usage;
  }
  
  return 0;
}
