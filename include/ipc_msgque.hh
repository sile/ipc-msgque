#ifndef IPC_MSGQUE_HH
#define IPC_MSGQUE_HH

#include "ipc_mmap.hh"
#include "ipc_allocator.hh"
#include <inttypes.h>

struct msgque_data_t {
  msgque_data_t(const void * const data, std::size_t size) : data(data), size(size) {}

  operator bool() const { return data != NULL; }

  std::size_t size;
  const void * const data;
};

struct cons_t {
  uint32_t car;
  uint32_t cdr;
};

struct msgque_queue_t {
  volatile uint32_t head;
  volatile uint32_t entry_count;
  uint32_t max_entry_count;

  bool push(uint32_t index, allocator& alc);
  uint32_t pop(allocator& alc);
};

class msgque_t {
public:
  /* TODO: 後で
  msgque_t(const std::string& filepath, size_t size, mode_t mode=0666) 
    : mm_(filepath,size,mode),
      alc_(mm_.ptr<void*>(), mm_.size())
  {
  }
  */

  msgque_t(size_t entry_count, size_t data_size)
    : mm_(data_size),
      alc_(mm_.ptr<void*>(), mm_.size()),
      mm_que_(sizeof(msgque_queue_t) + entry_count * sizeof(uint32_t)*2), // TODO: アロケータ自体のオーバヘッドを考慮
      que_alc_(mm_que_.ptr<void*>(), mm_que_.size()),
      que_(mm_que_.ptr<msgque_queue_t>())
  {
    que_->max_entry_count = entry_count;
  }

  // TODO: 自動で初期化されるようにしたい
  bool init() {
    que_->head = 0;
    que_->entry_count = 0;
  }
  
  operator bool() const { return mm_; }

  bool push(const void* data, std::size_t size);
  msgque_data_t pop();
  // TODO: void release(

  // XXX: que_のメンバにすべき
  bool is_empty() const {
    return que_->head == 0;
  }

  bool is_full() const {
    return que_->entry_count >= que_->max_entry_count;
  }

private:
  mmap_t mm_;  // mm_data_
  allocator alc_;
  
  mmap_t mm_que_;
  allocator que_alc_;
  msgque_queue_t* que_;
};

#endif
