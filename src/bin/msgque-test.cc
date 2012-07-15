#include <iostream>
#include <ipc_msgque.hh>
#include <ipc_allocator.hh>

int main(int argc, char** argv) {
  msgque_t que(1024);
  if(! que) {
    std::cerr << "[ERROR] message queue initialization failed" << std::endl;
  }

  que.push("abc", 4);
  msgque_data_t msg = que.pop();
  if(msg) {
    std::cout << "size=" << msg.size << "data=" << (const char*)msg.data << std::endl;
  }

  unsigned size = 1024 * 512;
  char* chunk = new char[size];
  allocator alloca(chunk, size);
  std::cout << "# " << alloca.allocate(10) << std::endl;
  std::cout << "# " << alloca.allocate(10) << std::endl;
  return 0;
}
