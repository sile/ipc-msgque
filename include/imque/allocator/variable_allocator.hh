#ifndef IMQUE_ALLOCATOR_VARIABLE_ALLOCATOR_HH
#define IMQUE_ALLOCATOR_VARIABLE_ALLOCATOR_HH

#include "../atomic/atomic.hh"
#include <cassert>
#include <inttypes.h>

namespace imque {
  namespace allocator {
    namespace VariableAllocatorAux {
      struct Node {
        uint32_t version:6; // tag for ABA problem
        uint32_t next:26;   // index of next Node
        uint32_t count:30;  // avaiable Chunk count
        uint32_t status:2;
        
        enum STATUS {
          AVAILABLE = 0,
          JOIN_HEAD = 1, 
          JOIN_TAIL = 2
        };
        
        bool isAvaiable() const { return status == AVAILABLE; }
        bool isJoinHead() const { return status & JOIN_HEAD; }
        bool isJoinTail() const { return status & JOIN_TAIL; }

        Node join(const Node& tail_node) const {
          return (Node){tail_node.version+1,
                        tail_node.next,
                        count + tail_node.count,
                        (status & ~Node::JOIN_HEAD) | (tail_node.status & ~Node::JOIN_TAIL)};
        }

        Node changeNext(uint32_t new_next) const { return (Node){version+1, new_next, count, status}; }
        Node changeCount(uint32_t new_count) const { return (Node){version+1, next, new_count, status}; }
        Node changeStatus(uint32_t new_status) const { return (Node){version+1, next, count, new_status}; }
      };

      struct Chunk {
        char padding[32];
      };
    }
    
    // variable-size-block allocator
    class VariableAllocator {
    private:
      typedef VariableAllocatorAux::Node Node;
      typedef atomic::Snapshot<Node> NodeSnapshot;
      typedef VariableAllocatorAux::Chunk Chunk;
      
      static const int RETRY_LIMIT = 32;
      static const int FAST_RETRY_LIMIT = 1;
      static const uint32_t NODE_COUNT_LIMIT = 0x1000000; // 24bit

    public:
      typedef uint32_t DESCRIPTOR_TYPE;

    public:
      VariableAllocator(void* region, uint32_t size)
        : node_count_(size/(sizeof(Node)+sizeof(Chunk))),
          nodes_(reinterpret_cast<Node*>(region)),
          chunks_(reinterpret_cast<Chunk*>(nodes_+node_count_)) {
      }
      
      operator bool() const { return nodes_ != NULL && node_count_ > 2 && node_count_ < NODE_COUNT_LIMIT; }

      void init() {
        if(*this) {
          nodes_[0].next   = 1;
          nodes_[0].count  = 0;
          nodes_[0].status = Node::AVAILABLE;
          
          nodes_[1].next   = node_count_;
          nodes_[1].count  = node_count_-1;
          nodes_[1].status = Node::AVAILABLE;
        }
      }
      
      // TODO: doc: 返り値の上限について
      uint32_t allocate(uint32_t size) {
        if(size == 0) {
          return 0; // invalid argument
        }

        uint32_t need_chunk_count = (size+sizeof(Chunk)-1) / sizeof(Chunk);
      
        NodeSnapshot cand;
        if(find_candidate(IsEnoughChunk(need_chunk_count), cand) == false) {
          return 0; // out of memory
        }

        uint32_t new_count = cand.node().count - need_chunk_count;
        if(cand.compare_and_swap(cand.node().changeCount(new_count)) == false) {
          return allocate(size);
        }
      
        uint32_t allocated_node_index = index(cand) + new_count;
        nodes_[allocated_node_index].version = cand.node().version + 1;
        nodes_[allocated_node_index].count = need_chunk_count;
        nodes_[allocated_node_index].status = Node::AVAILABLE;
        
        return allocated_node_index;
      }

      bool release(uint32_t descriptor) {
        return release_impl(descriptor, RETRY_LIMIT, false);
      }
      
      // TODO: note
      bool fast_release(uint32_t descriptor) {
        return release_impl(descriptor, FAST_RETRY_LIMIT, true);
      }

      template<typename T>
      T* ptr(uint32_t descriptor) const { return reinterpret_cast<T*>(chunks_ + descriptor); }

