#include <imque/queue.hh>

#include "../aux/nano_timer.hh"
#include "../aux/stat.hh"

#include <iostream>
#include <string>
#include <vector>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/time.h>

struct Param {
  int reader_count;
  int reader_loop_count;
  int reader_max_nice;
  int read_interval;

  int writer_count;
  int writer_loop_count;
  int writer_max_nice;
  int write_interval;
  
  int msg_size_min;
  int msg_size_max;
  int shm_size;
  int kill_num;
};

void gen_random_string(std::string& s, std::size_t size) {
  const char cs[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
  s.resize(size);
  for(std::size_t i=0; i < size; i++) {
    s[i] = cs[rand() % strlen(cs)];
  }
}

void reader_start(const Param& param, imque::Queue& que) {
  srand(time(NULL) + getpid());
  int new_nice = nice(rand() % (param.reader_max_nice+1));
  std::cout << "#[" << getpid() << "] R START: nice=" << new_nice << std::endl;
  
  imque::Stat ok_st;
  imque::Stat ng_st;
  std::string buf;
  for(int i=0; i < param.reader_loop_count; i++) {
    imque::NanoTimer t;
    que.deq(buf) ? ok_st.add(t.elapsed()) : ng_st.add(t.elapsed());
    
    if(param.read_interval)
      usleep(rand() % param.read_interval);
  }

  std::cout << "#[" << getpid() << "] R FINISH: " 
            << "ok=" << ok_st.count() << ", "
            << "ok_avg=" << ok_st.avg() << ", "
            << "ng_avg=" << ng_st.avg()
            << std::endl;
}

void writer_start(const Param& param, imque::Queue& que) {
  srand(time(NULL) + getpid());
  int new_nice = nice(rand() % (param.writer_max_nice+1));
  std::cout << "#[" << getpid() << "] W START: nice=" << new_nice << std::endl;
  
  imque::Stat ok_st;
  imque::Stat ng_st;
  int size_range = param.msg_size_max - param.msg_size_min + 1;
  std::string buf;
  
  for(int i=0; i < param.writer_loop_count; i++) {
    uint32_t size = static_cast<uint32_t>((rand() % size_range) + param.msg_size_min);
    gen_random_string(buf, size);
    
    imque::NanoTimer t;
    que.enq(buf.data(), buf.size()) ? ok_st.add(t.elapsed()) : ng_st.add(t.elapsed());
    
    if(param.write_interval)
      usleep(rand() % param.write_interval);
  }


  std::cout << "#[" << getpid() << "] W FINISH: " 
            << "ok=" << ok_st.count() << ", "
            << "ok_avg=" << ok_st.avg() << ", "
            << "ng_avg=" << ng_st.avg()
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
  int sigkill_num=0;
  int unknown_num=0;
  std::vector<pid_t>* children[2] = {&writers, &readers};
  for(int j=0; j < 2; j++) {
    for(std::size_t i=0; i < children[j]->size(); i++) {
      int status;
      waitpid(children[j]->at(i), &status, 0);
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
  }

  std::cout << "#[" << getpid() << "] P FINISH: " 
            << "exit=" << exit_num << ", " 
            << "killed=" << sigkill_num << ", "
            << "signal=" << signal_num << ", "
            << "unknown=" << unknown_num << " | " 
            << "overflow=" << que.overflowedCount() << std::endl;
}

int main(int argc, char** argv) {
  if(argc != 13) {
    std::cerr << "Usage: msgque-test READER_COUNT READER_LOOP_COUNT READER_MAX_NICE READ_INTERVAL(μs) WRITER_COUNT WRITER_LOOP_COUNT WRITER_MAX_NICE WRITE_INTERVAL(μs) MESSAGE_SIZE_MIN MESSAGE_SIZSE_MAX SHM_SIZE KILL_NUM" << std::endl;
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
    atoi(argv[11]),
    atoi(argv[12])
  };

  imque::Queue que(param.shm_size);
  if(! que) {
    std::cerr << "[ERROR] queue initialization failed" << std::endl;
    return 1;
  }
  
  parent_start(param, que);
  
  return 0;
}
