#ifndef IMQUE_ALLOCATOR_FIXED_ALLOCATOR_HH
#define IMQUE_ALLOCATOR_FIXED_ALLOCATOR_HH

#include "../atomic/atomic.hh"
#include "variable_allocator.hh"

#include <cassert>

namespace imque {
  namespace allocator {
    namespace FixedAllocatorAux {
      struct Block {
        uint32_t next;

        static const uint32_t END = 0xFFFFFFFF;
      };
      
      struct HeadBlock {
        uint32_t version;
        uint32_t next;
      };

      struct SuperBlock {
        uint32_t block_size;
        uint32_t used_count;
        uint32_t free_count;
        HeadBlock head;
      };
    }

    class FixedAllocator {
      typedef FixedAllocatorAux::Block Block;
      typedef FixedAllocatorAux::HeadBlock HeadBlock;
      typedef FixedAllocatorAux::SuperBlock SuperBlock;
      
      static const uint32_t SUPER_BLOCK_COUNT = 8;
      static const uint32_t BLOCK_SIZE_START = 32;
      static const uint32_t BLOCK_SIZE_LAST  = BLOCK_SIZE_START << (SUPER_BLOCK_COUNT-1);
      
    public:
      typedef uint32_t DESCRIPTOR_TYPE;

    public:
      FixedAllocator(void* region, uint32_t size) 
        : super_blocks_(reinterpret_cast<SuperBlock*>(region)),
          base_alc_(super_blocks_+SUPER_BLOCK_COUNT, 
                    size - sizeof(SuperBlock)*SUPER_BLOCK_COUNT),
          region_size_(size) {
      }

      operator bool() const { 
        return (super_blocks_ != NULL &&
                region_size_ > sizeof(SuperBlock)*SUPER_BLOCK_COUNT && 
                base_alc_);
      }

      void init() {
        if(*this) {
          base_alc_.init();

          uint32_t block_size = BLOCK_SIZE_START;
          for(uint32_t i=0; i < SUPER_BLOCK_COUNT; i++) {
            SuperBlock& sb = super_blocks_[i];
            sb.block_size = block_size;
            sb.used_count = 0;
            sb.free_count = 0;
            sb.head.version = 0;
            sb.head.next  = Block::END;
            
            block_size *= 2;
          }
        }
      }

      uint32_t allocate(uint32_t size) {
        if(size == 0) {
          return 0;
        }

        if(size > BLOCK_SIZE_LAST) {
          return base_alc_.allocate(size);
        }
      
        uint32_t sb_id = get_super_block_id(size);
        SuperBlock& sb = super_blocks_[sb_id-1];
      
        for(HeadBlock head = atomic::fetch(&sb.head);
            head.next != Block::END;
            head = atomic::fetch(&sb.head)) {
          assert(head.next != 0); // TODO: note
        
          Block block = *base_alc_.ptr<Block>(head.next);
          HeadBlock new_head = {head.version+1, block.next};
        
          if(atomic::compare_and_swap(&sb.head, head, new_head)) {
            atomic::add(&sb.used_count, 1);
            atomic::sub(&sb.free_count, 1);

            return encode_super_block_id(sb_id, head.next);
          }
        }

        uint32_t addr_desc = base_alc_.allocate(sb.block_size);
        if(addr_desc == 0) {
          return 0;
        }
      
        atomic::add_and_fetch(&sb.used_count,  1);
        return encode_super_block_id(sb_id, addr_desc);
      }

      bool release(uint32_t addr_desc) {
        if(addr_desc == 0) {
          return true;
        }

        uint32_t sb_id = decode_super_block_id(addr_desc);
        uint32_t base_addr_desc = decode_base_addr_desc(addr_desc);
        if(sb_id == 0) {
          return base_alc_.release(base_addr_desc);
        }
        assert(sb_id <= SUPER_BLOCK_COUNT);
        assert(base_addr_desc != 0);

        SuperBlock& sb = super_blocks_[sb_id-1];
        if(sb.used_count < sb.free_count &&
           base_alc_.fast_release(base_addr_desc)) {
          atomic::sub(&sb.used_count, 1);
          return true;
        }
        
        for(;;) {
          HeadBlock head = atomic::fetch(&sb.head);
          HeadBlock new_head = {head.version+1, base_addr_desc};
          base_alc_.ptr<Block>(new_head.next)->next = head.next;

          if(atomic::compare_and_swap(&sb.head, head, new_head)) {
            break;
          }
        }
        atomic::sub(&sb.used_count, 1);
        atomic::add(&sb.free_count, 1);
        return true;
      }

      template<typename T>
      T* ptr(uint32_t addr_desc) const { return base_alc_.ptr<T>(decode_base_addr_desc(addr_desc)); }
      
      template<typename T>
      T* ptr(uint32_t addr_desc, uint32_t offset) const { return base_alc_.ptr<T>(decode_base_addr_desc(addr_desc), offset); }

    private:
      static uint32_t get_super_block_id(uint32_t size) {
        uint32_t block_size = BLOCK_SIZE_START;
        uint32_t id=1;
        for(; block_size < size; id++) {
          block_size *= 2;
        }
        assert(id <= SUPER_BLOCK_COUNT);
        return id;
      }
      
      static uint32_t encode_super_block_id(uint32_t id, uint32_t addr_desc) {
        return addr_desc | (id >> 24);
      }

      static uint32_t decode_super_block_id(uint32_t encoded_addr_desc) {
        return encoded_addr_desc >> 24;
      }

      static uint32_t decode_base_addr_desc(uint32_t encoded_addr_desc) {
        return encoded_addr_desc & 0xFFFFFFFF;
      }

    private:
      SuperBlock* super_blocks_;
      VariableAllocator base_alc_;
      const uint32_t region_size_;
    };
  }
}

#endif
