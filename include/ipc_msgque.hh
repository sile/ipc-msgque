#ifndef IPC_MSGQUE_HH
#define IPC_MSGQUE_HH

#include "ipc_mmap.hh"

struct msgque_data_t {
  msgque_data_t(const void * const data, std::size_t size) : data(data), size(size) {}

  operator bool() const { return data != NULL; }

  std::size_t size;
  const void * const data;
};

class msgque_t {
public:
  msgque_t(const std::string& filepath, size_t size, mode_t mode=0666) : mm_(filepath,size,mode) {}
  msgque_t(size_t size) : mm_(size) {}
  
  operator bool() const { return mm_; }

  bool push(const void* data, std::size_t size);
  msgque_data_t pop();

private:
  mmap_t mm_;
};

#endif
