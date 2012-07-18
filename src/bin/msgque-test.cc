#include <iostream>
#include <ipc_msgque.hh>
#include <string.h>
#include <stdlib.h>
#include <string>

int main(int argc, char** argv) {
  msgque_t que(20, 1024*32);
  que.init();

  if(! que) {
    std::cerr << "[ERROR] message queue initialization failed" << std::endl;
  }

  for(int i=0; i < argc; i++) {
    que.push(argv[i], strlen(argv[i])+1);
  }

  std::string buf;
  for(;;) {
    buf.clear();
    if(! que.pop(buf)) {
      break;
    }
    std::cout << "size=" << buf.size() << ", data=" << buf.data() << std::endl;
  }
  
  return 0;
}
