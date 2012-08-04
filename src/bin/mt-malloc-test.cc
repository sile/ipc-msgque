#include <iostream>
#include <string.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>

#include <pthread.h>

const int CHILD_NUM  = 500;
const int LOOP_COUNT = 1000;

void sigsegv_handler(int sig) {
  std::cerr << "#" << getpid() << ":" << sig << std::endl;
  exit(1);
}

void* child_start(void* data) {
  int id = static_cast<int>(reinterpret_cast<long long>(data));
  std::cout << "# child: " << id << std::endl;
  srand(time(NULL));

  for(int i=0; i < LOOP_COUNT; i++) {
    unsigned size = (rand() % 1024) + 1;

    char *buf = new char[size];
    memset(buf, rand()%0x100, size);
    usleep(rand() % 400);
    delete [] buf;
  }
  std::cout << "# exit: " << id << std::endl;
}

int main() {
  pthread_t children[CHILD_NUM];
  signal(SIGSEGV, sigsegv_handler);

  for(int i=0; i < CHILD_NUM; i++) {
    pthread_create(&children[i], NULL, child_start, reinterpret_cast<void*>(i));
  }
  for(int i=0; i < CHILD_NUM; i++) {
    pthread_join(children[i], NULL);
  }

  return 0;
}
