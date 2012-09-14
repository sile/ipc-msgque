#include <imque/ipc/shared_memory.hh>
#include <imque/allocator/variable_allocator.hh>
#include <imque/allocator/fixed_allocator.hh>

#include "../aux/nano_timer.hh"
#include "../aux/stat.hh"

#include <iostream>
#include <string>
#include <vector>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <inttypes.h>
#include <time.h>
#include <signal.h>

struct Parameter {
  std::string method; // "variable" | "fixed" | "malloc"
  int process_count;
  int loop_count;
  int max_nice;
  int max_hold_micro_sec;
  int alloc_size_min;
  int alloc_size_max;
  int shm_size;
  int kill_num;
};

class MallocAllocator {
public:
  void* allocate(uint32_t size) { return malloc(size); }
  bool release(void* descriptor) { free(descriptor); return true; }

  template<typename T>
  T* ptr(void* descriptor) { return reinterpret_cast<T*>(descriptor); }
};

template<typename T> struct Descriptor {};
template<> struct Descriptor<imque::allocator::VariableAllocator> { typedef uint32_t TYPE; };
template<> struct Descriptor<imque::allocator::FixedAllocator>    { typedef uint32_t TYPE; };
template<> struct Descriptor<MallocAllocator>                     { typedef void* TYPE; };

template<class Allocator>
void child_start(Allocator& alc, const Parameter& param) {
  srand(time(NULL) + getpid());
  int new_nice = nice(rand() % (param.max_nice+1));
  std::cout << "#[" << getpid() << "] C START: nice=" << new_nice << std::endl;

  imque::Stat alc_ok_st;
  imque::Stat rls_ok_st;
  imque::Stat alc_ng_st;
  imque::Stat rls_ng_st;
  
  int size_range = param.alloc_size_max - param.alloc_size_min + 1;
  for(int i=0; i < param.loop_count; i++) {
    uint32_t size = static_cast<uint32_t>((rand() % size_range) + param.alloc_size_min);

    imque::NanoTimer t1;
    typename Descriptor<Allocator>::TYPE md = alc.allocate(size);
    md != 0 ? alc_ok_st.add(t1.elapsed()) : alc_ng_st.add(t1.elapsed());
    
    if(param.max_hold_micro_sec)
      usleep(rand() % param.max_hold_micro_sec);
    
    if(md != 0) {
      memset(alc.template ptr<void>(md), rand()%0x100, size);
      
      
      imque::NanoTimer t2;
      bool ok = alc.release(md); 
      ok ? rls_ok_st.add(t2.elapsed()) : rls_ng_st.add(t2.elapsed());
    }
  }

  std::cout << "#[" << getpid() << "] C FINISH: " 
            << "a_ok=" << alc_ok_st.count() << ", "
            << "r_ok=" << rls_ok_st.count() << ", "
            << "a_ok_avg=" << alc_ok_st.avg() << ", "
            << "a_ng_avg=" << alc_ng_st.avg() << ", "
            << "r_ok_avg=" << rls_ok_st.avg() << ", "
            << "r_ng_avg=" << rls_ng_st.avg() 
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
    kill(children[rand() % children.size()], SIGKILL);
    usleep(rand() % 1000);
  }

  int exit_num=0;
  int signal_num=0;
  int sigkill_num=0;
  int unknown_num=0;
  for(int i=0; i < param.process_count; i++) {
    int status;
    waitpid(children[i], &status, 0);
    if(WIFEXITED(status)) {
      exit_num++;
    } else if(WIFSIGNALED(status)) {
      if(WTERMSIG(status) == SIGKILL) {
        sigkill_num++;
      } else {
        signal_num++; 
      }
    } else {
      unknown_num++;
    }
  }

  std::cout << "#[" << getpid() << "] P FINISH: " 
            << "exit=" << exit_num << ", " 
            << "killed=" << sigkill_num << ", "
            << "abort=" << signal_num << ", "
            << "unknown=" << unknown_num << std::endl;
}


int main(int argc, char** argv) {
  if(argc != 10) {
  usage:
    std::cerr << "Usage: allocator-test ALLOCATION_METHOD(variable|fixed|malloc) PROCESS_COUNT LOOP_COUNT MAX_NICE MAX_HOLD_TIME(μs) ALLOC_SIZE_MIN ALLOC_SIZE_MAX SHM_SIZE KILL_NUM" << std::endl;
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
    atoi(argv[8]),
    atoi(argv[9])
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
