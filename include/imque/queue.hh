#ifndef IMQUE_QUEUE_HH
#define IMQUE_QUEUE_HH

#include "ipc/shared_memory.hh"
#include "queue/queue_impl.hh"

#include <string>
#include <sys/types.h>

namespace imque {
  class Queue {
  public:
    Queue(size_t entry_count, size_t data_size)
      : shm_(QueueImpl::calc_need_byte_size(entry_count, data_size)),
        impl_(entry_count, shm_) {
    }
      
    Queue(size_t entry_count, size_t data_size, const std::string& filepath, mode_t mode=0660)
      : shm_(filepath, QueueImpl::calc_need_byte_size(entry_count, data_size), mode),
        impl_(entry_count, shm_) {
    }

    operator bool() const { return shm_ && impl_; }

    void init() {
      if(*this) {
        impl_.init();
      }
    }

    bool enq(const void* data, size_t size) { return impl_.enq(data, size); }

    bool deq(std::string& data) { return impl_.deq(data); }
    
    bool isEmpty() const { return impl_.isEmpty(); }
    bool isFull()  const { return impl_.isFull(); }

    size_t overflowedCount() const { return impl_.overflowedCount(); }
    void resetOverflowedCount() { impl_.resetOverflowedCount(); }

  private:
    SharedMemory shm_;
    QueueImpl    impl_;
  };
}

#endif
