#include <iostream>
#include <ipc_msgque.hh>
#include <string.h>
#include <stdlib.h>

int main(int argc, char** argv) {
  msgque_t que(20, 1024*32);
  que.init();

  if(! que) {
    std::cerr << "[ERROR] message queue initialization failed" << std::endl;
  }

  for(int i=0; i < argc; i++) {
    que.push(argv[i], strlen(argv[i]));
  }
  
  for(;;) {
    msgque_data_t msg = que.pop();
    if(! msg) {
      break;
    }
    std::cout << "size=" << msg.size() << ", data=" << (const char*)msg.data() << std::endl;
  }
  
  return 0;
}
