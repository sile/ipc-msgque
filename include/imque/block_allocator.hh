#ifndef IMQUE_BLOCK_ALLOCATOR_HH
#define IMQUE_BLOCK_ALLOCATOR_HH

#include "allocator.hh"
#include <unistd.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <assert.h>

namespace imque {
  struct Block {
    uint32_t state:2;
    uint32_t next:30;
    char data[0];
    
    static const uint32_t FREE = 0;
    static const uint32_t USED = 1;
    static const uint32_t ISOLATED = 2;
  };

  class SuperBlock {
  public:
    static const int BLOCK_SIZE = 2048;

    uint32_t per_block_size;
    uint32_t free_count;
    uint32_t block_head;
    
    Block* get_block(uint32_t index, uint32_t block_size) {
      return reinterpret_cast<Block*>(reinterpret_cast<char*>(this+1) + block_size*index);
    }

    uint32_t pop(uint32_t block_size) {
      uint32_t cur = block_head;
      block_head = get_block(cur, block_head)->next;
      free_count--;
      return cur;
    }

    void push(uint32_t index, uint32_t block_size) {
      Block* blk = get_block(index, block_size);
      blk->next = head;
      block_head = index;
      free_count++;
    }
  };

  class Heap {
    static const int START_SIZE = 16;
    
    uint32_t super_block_head[7];

  public:
    bool acquire_block(uint32_t sc, uint16_t& sb_id, uint32_t& block_id, Allocator& alc) {
      uint32_t block_head = super_block_head[sc];
      
      uint32_t per_block_size = START_SIZE * (2^sc);
      uint32_t block_count = SuperBlock::BLOCK_SIZE / per_block_size;
      
      if(block_head == 0) {
        // TODO: 並列化
        super_block_head[sc] = block_head = alc.allocate(sizeof(SuperBlock)+
                                                         block_count * (per_block_size + sizeof(uint32_t)));
        SuperBlock* sb2 = alc.ptr<SuperBlock>(block_head);
        sb2->per_block_size = START_SIZE * (2^sc);
        sb2->free_count = block_count;
        sb2->block_head = 0;

        for(int i=0; i < block_count; i++) {
          sb2->get_block(i, per_block_size)->next  = i+1;
          sb2->get_block(i, per_block_size)->state = Block::FREE;
        }
        sb2->get_block(block_count-1, per_block_size)->next = 0;
      }
      
      // TODO: find_super_block
      SuperBlock* sb = alc.ptr<SuperBlock>(block_head);
      
      return true;
    }

    void init() {
      for(int i=0; i < 7; i++) {
        super_block_head[i] = 0;
      }
    }

  public:
    static uint32_t calc_size_class(uint32_t size) {
      uint32_t sc = 0;
      uint32_t  n = START_SIZE;
      while(size > n && sc < 7) {
        sc++;
        n += n;
      }
      assert(sc < 7); // XXX:

      return sc;
    }
  };

  struct Handle {
    uint16_t heap_id;
    uint16_t sb_id;
    uint32_t block_id;

    operator bool() const { return block_id != 0; }

    Handle(uint16_t heap, uint16_t sb, uint32_t block)
      : heap_id(heap), sb_id(sb), block_id(block) {}
  };

  class BlockAllocator {
    static const unsigned GOLDEN_RATIO_PRIME=0x9e370001; //(2^31) + (2^29) - (2^25) + (2^22) - (2^19) - (2^16) + 1;
    static const unsigned HEAP_COUNT = 4;

  public:
    BlockAllocator(void* region, uint32_t size) 
      : heaps_(reinterpret_cast<Heap*>(region)), // TODO: check size
        base_alloca_(heaps_+HEAP_COUNT + 1, size - sizeof(Heap)*(HEAP_COUNT+1)) {
    }

    ~BlockAllocator() {
    }
    
    void init() {
      base_alloca_.init();

      for(unsigned i=0; i < HEAP_COUNT+1; i++) {
        heaps_[i].init();
      }
    }

    operator bool() const { return base_alloca_; }

    Handle allocate(uint32_t size) {
      if(size > SuperBlock::BLOCK_SIZE / 2) {
        uint32_t index = base_alloca_.allocate(size);
        return Handle(0,0,index);
      }

      uint16_t heap_id = get_heap_id();
      Heap& heap = heaps_[heap_id];
      uint32_t sc = Heap::calc_size_class(size);

      uint16_t sb_id;
      uint32_t block_id;
      heap.acquire_block(sc, sb_id, block_id, base_alloca_);
      
      return Handle(0,0,0);
    }
    
    bool release(Handle handle) {
      if(handle.heap_id == 0 && handle.sb_id == 0) {
        return base_alloca_.release(handle.block_id);
      }
      
      return true;
    }

  private:
    uint16_t get_heap_id() {
      return (static_cast<unsigned>(syscall(SYS_gettid)) * GOLDEN_RATIO_PRIME % HEAP_COUNT) + 1;
    }
    
  private:
    Allocator base_alloca_;
    Heap* heaps_; //[HEAP_COUNT + 1];
  };
}

#endif
