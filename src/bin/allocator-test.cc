#include <imque/ipc/shared_memory.hh>
#include <imque/allocator/variable_allocator.hh>
#include <imque/allocator/fixed_allocator.hh>

#include <iostream>
#include <string>
#include <vector>
#include <string.h>
#include <stdlib.h>
#include <math.h>

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
  int max_hold_micro_sec;
  int alloc_size_min;
  int alloc_size_max;
  int shm_size;
  int kill_num;
};

class NanoTimer {
public:
  NanoTimer() {
    clock_gettime(CLOCK_REALTIME, &t);
  }

  long elapsed() {
    timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    return ns(now) - ns(t);
  }

  long ns(const timespec& ts) {
    return static_cast<long>(static_cast<long long>(ts.tv_sec)*1000*1000*1000 + ts.tv_nsec);
  }

private:
  timespec t;
};

struct Stat {
  Stat(unsigned loop_count) 
    : loop_count(loop_count),
      allocate_ok_count(0),
      release_ok_count(0),
      allocate_times(loop_count,-1),
      release_times(loop_count,-1) {
  }
  
  const unsigned loop_count;
  unsigned allocate_ok_count;
  unsigned release_ok_count;
  std::vector<long> allocate_times;
  std::vector<long> release_times;
};

long calc_average(const std::vector<long>& ary) {
  long long sum = 0;
  for(std::size_t i=0; i < ary.size(); i++) {
    if(ary[i] != -1) {
      sum += ary[i];
    }
  }
  return static_cast<long>(sum / ary.size());
}

long calc_max(const std::vector<long>& ary) {
  long max = ary[0];
  for(std::size_t i=1; i < ary.size(); i++) 
    if(ary[i] != -1 && max < ary[i])
      max = ary[i];
  return max;
}

long calc_min(const std::vector<long>& ary) {
  long min = ary[0];
  for(std::size_t i=1; i < ary.size(); i++)
    if(ary[i] != -1 && ary[i] < min)
      min = ary[i];
  return min;
}
  
long calc_standard_deviation(const std::vector<long>& ary) {
  long avg = calc_average(ary);
  long long sum = 0;
  for(std::size_t i=1; i < ary.size(); i++) {
    if(ary[i] != -1) {
      sum += static_cast<long>(pow(ary[i] - avg, 2));
    }
  }
  return static_cast<long>(sqrt(sum / ary.size()));
}

template<class Allocator>
void child_start(Allocator& alc, const Parameter& param) {
  srand(time(NULL) + getpid());
  
  Stat st(param.loop_count);
  int size_range = param.alloc_size_max-param.alloc_size_min;
  for(int i=0; i < param.loop_count; i++) {
    uint32_t size = static_cast<uint32_t>((rand() % size_range) + param.alloc_size_min);

    NanoTimer t1;
    typename Allocator::DESCRIPTOR_TYPE md = alc.allocate(size);
    st.allocate_times[i] = t1.elapsed();
    st.allocate_ok_count += md==0 ? 0 : 1;
    
    if(param.max_hold_micro_sec)
      usleep(rand() % param.max_hold_micro_sec);
    
    if(md != 0) {
      memset(alc.template ptr<void>(md), rand()%0x100, size);
      
      NanoTimer t2;
      bool ok = alc.release(md); 
      st.release_times[i] = t2.elapsed();
      st.release_ok_count += ok ? 1 : 0;
    }
  }


  std::cout << "#[" << getpid() << "] C: " 
            << "a_ok=" << st.allocate_ok_count << ", "
            << "r_ok=" << st.release_ok_count << ", "
            << "a_avg=" << calc_average(st.allocate_times) << ", "
            << "a_min=" << calc_min(st.allocate_times) << ", "
            << "a_max=" << calc_max(st.allocate_times) << ", "
            << "a_sd=" << calc_standard_deviation(st.allocate_times) << ", "
            << "r_avg=" << calc_average(st.release_times) << ", "
            << "r_min=" << calc_min(st.release_times) << ", "
            << "r_max=" << calc_max(st.release_times) << ", "
            << "r_sd=" << calc_standard_deviation(st.release_times)
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
