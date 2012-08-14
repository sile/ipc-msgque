#ifndef IMQUE_ALLOCATOR_VARIABLE_ALLOCATOR_HH
#define IMQUE_ALLOCATOR_VARIABLE_ALLOCATOR_HH

#include "../atomic/atomic.hh"

#include <inttypes.h>
#include <cassert>
#include <cstring>

namespace imque {
  namespace allocator {
    namespace VariableAllocatorAux {
      struct Node {
        uint32_t next;
        uint32_t count:30;
        uint32_t status:2;
        
        enum STATUS {
          AVAILABLE = 0, 
          JOIN_HEAD = 1, 
          JOIN_TAIL = 2
        };
      };

      class NodeSnapshot {
        NodeSnapshot() : ptr_(NULL) {}
        NodeSnapshot(Node* ptr) { update(ptr); }
      
        void update(Node* ptr) {
          ptr_ = ptr;
          val_ = atomic::fetch(ptr);
        }
        
        const Node& node() const { return val_; }
        uint32_t index(Node* head) const { return ptr_ - head; }

        bool isModified() const { 
          Node tmp_val = atomic::fetch(ptr_);
          return memcmp(&tmp_val, &val_, sizeof(Node)) != 0;
        }
        
        bool compare_and_swap(const Node& new_val) {
          if(atomic::compare_and_swap(ptr_, val_, new_val)) {
            val_ = new_val;
            return true;
          }
          return false;
        }

      private:
        Node* ptr_;
        Node  val_;
      };
      
      struct Chunk {
        char padding[32];
      };
    }

    class VariableAllocator {
    public:
      
    private:
      
    };
  }
}

#endif
