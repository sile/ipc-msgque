#include <imque/queue.hh>

#include <iostream>
#include <string>
#include <vector>
#include <algorithm>

#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <signal.h>

struct Param {
  int reader_count;
  int reader_loop_count;
  int read_interval;

  int writer_count;
  int writer_loop_count;
  int write_interval;
  
  int msg_size_min;
  int msg_size_max;
  int que_entry_count;
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
      ok_count(0),
      times(loop_count,-1) {
  }
  
  const unsigned loop_count;
  unsigned ok_count;
  std::vector<long> times;
};

long calc_average(const std::vector<long>& ary) {
  long long sum = 0;
  long count = 0;
  for(std::size_t i=0; i < ary.size(); i++) {
    if(ary[i] != -1) {
      sum += ary[i];
      count++;
    }
  }
  return static_cast<long>(sum / count);
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
  if(min == -1)
    min = static_cast<long>(0xFFFFFFFF);
  for(std::size_t i=1; i < ary.size(); i++)
    if(ary[i] != -1 && ary[i] < min)
      min = ary[i];
  return min;
}
  
long calc_standard_deviation(const std::vector<long>& ary) {
  long avg = calc_average(ary);
  long long sum = 0;
  long count = 0;
  for(std::size_t i=1; i < ary.size(); i++) {
    if(ary[i] != -1) {
      sum += static_cast<long>(pow(ary[i] - avg, 2));
      count++;
    }
  }
  return static_cast<long>(sqrt(sum / count));
}

void gen_random_string(std::string& s, std::size_t size) {
  const char cs[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
  s.resize(size);
  for(std::size_t i=0; i < size; i++) {
    s[i] = cs[rand() % strlen(cs)];
  }
}

void reader_start(const Param& param, imque::Queue& que) {
  srand(time(NULL) + getpid());
  
  Stat st(param.reader_loop_count);
  std::string buf;
  for(int i=0; i < param.reader_loop_count; i++) {
    NanoTimer t1;
    if(que.deq(buf)) {
      st.times[i] = t1.elapsed();
      st.ok_count++;
    }
    
    if(param.read_interval)
      usleep(rand() % param.read_interval);
  }

  std::cout << "#[" << getpid() << "] R: " 
            << "ok=" << st.ok_count << ", "
            << "avg=" << calc_average(st.times) << ", "
            << "min=" << calc_min(st.times) << ", "
            << "max=" << calc_max(st.times) << ", "
            << "sd=" << calc_standard_deviation(st.times)
            << std::endl;
}

void writer_start(const Param& param, imque::Queue& que) {
  srand(time(NULL) + getpid());
  
  Stat st(param.writer_loop_count);
  int size_range = param.msg_size_max - param.msg_size_min;
  std::string buf;
  
  for(int i=0; i < param.writer_loop_count; i++) {
    uint32_t size = static_cast<uint32_t>((rand() % size_range) + param.msg_size_min);
    gen_random_string(buf, size);
    
    NanoTimer t1;
    if(que.enq(buf.data(), buf.size())) {
      st.times[i] = t1.elapsed();
      st.ok_count++;
    }
    
    if(param.write_interval)
      usleep(rand() % param.write_interval);
  }

  std::cout << "#[" << getpid() << "] W: " 
            << "ok=" << st.ok_count << ", "
            << "avg=" << calc_average(st.times) << ", "
            << "min=" << calc_min(st.times) << ", "
            << "max=" << calc_max(st.times) << ", "
            << "sd=" << calc_standard_deviation(st.times)
            << std::endl;

}

void parent_start(const Param& param, imque::Queue& que) {
  std::vector<pid_t> writers(param.writer_count);
  std::vector<pid_t> readers(param.reader_count);
  
  for(std::size_t i=0; i < std::max(readers.size(), writers.size()); i++) {
    if(i < writers.size()) {
      writers[i] = fork();
      switch(writers[i]) {
      case 0:
        writer_start(param, que);
        return;
      case -1:
        std::cerr << "ERROR: fork() failed: " << strerror(errno) << std::endl;
        return;
      }
    }
    if(i < readers.size()) {
      readers[i] = fork();
      switch(readers[i]) {
      case 0:
        reader_start(param, que);
        return;
      case -1:
        std::cerr << "ERROR: fork() failed: " << strerror(errno) << std::endl;
        return;
      }
    }
  }
  
  srand(time(NULL) + getpid());
  for(int i=0; i < param.kill_num; i++) {
    if(rand() % 2 == 0) {
      kill(writers[rand() % writers.size()], 9);
    } else {
      kill(readers[rand() % readers.size()], 9);
    }
    usleep(rand() % 1000);
  }

  int exit_num=0;
  int signal_num=0;
  int unknown_num=0;

  std::vector<pid_t>* children[2] = {&writers, &readers};
  for(int j=0; j < 2; j++) {
    for(std::size_t i=0; i < children[j]->size(); i++) {
      int status;
      waitpid(children[j]->at(i), &status, 0);
      if(WIFEXITED(status)) {
        exit_num++;
      } else if(WIFSIGNALED(status)) {
        signal_num++;
      } else {
        unknown_num++;
      }
    }
  }

  std::cout << "#[" << getpid() << "] P: " 
            << "exit=" << exit_num << ", " 
            << "signal=" << signal_num << ", "
            << "unknown=" << unknown_num << " | " 
            << "overflow=" << que.overflowedCount() << std::endl;
}

int main(int argc, char** argv) {
  if(argc != 12) {
    std::cerr << "Usage: msgque-test READER_COUNT READER_LOOP_COUNT READ_INTERVAL(μs) WRITER_COUNT WRITER_LOOP_COUNT WRITE_INTERVAL(μs) MESSAGE_SIZE_MIN MESSAGE_SIZSE_MAX QUEUE_ENTRY_COUNT SHM_SIZE KILL_NUM" << std::endl;
    return 1;
  }

  Param param = {
    atoi(argv[1]),
    atoi(argv[2]),
    atoi(argv[3]),
    atoi(argv[4]),
    atoi(argv[5]),
    atoi(argv[6]),
    atoi(argv[7]),
    atoi(argv[8]),
    atoi(argv[9]),
    atoi(argv[10]),
    atoi(argv[11])
  };

  imque::Queue que(param.que_entry_count, param.shm_size);
  que.init();
  
  if(! que) {
    std::cerr << "[ERROR] queue initialization failed" << std::endl;
    return 1;
  }
  
  parent_start(param, que);
  
  return 0;
}
