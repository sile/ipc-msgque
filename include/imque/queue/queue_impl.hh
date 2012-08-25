#ifndef IMQUE_QUEUE_QUEUE_IMPL_HH
#define IMQUE_QUEUE_QUEUE_IMPL_HH

#include "../atomic/atomic.hh"
#include "../ipc/shared_memory.hh"
#include "../allocator/fixed_allocator.hh"
#include <inttypes.h>
#include <string.h>

namespace imque {
  namespace queue {
    // FIFOキュー
    class QueueImpl {
      struct Entry {
        uint32_t state:2;
        uint32_t value:30;

        enum STATE {
          FREE = 0,
          USED = 1
        };
      };

      struct Stat {
        uint32_t overflowed_count;
      };

      struct Header {
        volatile uint32_t version; // tag for ABA problem
        volatile uint32_t read_pos;
        volatile uint32_t write_pos;
        Stat stat;
        uint32_t entry_limit;
        Entry entries[0];
      };

    public:
      QueueImpl(size_t entry_limit, ipc::SharedMemory& shm)
        : que_(shm.ptr<Header>()),
          alc_(shm.ptr<void>(queSize(entry_limit)),
               shm.size() > queSize(entry_limit) ?  shm.size() - queSize(entry_limit) : 0) {
        if(shm) {
          que_->entry_limit = entry_limit;
        }
      }

      operator bool() const { return que_ != NULL && alc_; }
    
      // 初期化メソッド。
      // コンストラクタに渡した一つの shm につき、一回呼び出す必要がある。
      void init() {
        if(*this) {
          alc_.init();
      
          que_->version = 0;
          que_->read_pos  = 0;
          que_->write_pos = 0;
          que_->stat.overflowed_count = 0;
          memset(que_->entries, 0, sizeof(Entry)*que_->entry_limit);
        }
      }

      // キューに要素を追加する (キューに空きがない場合は false を返す)
      bool enq(const void* data, size_t size) {
        return enqv(&data, &size, 1);
      }

      // キューに要素を追加する (キューに空きがない場合は false を返す)
      // datav および sizev は count 分のサイズを持ち、それらを全て結合したデータがキューには追加される
      bool enqv(const void** datav, size_t* sizev, size_t count) {
        if(isFull()) {
          atomic::add(&que_->stat.overflowed_count, 1);
          return false;
        }
      
        size_t total_size = 0;
        for(size_t i=0; i < count; i++) {
          total_size += sizev[i];
        }

        uint32_t alloc_id = alc_.allocate(sizeof(size_t) + total_size);
        if(alloc_id == 0) {
          atomic::add(&que_->stat.overflowed_count, 1);
          return false;
        }

        alc_.ptr<size_t>(alloc_id)[0] = total_size;
        size_t offset = sizeof(size_t);
        for(size_t i=0; i < count; i++) {
          memcpy(alc_.ptr<void>(alloc_id, offset), datav[i], sizev[i]);
          offset += sizev[i];
        }

        if(enqImpl(alloc_id) == false) {
          assert(alc_.release(alloc_id));
          atomic::add(&que_->stat.overflowed_count, 1);
          return false;
        }
      
        return true;
      }

      // キューから要素を取り出し buf に格納する (キューが空の場合は false を返す)
      bool deq(std::string& buf) {
        if(isEmpty()) {
          return false;
        }

        uint32_t alloc_id = deqImpl();
        if(alloc_id == 0) {
          return false;
        }

        size_t size = alc_.ptr<size_t>(alloc_id)[0];
        char*  data = alc_.ptr<char>(alloc_id, sizeof(size_t));
        buf.assign(data, size);
      
        assert(alc_.release(alloc_id));
      
        return true;
      }

      bool isEmpty() const { return que_->read_pos == que_->write_pos; }
      bool isFull()  const { return que_->read_pos == (que_->write_pos+1) % que_->entry_limit; }
      size_t entryCount() const { 
        uint32_t curr_read  = que_->read_pos;
        uint32_t curr_write = que_->write_pos;
        if(curr_write >= curr_read) {
          return curr_write - curr_read;
        } else {
          return curr_write + (que_->entry_limit - curr_read);
        }
      }

      // キューへの要素追加に失敗した回数を返す
      size_t overflowedCount() const { return que_->stat.overflowed_count; }
      void resetOverflowedCount() { que_->stat.overflowed_count = 0; }

    private:
      bool enqImpl(uint32_t value) {
        uint32_t curr_read  = que_->read_pos;
        uint32_t curr_write = que_->write_pos;
        uint32_t next_write = (curr_write+1) % que_->entry_limit;
      
        if(curr_read == next_write) {
          atomic::add(&que_->stat.overflowed_count, 1);
          return false;
        }
      
        Entry* pe = &que_->entries[curr_write];
        Entry  e = *pe;
        if(e.state != Entry::FREE) {
          atomic::compare_and_swap(&que_->write_pos, curr_write, next_write);
          return enqImpl(value);
        }

        Entry new_e = {Entry::USED, value};
        if(atomic::compare_and_swap(pe, e, new_e) == false) {
          return enqImpl(value);
        }
      
        atomic::compare_and_swap(&que_->write_pos, curr_write, next_write);      
        return true;
      }

      uint32_t deqImpl() {
        uint32_t curr_read  = que_->read_pos;
        uint32_t curr_write = que_->write_pos;
        uint32_t next_read = (curr_read+1) % que_->entry_limit;
      
        if(curr_read == curr_write) {
          return 0;
        }

        Entry* pe = &que_->entries[curr_read];
        Entry   e = *pe;
        if(e.state == Entry::FREE) {
          atomic::compare_and_swap(&que_->read_pos, curr_read, next_read);
          return deqImpl();
        }

        uint32_t new_version = atomic::fetch_and_add(&que_->version, 1);
        Entry new_e = {Entry::FREE, new_version};
        if(atomic::compare_and_swap(pe, e, new_e) == false) {
          return deqImpl();
        }
      
        atomic::compare_and_swap(&que_->read_pos, curr_read, next_read);
        return e.value;
      }

      static size_t queSize(size_t entry_limit) {
        return sizeof(Header) + sizeof(Entry)*entry_limit;
      }

    private:
      Header* que_;
      allocator::FixedAllocator alc_;
    };
  }
}

#endif 
