#ifndef IPC_MSGQUE_HH
#define IPC_MSGQUE_HH

#include "ipc_mmap.hh"
//#include "ipc_allocator.hh"
#include "imque/allocator.hh"
#include <inttypes.h>
#include <string>
#include <string.h>

typedef imque::Allocator allocator;

struct msgque_data_t {
  msgque_data_t(allocator& alc, uint32_t index) 
    : alc_(alc), index_(index) {
  }

  operator bool() const { return index_ != 0; }
  
  void* data() const { return alc_.ptr<char>(index_)+sizeof(std::size_t); }
  std::size_t size() const { return alc_.ptr<std::size_t>(index_)[0]; }
  
  allocator& alc_;
  uint32_t index_;
};

struct que_ent_t {
  uint32_t flag:1;
  uint32_t value:31;

  uint32_t uint32() const { return *(uint32_t*)this; }
};

struct msgque_queue_t {
  volatile uint32_t next_read;
  volatile uint32_t next_write;
  uint32_t size;
  
  que_ent_t entries[0];

  bool push(uint32_t index);
  uint32_t pop();
};

// TODO: 「キューが満杯」等の状態が発生したことを検知できるフラグ(or フィールド)をいくつか用意しておく
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
      alc_(mm_.ptr<void>(), mm_.size()),
      mm_que_(sizeof(msgque_queue_t) + entry_count * sizeof(uint32_t)*2), // TODO: アロケータ自体のオーバヘッドを考慮
      que_(mm_que_.ptr<msgque_queue_t>())
  {
    que_->size = entry_count;
  }

  // TODO: 自動で初期化されるようにしたい
  bool init() {
    alc_.init();
    
    que_->next_read = 0;
    que_->next_write = 0;
    memset(que_->entries, 0, sizeof(que_ent_t)*que_->size);
  }
  
  operator bool() const { return mm_; }

  bool push(const void* data, std::size_t size);
  bool pop(std::string& buf);

private:
  mmap_t mm_;  // mm_data_
  allocator alc_;
  
  mmap_t mm_que_;
  msgque_queue_t* que_;
};

#endif
