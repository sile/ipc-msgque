#ifndef IMQUE_QUEUE_QUEUE_IMPL_HH
#define IMQUE_QUEUE_QUEUE_IMPL_HH

#include "../atomic/atomic.hh"
#include "../ipc/shared_memory.hh"
#include "../allocator/fixed_allocator.hh"
#include <inttypes.h>
#include <string.h>

namespace imque {
  namespace queue {
    static const char MAGIC[] = "IMQUE-0.0.5";

    // FIFOキュー
    class QueueImpl {
      struct Node {
        uint32_t next;
        uint32_t data_size;
        char data[0];
        
        static const uint32_t END = 0;
      };

      struct Stat {
        uint32_t overflowed_count;
      };

      struct Header {
        char magic[sizeof(MAGIC)];
        uint32_t shm_size;

        volatile uint32_t head;
        volatile uint32_t tail;
        
        Stat stat;
      };

    public:
      QueueImpl(size_t entry_limit, ipc::SharedMemory& shm)
        : shm_size_(shm.size()),
          que_(shm.ptr<Header>()),
          alc_(shm.ptr<void>(queSize(entry_limit)),
               shm.size() > queSize(entry_limit) ?  shm.size() - queSize(entry_limit) : 0) {
      }

      operator bool() const { return que_ != NULL && alc_; }
    
      // 初期化メソッド。
      // コンストラクタに渡した一つの shm につき、一回呼び出す必要がある。
      void init() {
        if(*this) {
          alc_.init();
      
          memcpy(que_->magic, MAGIC, sizeof(MAGIC));
          que_->shm_size = shm_size_;
          
          uint32_t md = alc_.allocate(sizeof(Node)); // XXX: 失敗時の処理
          assert(md != 0);
          
          alc_.ptr<Node>(md)->next = Node::END;
          
          que_->head = md;
          que_->tail = md;

          que_->stat.overflowed_count = 0;
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

        uint32_t alloc_id = alc_.allocate(sizeof(Node) + total_size);
        if(alloc_id == 0) {
          atomic::add(&que_->stat.overflowed_count, 1);
          return false;
        }

        Node* node = alc_.ptr<Node>(alloc_id);
        node->next = Node::END;
        node->data_size = total_size;

        size_t offset = sizeof(Node);
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
        uint32_t md = deqImpl();
        if(md == 0) {
          return false;
        }

        Node* node = alc_.ptr<Node>(md);
        buf.assign(node->data, node->data_size);
      
        assert(alc_.release(md));
      
        return true;
      }

      bool isEmpty() const { return que_->head == que_->tail; } // XXX: 不正確 (両者はズレる可能性あり)。もしバージョンを導入するなら、その比較を行った方が正確

      bool isFull()  const { return false; }   // XXX: dummy
      size_t entryCount() const { return 10; } // XXX: dummy
      
      // キューへの要素追加に失敗した回数を返す
      size_t overflowedCount() const { return que_->stat.overflowed_count; }
      void resetOverflowedCount() { que_->stat.overflowed_count = 0; }

    private:
      bool enqImpl(uint32_t new_tail) {
        for(;;) {
          uint32_t tail = que_->tail;
          if(alc_.refincr(tail) == false) { // TODO: pointer取得と統合してしまっても良いかもしれない
            continue;
          }
          
          Node node = atomic::fetch(alc_.ptr<Node>(tail));
          if(node.next != Node::END) {
            atomic::compare_and_swap(&que_->tail, tail, node.next);
            alc_.release(tail);
            continue;
          }

          if(atomic::compare_and_swap(&alc_.ptr<Node>(tail)->next, node.next, new_tail)) {
            atomic::compare_and_swap(&que_->tail, tail, new_tail);
            alc_.release(tail);
            return true;
          }
          alc_.release(tail);
        }
        
        return true;
      }

      uint32_t deqImpl() {
        for(;;) {
          uint32_t head = que_->head;
          if(alc_.refincr(head) == false) {
            continue;
          }

          Node node = atomic::fetch(alc_.ptr<Node>(head));
          if(node.next == Node::END) {
            alc_.release(head);
            return 0;
          }

          if(atomic::compare_and_swap(&que_->head, head, node.next)) {
            alc_.release(head);
            return head;
          }
        }
      }

      static size_t queSize(size_t entry_limit) {
        return sizeof(Header);
      }

    private:
      const uint32_t shm_size_;    // for check

      Header* que_;
      allocator::FixedAllocator alc_;
    };
  }
}

#endif 
