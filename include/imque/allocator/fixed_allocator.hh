#ifndef IMQUE_ALLOCATOR_FIXED_ALLOCATOR_HH
#define IMQUE_ALLOCATOR_FIXED_ALLOCATOR_HH

#include "../atomic/atomic.hh"
#include "variable_allocator.hh"
#include <cassert>

namespace imque {
  namespace allocator {
    namespace FixedAllocatorAux {
      struct Block {
        uint32_t next; // index of next Block

        static const uint32_t END = 0xFFFFFFFF;
      };
      
      struct SuperBlock {
        uint32_t block_size;
        uint32_t used_count;
        uint32_t free_count;
        Block head;
      };
    }
    
    // ロックフリーな固定長ブロックアロケータ。
    // VariableAllocatorの上に構築されており BLOCK_SIZE_START から BLOCK_SIZE_LAST までの二の階乗サイズのブロックを扱うことが可能。
    // BLOCK_SIZE_LAST を越えるサイズのメモリ割当要求に対しては VariableAllocator に直接処理を委譲する。
    class FixedAllocator {
      typedef FixedAllocatorAux::Block Block;
      typedef FixedAllocatorAux::SuperBlock SuperBlock;
      
      static const uint32_t SUPER_BLOCK_COUNT = 7;
      static const uint32_t BLOCK_SIZE_START = 64;
      static const uint32_t BLOCK_SIZE_LAST  = BLOCK_SIZE_START << (SUPER_BLOCK_COUNT-1);
      static const uint32_t SUPER_BLOCKS_SIZE = sizeof(SuperBlock)*SUPER_BLOCK_COUNT;
      
    public:
      // region: 割当に使用するメモリ領域。
      // size: regionのサイズ
      FixedAllocator(void* region, uint32_t size) 
        : super_blocks_(reinterpret_cast<SuperBlock*>(region)),
          base_alc_(super_blocks_+SUPER_BLOCK_COUNT, 
                    size > SUPER_BLOCKS_SIZE ? size - SUPER_BLOCKS_SIZE : 0),
          region_size_(size) {
      }

      operator bool() const { return super_blocks_ != NULL && base_alc_; }

      // 初期化メソッド。
      // コンストラクタに渡した region につき一回呼び出す必要がある。
      void init() {
        if(*this) {
          base_alc_.init();

          uint32_t block_size = BLOCK_SIZE_START;
          for(uint32_t i=0; i < SUPER_BLOCK_COUNT; i++) {
            SuperBlock& sb = super_blocks_[i];
            sb.block_size = block_size;
            sb.used_count = 0;
            sb.free_count = 0;
            sb.head.next  = Block::END;
            
            block_size *= 2;
          }
        }
      }

      // メモリ割当を行う。
      // 要求したサイズの割当に失敗した場合は 0 を、それ以外はメモリ領域参照用の識別子(記述子)を返す。
      // (識別子を ptrメソッド に渡すことで、実際のメモリ領域を参照可能)
      uint32_t allocate(uint32_t size) {
        if(size == 0) {
          return 0;
        }
        
        if(size > BLOCK_SIZE_LAST) {
          return base_alc_.allocate(size);
        }

        uint32_t sb_id = getSuperBlockId(size);
        SuperBlock& sb = super_blocks_[sb_id-1];
      
        // まずキャッシュからのブロック取得を試みる
        for(Block head = atomic::fetch(&sb.head);
            head.next != Block::END;
            head = atomic::fetch(&sb.head)) {
          Block block = *base_alc_.ptr<Block>(head.next);
          Block new_head = {block.next};
        
          if(atomic::compare_and_swap(&sb.head, head, new_head)) {
            atomic::add(&sb.used_count, 1);
            atomic::sub(&sb.free_count, 1);

            return base_alc_.dupNew(head.next); // キャッシュから再利用
          }
        }

        // キャッシュには利用可能なブロックがないので、可変長ブロックアロケータに割当を依頼する
        uint32_t md = base_alc_.allocate(sb.block_size); // memory descriptor
        if(md == 0) {
          return 0;
        }

        atomic::add(&sb.used_count, 1);
        return md;
      }

      // allocateメソッドで割り当てたメモリ領域を解放する。(解放に成功した場合は trueを、失敗した場合は false を返す)
      // md(メモリ記述子)が 0 の場合は何も行わない。
      bool release(uint32_t md) {
        if(md == 0) {
          return true;
        }
        if(! base_alc_.undup(md)) {
          return true; // まだ誰かが参照中
        }

        uint32_t sb_id = getSuperBlockId(base_alc_.getSize(md));
        if(sb_id == 0) {
          return base_alc_.release(md);
        }
        assert(sb_id <= SUPER_BLOCK_COUNT);

        SuperBlock& sb = super_blocks_[sb_id-1];

        // キャッシュに溜めておく必要がないなら、ブロックを解放する
        if(sb.used_count < sb.free_count &&
           base_alc_.lightRelease(md)) {
          atomic::sub(&sb.used_count, 1);
          return true;
        }
        
        // キャッシュが不足しているか、高競合下によりブロック解放に失敗した場合は、キャッシュに追加する
        for(;;) {
          Block head = atomic::fetch(&sb.head);
          Block new_head = {md};
          base_alc_.ptr<Block>(new_head.next)->next = head.next;
          
          if(atomic::compare_and_swap(&sb.head, head, new_head)) {
            break;
          }
        }

        atomic::sub(&sb.used_count, 1);
        atomic::add(&sb.free_count, 1);
        return true;
      }

      bool dup(uint32_t md, uint32_t delta=1) {
        return base_alc_.dup(md, delta);
      }

      // allocateメソッドが返したメモリ記述子から、対応する実際にメモリ領域を取得する
      template<typename T>
      T* ptr(uint32_t md) const { return base_alc_.ptr<T>(md); }
      
      template<typename T>
      T* ptr(uint32_t md, uint32_t offset) const { return base_alc_.ptr<T>(md, offset); }

    private:
      static uint32_t getSuperBlockId(uint32_t size) {
        uint32_t block_size = BLOCK_SIZE_START;
        uint32_t id=1;
        for(; block_size < size; id++) {
          block_size *= 2;
        }
        assert(id <= SUPER_BLOCK_COUNT);
        return id;
      }
      
    private:
      SuperBlock* super_blocks_;
      VariableAllocator base_alc_;
      const uint32_t region_size_;
    };
  }
}

#endif