      template<typename T>
      T* ptr(uint32_t descriptor, uint32_t offset) const { return reinterpret_cast<T*>(ptr<char>(descriptor)+offset); }
      
    private:
      struct IsEnoughChunk {
        IsEnoughChunk(uint32_t need_chunk_count) : count_(need_chunk_count) {}
        
        bool operator()(const NodeSnapshot& curr) const {
          return curr.node().isAvaiable() && curr.node().count > count_;
        }
        
        const uint32_t count_;
      };

      struct IsPredecessor {
        IsPredecessor(uint32_t node_index) : node_index_(node_index) {}
        
        bool operator()(const NodeSnapshot& curr) const {
          return node_index_ < curr.node().next;
        }

        uint32_t node_index_;
      };
      
      template<class Callback>
      bool find_candidate(const Callback& fn, NodeSnapshot& node, int retry=RETRY_LIMIT) {
        NodeSnapshot head(&nodes_[0]);
        return find_candidate(fn, head, node, retry);
      }

      template<class Callback>
      bool find_candidate(const Callback& fn, NodeSnapshot& pred, NodeSnapshot& curr, int retry) {
        if(retry < 0) {
          return false;
        }
        
        if(pred.node().next == node_count_) { // 終端ノードに達した
          return false;
        }
        
        if(get_next_snapshot(pred, curr) == false ||  
           update_node_status(pred, curr) == false ||
           join_nodes_if_need(pred, curr) == false) { 
          return find_candidate(fn, curr, retry-1);
        }

        if(fn(curr)) {
          return true;
        }

        pred = curr;
        return find_candidate(fn, pred, curr, retry);
      }

      bool get_next_snapshot(NodeSnapshot& pred, NodeSnapshot& curr) const {
        assert(pred.node().next != node_count_);
        
        curr.update(&nodes_[pred.node().next]);
        if(pred.isModified()) {
          return false;
        }
        
        assert(curr.node().isJoinHead()==false || isJoinable(curr));

        if(pred.node().isJoinHead()==false && curr.node().isJoinTail()) {
          return false;
        }

        return true;
      }

      bool update_node_status(NodeSnapshot& pred, NodeSnapshot& curr) {
        if(isJoinable(pred) == false) {
          return true;
        }

        return (pred.compare_and_swap(pred.node().changeStatus(pred.node().status | Node::JOIN_HEAD)) &&
                curr.compare_and_swap(curr.node().changeStatus(curr.node().status | Node::JOIN_TAIL)));
      }
    
      bool join_nodes_if_need(NodeSnapshot& pred, NodeSnapshot& curr) {
        if(! (pred.node().isJoinHead() && curr.node().isJoinTail())) {
          return true;
        }
        assert(isJoinable(pred));
      
        Node new_pred_node = pred.node().join(curr.node());
        if(pred.compare_and_swap(new_pred_node) == false) {
          return false;
        }

        curr = pred;
        return true;
      }

      uint32_t index(const NodeSnapshot& node) const {
        return node.place() - nodes_;
      }

      bool isJoinable(const NodeSnapshot& node) const {
        return node.node().next == index(node) + node.node().count; 
      }

      bool release_impl(uint32_t descriptor, int retry_limit, bool fast) {
        if(descriptor == 0 || descriptor >= node_count_) {
          assert(descriptor < node_count_);
          return true;
        }

        uint32_t node_index = descriptor;
        NodeSnapshot pred;
        if(find_candidate(IsPredecessor(node_index), pred, retry_limit) == false) {
          return false;
        }
        assert(node_index >= index(pred)+pred.node().count);
        assert(pred.node().isAvaiable());

        Node* node = &nodes_[node_index];
        Node new_pred_node;
        bool is_neighbor = node_index == index(pred)+pred.node().count;
        if(is_neighbor) {
          new_pred_node = pred.node().changeCount(pred.node().count + node->count);
        } else {
          new_pred_node = pred.node().changeNext(node_index);
          node->next = pred.node().next;
        }
      
        if(pred.compare_and_swap(new_pred_node) == false) {
          return fast ? false : release_impl(descriptor, retry_limit, fast);
        }
      
        return true;
      }

    private:
      const uint32_t node_count_;
      Node*          nodes_;
      Chunk*         chunks_;      
    };
  }
}

#endif
