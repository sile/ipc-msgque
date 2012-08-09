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

  template <typename FROM, typename TO>
  TO conv(FROM f) {
    union {
      FROM f;
      TO t;
    } u;
    u.f = f;
    return u.t;
  }

  struct Ver {
    uint32_t ver_;
    uint32_t val_;

    static Ver fromInt(uint64_t v) {
      return conv<uint64_t,Ver>(v);
    }

    uint64_t toInt() {
      return conv<Ver,uint64_t>(*this);
    }
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
      Ver u;
    };
  };

  union Handle {
    Handle() {}
    Handle(uint32_t handle) : intval(handle) {}

    struct {
      uint32_t pad:2;
      uint32_t sc:4;
      uint32_t idx:26;
    } u;
    uint32_t intval;
  };

  class BlockAllocator {
  public:
    BlockAllocator(void* region, uint32_t size) 
      : sb_(reinterpret_cast<SuperBlock*>(region)),
        alc_(sb_+6, //reinterpret_cast<char*>(region)+sizeof(SuperBlock)*6, 
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
        //sb_[i].head_ = 0xFFFFFFFF;
        sb_[i].u.ver_ = 0;
        sb_[i].u.val_ = 0xFFFFFFFF;
        
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
        Handle h;
        h.u.sc = 0;
        h.u.idx = alc_.allocate(size);
        return h.intval;
      }
      
      uint32_t sc = calc_super_block(size);

      SuperBlock& sb = sb_[sc-1];
      Handle h;
      h.u.sc = sc;

      for(Ver v = Ver::fromInt(__sync_add_and_fetch(&sb.head_, 0));
          v.val_ != 0xFFFFFFFF;
          v = Ver::fromInt(__sync_add_and_fetch(&sb.head_, 0))) {
        
        assert(v.val_ != 0);

        Block blk = *alc_.ptr<Block>(v.val_);
        if(v.ver_ != Ver::fromInt(__sync_add_and_fetch(&sb.head_, 0)).ver_) {
          // NOTE: 下のassertをなくせば、この分岐は不要
          continue;
        }
        uint32_t next = blk.next;
        assert(next != v.val_);
        
        Ver nv;
        nv.ver_ = v.ver_+1;
        nv.val_ = next;
        
        if(__sync_bool_compare_and_swap(&sb.head_, v.toInt(), nv.toInt())) {
          h.u.idx = v.val_;
          __sync_add_and_fetch(&sb.used_count_, 1);
          __sync_sub_and_fetch(&sb.free_count_, 1);
          return h.intval;
        }
      }
      
      uint32_t alloc_size = 32;
      for(uint32_t i=1; i < h.u.sc; i++) {
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

      if(used_count < free_count) {
        // TODO: 以下がfalseを返した場合は、freelistに追加するようにする
        return alc_.release(h.u.idx);
      }

      assert(h.u.idx != 0);
      Block* block = alc_.ptr<Block>(h.u.idx);
      for(;;) {
        //uint32_t head = sb.head_;
        Ver v = Ver::fromInt(__sync_add_and_fetch(&sb.head_, 0));
        
        block->next = v.val_; //head;
        assert(v.val_ != h.u.idx);
        Ver nv;
        nv.ver_ = v.ver_+1;
        nv.val_ = h.u.idx;

        if(__sync_bool_compare_and_swap(&sb.head_, v.toInt(), nv.toInt())) {
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
    SuperBlock* sb_; // 32,64,128,256,512,1024
    Allocator alc_;
  };
}

#endif
