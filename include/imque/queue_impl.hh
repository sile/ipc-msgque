#ifndef IMQUE_QUEUE_IMPL_HH
#define IMQUE_QUEUE_IMPL_HH

#include "shared_memory.hh"
#include "allocator.hh"
#include "block_allocator.hh"

#include <inttypes.h>
#include <string.h>

namespace imque {
  class QueueImpl {
    struct Entry {
      uint32_t next;
      uint32_t size;
      char data[0];

      static const uint32_t END = 0;
    };

    struct Stat {
      uint32_t overflowed_count;
    };

    struct Ver {
      uint32_t ver;
      uint32_t val;
      
      static Ver fromUint64(uint64_t n) { 
        uint64_t* tmp = &n;
        return *reinterpret_cast<Ver*>(tmp);
      }

      uint64_t toUint64() {
        return __sync_add_and_fetch(reinterpret_cast<uint64_t*>(this), 0);
      }

      uint64_t* toUint64Ptr() {
        return reinterpret_cast<uint64_t*>(this);
      }

      Ver snapshot() {
        return Ver::fromUint64(toUint64());
      }
    };

    struct Header {
      /*volatile*/ Ver head;
      /*volatile*/ Ver tail;

      Stat stat;
    };

  public:
    QueueImpl(SharedMemory& shm)
      : que_(shm.ptr<Header>()),
        alc_(shm.ptr<void>(que_size()), shm.size()-que_size()) {
    }
    
    operator bool() const { return alc_; }
    
    void init() {
      alc_.init();
      
      uint32_t dummy_head = alc_.allocate(sizeof(Entry));
      if(dummy_head != 0) {
        alc_.ptr<Entry>(dummy_head)->next = Entry::END;
      }
      que_->head.ver = 0;
      que_->head.val = dummy_head;
      que_->tail.ver = 0;
      que_->tail.val = dummy_head;
      
      que_->stat.overflowed_count = 0;
    }

    Ver get_tail() {
      Ver tail = que_->tail.snapshot();
      Entry* e = alc_.ptr<Entry>(tail.val);
      uint32_t e_next = e->next;
      if(e_next == Entry::END) {
        return tail;
      }
      
      Ver new_tail = {tail.ver+1, e_next};
      __sync_bool_compare_and_swap(que_->tail.toUint64Ptr(), tail.toUint64(), new_tail.toUint64());
      return get_tail();
    }

    bool enq(const void* data, size_t size) {
      uint32_t alloc_id = alc_.allocate(sizeof(Entry) + size);
      if(alloc_id == 0) {
        __sync_add_and_fetch(&que_->stat.overflowed_count, 1);
        return false;
      }
      
      Entry* e = alc_.ptr<Entry>(alloc_id);
      e->next = Entry::END;
      e->size = size;
      memcpy(e->data, data, size);

      for(;;) {
        Ver tail = get_tail();
        assert(tail.val != alloc_id);
        Entry* e2 = alc_.ptr<Entry>(tail.val);
        if(__sync_bool_compare_and_swap(&e2->next, Entry::END, alloc_id)) {
          break;
        }
      }

      return true;
    }

    bool deq(std::string& buf) {
      for(;;) {
        Ver head = que_->head.snapshot();
        Ver tail = get_tail();
        
        if(head.val == tail.val) { // TODO: version check
          return false; // empty
        }
        assert(head.ver < tail.ver);

        Entry* e = alc_.ptr<Entry>(head.val);
        uint32_t e_next = e->next;
        if(que_->head.toUint64() != head.toUint64()) {
          continue;
        }
        assert(e_next != Entry::END);
        /*
        if(e_next == Entry::END) {
          return false; // empty
        }
        */
        
        Ver new_head = {head.ver+1, e_next};
        if(__sync_bool_compare_and_swap(que_->head.toUint64Ptr(), head.toUint64(), new_head.toUint64())) {
          Entry* e2 = alc_.ptr<Entry>(e_next);
          buf.assign(e2->data, e2->size);
          assert(alc_.release(head.val));
          return true;
        }
      }
    }

    bool isEmpty() const { return que_->head.val == que_->tail.val; } // XXX:
    bool isFull()  const { return false; } // TODO: delete

    static size_t calc_need_byte_size(size_t data_size) {
      return que_size() + dat_size(data_size);
    }

    size_t overflowedCount() const { return que_->stat.overflowed_count; }
    void resetOverflowedCount() { que_->stat.overflowed_count = 0; }

  private:
    static size_t que_size() {
      return sizeof(Header);
    }
    
    static size_t dat_size(size_t data_size) {
      return Allocator::calc_need_byte_size(data_size);
    }

  private:
    Header*   que_;
    BlockAllocator alc_;
  };
}

#endif 
