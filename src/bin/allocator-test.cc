#include <imque/ipc/shared_memory.hh>
#include <imque/allocator/variable_allocator.hh>
#include <imque/allocator/fixed_allocator.hh>

#include <iostream>
#include <string>
#include <vector>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <limits.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <inttypes.h>
#include <time.h>
#include <signal.h>
#include <sys/time.h>

// TODO: nice値変動パラメータ追加、計時間隔とか
struct Parameter {
  std::string method; // "variable" | "fixed" | "malloc"
  int process_count;
  int loop_count;
  int max_hold_micro_sec;
  int alloc_size_min;
  int alloc_size_max;
  int shm_size;
  int kill_num;
};

class NanoTimer {
public:
  NanoTimer() {
    gettimeofday(&t, NULL);
  }

  long elapsed() {
    timeval now;
    gettimeofday(&now, NULL);
    return ns(now) - ns(t);
  }

  long ns(const timeval& ts) {
    return static_cast<long>(static_cast<long long>(ts.tv_sec)*1000*1000*1000 + ts.tv_usec*1000);
  }

private:
  timeval t;
};

struct Stat {
  Stat() : count(0), min(0), max(INT_MAX), total(0) {
  }
  
  void add(int val) {
    count++;
    if(min < val) min = val;
    if(max > val) max = val;
    total += val;
  }
  
  int avg() const {
    return static_cast<int>(total / count);
  }

  int count;
  int min;
  int max;
  long long total;
};

class MallocAllocator {
public:
  void* allocate(uint32_t size) {
    return malloc(size);
  }

  bool release(void* descriptor) {
    free(descriptor);
    return true;
  }

  template<typename T>
  T* ptr(void* descriptor) { return reinterpret_cast<T*>(descriptor); }
};

template<typename T>
struct Descriptor {};
template<>
struct Descriptor<imque::allocator::VariableAllocator> {
  typedef uint32_t TYPE;
};
template<>
struct Descriptor<imque::allocator::FixedAllocator> {
  typedef uint32_t TYPE;
};
template<>
struct Descriptor<MallocAllocator> {
  typedef void* TYPE;
};


template<class Allocator>
void child_start(Allocator& alc, const Parameter& param) {
  srand(time(NULL) + getpid());
  
  Stat alloc_st;
  Stat release_st;
  int alloc_ok_count = 0;
  int release_ok_count = 0;
  int size_range = param.alloc_size_max-param.alloc_size_min;
  for(int i=0; i < param.loop_count; i++) {
    uint32_t size = static_cast<uint32_t>((rand() % size_range) + param.alloc_size_min);

    NanoTimer t1;
    //typename Allocator::DESCRIPTOR_TYPE md = alc.allocate(size);
    typename Descriptor<Allocator>::TYPE md = alc.allocate(size);
    alloc_st.add(t1.elapsed());
    alloc_ok_count += md==0 ? 0 : 1;
    
    if(param.max_hold_micro_sec)
      usleep(rand() % param.max_hold_micro_sec);
    
    if(md != 0) {
      memset(alc.template ptr<void>(md), rand()%0x100, size);
      
      NanoTimer t2;
      bool ok = alc.release(md); 
      release_st.add(t2.elapsed());
      release_ok_count += ok ? 1 : 0;
    }
  }

  std::cout << "#[" << getpid() << "] C: " 
            << "a_ok=" << alloc_ok_count << ", "
            << "r_ok=" << release_ok_count << ", "
            << "a_avg=" << alloc_st.avg() << ", "
            << "r_avg=" << release_st.avg() 
            << std::endl;
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
  
  srand(time(NULL) + getpid());
  for(int i=0; i < param.kill_num; i++) {
    kill(children[rand() % children.size()], 9);
    usleep(rand() % 1000);
  }

  int exit_num=0;
  int signal_num=0;
  int unknown_num=0;
  for(int i=0; i < param.process_count; i++) {
    int status;
    waitpid(children[i], &status, 0);
    if(WIFEXITED(status)) {
      exit_num++;
    } else if(WIFSIGNALED(status)) {
      signal_num++;
    } else {
      unknown_num++;
    }
  }

  std::cout << "#[" << getpid() << "] P: " 
            << "exit=" << exit_num << ", " 
            << "signal=" << signal_num << ", "
            << "unknown=" << unknown_num << std::endl;
}


int main(int argc, char** argv) {
  if(argc != 9) {
  usage:
    std::cerr << "Usage: allocator-test ALLOCATION_METHOD(variable|fixed|malloc) PROCESS_COUNT LOOP_COUNT MAX_HOLD_TIME(μs) ALLOC_SIZE_MIN ALLOC_SIZE_MAX SHM_SIZE KILL_NUM" << std::endl;
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
  } else if (param.method == "fixed") {
    imque::allocator::FixedAllocator alc(shm.ptr<void>(), shm.size());
    if(! alc) {
      std::cerr << "[ERROR] allocator initialization failed" << std::endl;
      return 1;
    }
    alc.init();
    parent_start(alc, param);
  } else if (param.method == "malloc") {
    MallocAllocator alc;
    parent_start(alc, param);
  } else {
    goto usage;
  }
  
  return 0;
}
