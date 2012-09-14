/**
 * キューに入れたメッセージの欠損や重複がないかのチェック
 */
#include <imque/queue.hh>
#include <imque/ipc/shared_memory.hh>
#include <imque/atomic/atomic.hh>
#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>

struct Param {
  int process_count;
  int messages_per_process;
  int interval; // μsec
  int shm_size;
};

void reader_start(imque::Queue& que, imque::ipc::SharedMemory& recv_marks, const Param& param) {
  srand(time(NULL) + getpid());
 
  std::string buf;
  for(int i=0; i < param.messages_per_process; ) {
    if(que.deq(buf)) {
      int index = atoi(buf.c_str());
      imque::atomic::add(&recv_marks.ptr<int>()[index], 1);
      i++;
    } else if(que.overflowedCount() > 0) {
      i++;
    }
    if(param.interval) 
      usleep(rand() % param.interval);
  }
}

void writer_start(int id, imque::Queue& que, const Param& param) {
  srand(time(NULL) + getpid());

  std::ostringstream out;
  for(int i=0; i < param.messages_per_process; i++) {
    out << (id*param.messages_per_process) + i;
    que.enq(out.str().data(), out.str().size());
    out.str("");

    if(param.interval) 
      usleep(rand() % param.interval);
  }
}

void parent_start(imque::Queue& que, imque::ipc::SharedMemory& recv_marks, const Param& param) {
  std::vector<pid_t> children(param.process_count*2); // XXX: 実際は process_count の二倍のプロセスを生成している

  // reader
  for(int i=0; i < param.process_count; i++) {
    children[i] = fork();
    switch(children[i]) {
    case -1:
      std::cerr << "ERROR: fork() failed: " << strerror(errno) << std::endl;
      return;
    case 0:
      reader_start(que, recv_marks, param);
      return;
    }
  }

  // writer
  for(int i=0; i < param.process_count; i++) {
    children[i+param.process_count] = fork();
    switch(children[i+param.process_count]) {
    case -1:
      std::cerr << "ERROR: fork() failed: " << strerror(errno) << std::endl;
      return;
    case 0:
      writer_start(i, que, param);
      return;
    }
  }
  
  int exit_num=0;
  int signal_num=0;
  int unknown_num=0;
  for(std::size_t i=0; i < children.size(); i++) {
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

  int ok_count = 0;
  int missing_count = 0;
  int duplicate_count = 0;
  for(int i=0; i < param.process_count*param.messages_per_process; i++) {
    int count = recv_marks.ptr<int>()[i];
    if(count == 0) {
      missing_count++;
    } else if(count > 1) {
      duplicate_count++;
    } else {
      ok_count++;
    }
  }
      
  std::cout << "#[" << getpid() << "] FINISH: " 
            << "ok=" << ok_count << ", "
            << "miss=" << missing_count << ", " 
            << "dup=" << duplicate_count << " | "
            << "exit=" << exit_num << ", " 
            << "signal=" << signal_num << ", "
            << "unknown=" << unknown_num << " | " 
            << "overflow=" << que.overflowedCount() << std::endl;
}

int main(int argc, char** argv) {
  if(argc != 5) {
    std::cerr << "Usage: consistency-check PROCESS_COUNT MESSAGES_PER_PROCESS INTERVAL SHM_SIZE" << std::endl;
    return 1;
  }

  Param param = {
    atoi(argv[1]),
    atoi(argv[2]),
    atoi(argv[3]),
    atoi(argv[4])
  };

  imque::Queue que(param.shm_size);
  if(! que) {
    std::cerr << "[ERROR] queue initialization failed" << std::endl;
    return 1;
  }
  
  imque::ipc::SharedMemory recv_marks(sizeof(int) * param.process_count * param.messages_per_process);
  if(! recv_marks) {
    std::cerr << "[ERROR] shm initialization failed" << std::endl;
    return 1;
  }
  memset(recv_marks.ptr<void>(), 0, recv_marks.size());
  
  parent_start(que, recv_marks, param);
  
  return 0;
}
