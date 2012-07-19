#include <iostream>
#include <ipc_msgque.hh>
#include <string.h>
#include <stdlib.h>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

const int CHILD_NUM = 10;
const int LOOP_COUNT = 100;

void gen_random_string(std::string& s, std::size_t size) {
  const char cs[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
  s.resize(size);
  for(int i=0; i < size; i++) {
    s[i] = cs[rand() % strlen(cs)];
  }
}

void writer_start(msgque_t& que) {
  std::cout << "# child(writer): " << getpid() << std::endl;
  srand(time(NULL));
  
  for(int i=0; i < LOOP_COUNT; i++) {
    unsigned size = (rand() % 128) + 3;
    std::string s;
    gen_random_string(s, size);
    
    if(que.push(s.c_str(), s.size()+1) == false) {
      std::cout << "@ [" << getpid() << "] queue is full" << std::endl;
      usleep(rand() % 300);
    } else {
      std::cout << "@ [" << getpid() << "] write: " << s << std::endl;
    }
    usleep(rand() % 300);
  }
  std::cout << "# exit: " << getpid() << std::endl;
}

void reader_start(msgque_t& que) {
  std::cout << "# child(reader): " << getpid() << std::endl;
  srand(time(NULL));
  
  for(int i=0; i < LOOP_COUNT; i++) {
    std::string s;
    
    if(que.pop(s) == false) {
      std::cout << "@ [" << getpid() << "] queue is empty" << std::endl;
      usleep(rand() % 300);
    } else {
      std::cout << "@ [" << getpid() << "] read: " << s << std::endl;
    }
    usleep(rand() % 300);
  }
  std::cout << "# exit: " << getpid() << std::endl;
}

int main(int argc, char** argv) {
  msgque_t que(256, 1024*1024);
  que.init();

  if(! que) {
    std::cerr << "[ERROR] message queue initialization failed" << std::endl;
    return 1;
  }

  for(int i=0; i < CHILD_NUM; i++) {
    if(fork() == 0) {
      if(i % 2 == 0) {
        writer_start(que);
      } else {
        reader_start(que);
      }
      return 0;
    }
  }

  waitid(P_ALL, 0, NULL, WEXITED);

  return 0;
}
