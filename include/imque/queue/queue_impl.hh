#ifndef IMQUE_QUEUE_QUEUE_IMPL_HH
#define IMQUE_QUEUE_QUEUE_IMPL_HH

#include "../atomic/atomic.hh"
#include "../ipc/shared_memory.hh"
#include "../allocator/fixed_allocator.hh"
#include <inttypes.h>
#include <string.h>
#include <algorithm>

namespace imque {
  namespace queue {
    static const char MAGIC[] = "IMQUE-0.1.1";

    // FIFOキュー
    class QueueImpl {
      struct Node {
        uint32_t next;
        uint32_t data_size;
        char data[0];
        
        static const uint32_t END = 0;
      };

      struct Header {
        char magic[sizeof(MAGIC)];
        uint32_t shm_size;

        volatile uint32_t head;  // NOTE: mdを保持。md自体がABA対策がなされているので、ここではそれ用のフィールドは不要。
        volatile uint32_t tail;
        
        uint32_t overflowed_count;
      };
      static const uint32_t HEADER_SIZE = sizeof(Header);

      // 参照カウント周りの処理隠蔽用のクラス
      class NodeRef {
      public:
        NodeRef(uint32_t md, allocator::FixedAllocator& alc) 
          : alc_(alc), md_(0) {
          if(alc.dup(md)) { // 既に解放されている可能性もあるのでチェックする
            md_ = md;
          }
        }
        
        ~NodeRef() {
          if(md_) {
            alc_.release(md_);
          }
        }

        operator bool() const { return md_ != 0; }

        Node node_copy() const { return atomic::fetch(alc_.ptr<Node>(md_)); }
        uint32_t& node_next() { return alc_.ptr<Node>(md_)->next; }
        uint32_t md() const { return md_; }
        
      private:
        allocator::FixedAllocator& alc_;
        uint32_t md_;
      };

    public:
      QueueImpl(ipc::SharedMemory& shm)
        : shm_size_(shm.size()),
          que_(shm.ptr<Header>()),
          alc_(shm.ptr<void>(HEADER_SIZE), std::max(0, static_cast<int32_t>(shm.size() - HEADER_SIZE))) {
      }

      operator bool() const { return alc_ && que_; }
    
      // 初期化メソッド。
      // コンストラクタに渡した一つの shm につき、一回呼び出す必要がある。
      void init() {
        if(*this) {
          alc_.init();
      
          uint32_t sentinel = alc_.allocate(sizeof(Node));
          if(sentinel == 0) {
            que_ = NULL;
            return;
          }

          memcpy(que_->magic, MAGIC, sizeof(MAGIC));
          que_->shm_size = shm_size_;
          
          alc_.ptr<Node>(sentinel)->next = Node::END;
          
          que_->head = sentinel;
          que_->tail = sentinel;
          assert(alc_.dup(sentinel)); // head と tail の二箇所から参照されているので、参照カウントを一つ増やしておく

          que_->overflowed_count = 0;
        }
      }

      // 重複初期化チェック(簡易)付きの初期化メソッド。
      // 共有メモリ用のファイルを使い回している場合は、二回目以降は明示的なinit()呼び出しを行った方が安全。
      void init_once() {
        if(*this && (memcmp(que_->magic, MAGIC, sizeof(MAGIC)) != 0 || 
                     shm_size_ != que_->shm_size)) {
          init();
        }
      }

      // キューに要素を追加する (キューに空きがない場合は false を返す)
      bool enq(const void* data, size_t size) {
        return enqv(&data, &size, 1);
      }

      // キューに要素を追加する (キューに空きがない場合は false を返す)
      // datav および sizev は count 分のサイズを持ち、それらを全て結合したデータがキューには追加される
      bool enqv(const void** datav, size_t* sizev, size_t count) {
        size_t total_size = 0;
        for(size_t i=0; i < count; i++) {
          total_size += sizev[i];
        }
        
        uint32_t md = alc_.allocate(sizeof(Node) + total_size); // md = memory descriptor
        if(md == 0) {
          atomic::add(&que_->overflowed_count, 1);
          return false;
        }

        Node* node = alc_.ptr<Node>(md);
        node->next = Node::END;
        node->data_size = total_size;

        size_t offset = sizeof(Node);
        for(size_t i=0; i < count; i++) {
          memcpy(alc_.ptr<void>(md, offset), datav[i], sizev[i]);
          offset += sizev[i];
        }

        enqImpl(md);
        return true;
      }

      // キューから要素を取り出し buf に格納する (キューが空の場合は false を返す)
      bool deq(std::string& buf) {
        uint32_t md = deqImpl();
        if(md == 0) {
          return false;
        }

        Node* node = alc_.ptr<Node>(md);
        buf.assign(node->data, node->data_size);
      
        assert(alc_.release(md));
        return true;
      }
      
      // キューが空かどうか
      bool isEmpty() {
        for(;;) {
          NodeRef head_ref(que_->head, alc_);
          if(! head_ref) {
            continue; 
          }

          return head_ref.node_copy().next == Node::END;
        }
      }

      // キューへの要素追加に失敗した回数を返す
      size_t overflowedCount() const { return que_->overflowed_count; }
      size_t resetOverflowedCount() { 
        return atomic::fetch_and_clear(&que_->overflowed_count);
      }

    private:
      void enqImpl(uint32_t new_tail) {
        assert(alc_.dup(new_tail, 2)); // head と tail からの参照分を始めにカウントしておく

        for(;;) {
          NodeRef tail_ref(que_->tail, alc_);
          if(! tail_ref) {
            continue;
          }

          Node node = tail_ref.node_copy();
          if(node.next != Node::END) {
            // tail が末尾を指していないので、一つ前に進める
            if(atomic::compare_and_swap(&que_->tail, tail_ref.md(), node.next)) {
              alc_.release(tail_ref.md());
            } 
            continue;
          }

          if(atomic::compare_and_swap(&tail_ref.node_next(), node.next, new_tail)) {
            break;
          }
        }
      }

      uint32_t deqImpl() {
        for(;;) {
          NodeRef head_ref(que_->head, alc_);
          if(! head_ref) {
            continue;
          }

          Node node = head_ref.node_copy();
          if(node.next == Node::END) {
            return 0;
          }

          if(atomic::compare_and_swap(&que_->head, head_ref.md(), node.next)) {
            alc_.release(head_ref.md());
            return node.next;
          }
        }
      }

    private:
      const uint32_t shm_size_; 

      Header* que_;
      allocator::FixedAllocator alc_;
    };
  }
}

#endif 
