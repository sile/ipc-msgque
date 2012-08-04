#include <iostream>
#include <imque/queue.hh>
#include <string.h>
#include <stdlib.h>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <imque/queue.hh>

const int CHILD_NUM = 2000;
const int LOOP_COUNT = 400;

void sigsegv_handler(int sig) {
  std::cerr << "#" << getpid() << ":" << sig << std::endl;
  exit(1);
}

void gen_random_string(std::string& s, std::size_t size) {
  const char cs[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
  s.resize(size);
  for(int i=0; i < size; i++) {
    s[i] = cs[rand() % strlen(cs)];
  }
}

void writer_start(imque::Queue& que) {
  std::cout << "# child(writer): " << getpid() << std::endl;
  srand(time(NULL)+getpid());
  
  for(int i=0; i < LOOP_COUNT; i++) {
    unsigned size = (rand() % 128) + 3;
    std::string s;
    gen_random_string(s, size);
    
    if(que.enq(s.c_str(), s.size()) == false) {
      std::cout << "@ [" << getpid() << "] queue is full" << std::endl;
      usleep(rand() % 300);
    } else {
      std::cout << "@ [" << getpid() << "] write: " << s << std::endl;
    }
    usleep(rand() % 400);
  }
  std::cout << "# exit: " << getpid() << std::endl;
}

void reader_start(imque::Queue& que) {
  std::cout << "# child(reader): " << getpid() << std::endl;
  srand(time(NULL));
  usleep(10);
  
  for(int i=0; i < LOOP_COUNT*3.5; i++) {
    std::string s;
    if(que.deq(s) == false) {
      std::cout << "@ [" << getpid() << "] queue is empty" << std::endl;
      usleep(rand() % 200);
    } else {
      std::cout << "@ [" << getpid() << "] read: " << s << std::endl;
    }
  }
  std::cout << "# exit: " << getpid() << std::endl;
}

int main(int argc, char** argv) {
  imque::Queue que(256, 1024*1024);
  que.init();
  signal(SIGSEGV, sigsegv_handler);

  if(! que) {
    std::cerr << "[ERROR] message queue initialization failed" << std::endl;
    return 1;
  }

  for(int i=0; i < CHILD_NUM; i++) {
    if(fork() == 0) {
      if(i % 3 == 0) {
        reader_start(que);
      } else {
        writer_start(que);
      }
      return 0;
    }
  }
  for(int i=0; i < CHILD_NUM; i++) {
    waitid(P_ALL, 0, NULL, WEXITED);
  }
  std::cerr << "overflow: " << que.overflowedCount() << std::endl;

  return 0;
}
