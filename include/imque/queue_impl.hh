#ifndef IMQUE_QUEUE_IMPL_HH
#define IMQUE_QUEUE_IMPL_HH

#include "shared_memory.hh"
#include "allocator.hh"

#include <inttypes.h>
#include <string.h>

namespace imque {
  class QueueImpl {
    struct Entry {
      uint32_t state:1;
      uint32_t value:31;
    };

    struct Header {
      volatile uint32_t read_pos;
      volatile uint32_t write_pos;
      uint32_t entry_count;
      Entry entries[0];
      
      uint32_t next_read_pos() const { return (read_pos+1)%entry_count; }
      uint32_t next_write_pos() const { return (write_pos+1)%entry_count; }
    };

  public:
    QueueImpl(size_t entry_count, SharedMemory& shm)
      : que_(shm.ptr<Header>()),
        alc_(shm.ptr<void>(que_size(entry_count)), shm.size()-que_size(entry_count)) {
      if(shm) {
        que_->entry_count = entry_count;
      }
    }

    operator bool() const { return alc_; }
    
    void init() {
      alc_.init();
      
      que_->read_pos  = 0;
      que_->write_pos = 0;
      memset(que_->entries, 0, sizeof(Entry)*que_->entry_count);
    }

    bool enq(const void* data, size_t size) {
      if(isFull()) {
        return false;
      }
      
      return true;
    }

    bool deq(std::string& buf) {
      if(isEmpty()) {
        return false;
      }

      return true;
    }

    bool isEmpty() const { return que_->read_pos == que_->write_pos; }
    bool isFull()  const { return que_->read_pos == que_->next_write_pos(); }

    static size_t calc_need_byte_size(size_t entry_count, size_t data_size) {
      return que_size(entry_count) + dat_size(data_size);
    }

  private:
    static size_t que_size(size_t entry_count) {
      return sizeof(Header) + sizeof(Entry)*entry_count;
    }
    
    static size_t dat_size(size_t data_size) {
      return Allocator::calc_need_byte_size(data_size);
    }

  private:
    Header*   que_;
    Allocator alc_;
  };
}

#endif 
