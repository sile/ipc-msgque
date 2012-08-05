#ifndef IMQUE_BLOCK_ALLOCATOR_HH
#define IMQUE_BLOCK_ALLOCATOR_HH

#include "allocator.hh"
#include <unistd.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <assert.h>

namespace imque {
  struct Block {
    uint32_t next;
  };

  struct SuperBlock {
    SuperBlock(uint32_t block_size) 
      : block_size_(block_size),
        used_count_(0),
        free_count_(0),
        head_(0xFFFFFFFF) {}
    
    uint32_t block_size_;
    uint32_t used_count_;
    uint32_t free_count_;
    union {
      uint64_t head_;
      struct {
        uint32_t ver_;
        uint32_t val_;
      } u;
    }
  };

  union Handle {
    Handle() {}
    Handle(uint32_t handle) : intval(handle) {}

    struct {
      uint32_t sc:4;
      uint32_t idx:28;
    } u;
    uint32_t intval;
  };

  class BlockAllocator {
  public:
    BlockAllocator(void* region, uint32_t size) 
      : sb_(reinterpret_cast<SuperBlock*>(region)),
        alc_(reinterpret_cast<char*>(region)+sizeof(SuperBlock)*6, 
             size - sizeof(SuperBlock)*6) {
    }

    operator bool() const { return alc_; }

    void init() {
      alc_.init();

      uint32_t block_size = 32;
      for(int i=0; i < 6; i++) {
        sb_[i].block_size_ = block_size;
        sb_[i].used_count_ = 0;
        sb_[i].free_count_ = 0;
        sb_[i].head_ = 0xFFFFFFFF;
        sb_[i].ver_ = 0;
        
        block_size *= 2;
      }
    }

    uint32_t calc_super_block(uint32_t size) {
      uint32_t block_size = 32;
      uint32_t sc = 0;
      for(; block_size < size; sc++) {
        block_size *= 2;
      }
      assert(sc < 6);
      return sc+1;
    }

    uint32_t allocate(uint32_t size) {
      if(size > 1024) {
        return alc_.allocate(size);
      }
      
      uint32_t sc = calc_super_block(size);

      SuperBlock& sb = sb_[sc-1];
      Handle h;
      h.u.sc = sc;

      for(uint32_t head=sb.head_; head != 0xFFFFFFFF; head=sb.head_) {
        Block blk = *alc_.ptr<Block>(head); // XXX: ABA problem
        uint32_t next = blk.next;
        assert(next != head);
        if(__sync_bool_compare_and_swap(&sb.head_, head, next)) {
          h.u.idx = head;
          __sync_add_and_fetch(&sb.used_count_, 1);
          __sync_sub_and_fetch(&sb.free_count_, 1);
          return h.intval;
        }
      }
      
      uint32_t alloc_size = 32;
      for(int i=1; i < h.u.sc; i++) {
        alloc_size *= 2;
      }
      assert(alloc_size >= size);

      h.u.idx = alc_.allocate(alloc_size);
      if(h.u.idx == 0) {
        return 0;
      }
      __sync_add_and_fetch(&sb.used_count_, 1);
      return h.intval;
    }

    bool release(uint32_t handle) {
      if(handle == 0) {
        return true;
      }

      Handle h(handle);

      if(h.u.sc == 0) {
        return alc_.release(h.u.idx);
      }

      SuperBlock& sb = sb_[h.u.sc-1];

      uint32_t used_count = sb.used_count_;
      uint32_t free_count = sb.free_count_;

      __sync_sub_and_fetch(&sb.used_count_, 1);

      if(used_count*2 < free_count) {
        return alc_.release(h.u.idx);
      }

      assert(h.u.idx != 0);
      Block* block = alc_.ptr<Block>(h.u.idx);
      for(;;) {
        uint32_t head = sb.head_;
        block->next = head;
        assert(head != h.u.idx);
        if(__sync_bool_compare_and_swap(&sb.head_, head, h.u.idx)) {
          break;
        }
      }

      __sync_add_and_fetch(&sb.free_count_, 1);
      return true;
    }
    
    template<typename T>
    T* ptr(uint32_t index) const { return alc_.ptr<T>(Handle(index).u.idx); }

    template<typename T>
    T* ptr(uint32_t index, uint32_t offset) const { return alc_.ptr<T>(Handle(index).u.idx, offset); }
    
  private:
    Allocator alc_;
    SuperBlock* sb_; // 32,64,128,256,512,1024
  };
}

#endif
