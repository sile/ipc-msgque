#ifndef IMQUE_ALLOCATOR_HH
#define IMQUE_ALLOCATOR_HH

#include <inttypes.h>
#include <cassert>

#include <iostream> // XXX:

namespace imque {
  class Allocator {
    struct Node {  // node of free-list
      uint32_t next;  // next free block index
      uint32_t count:30;     // available block count
      uint32_t status:2;

      enum STATUS {
        FREE      = 0,
        JOIN_HEAD = 1,
        JOIN_TAIL = 2
      };

      uint64_t* as_uint64_ptr() { return reinterpret_cast<uint64_t*>(this); }
      uint64_t as_uint64() const { return *reinterpret_cast<const uint64_t*>(this); }
    } __attribute__((__packed__));

    struct Chunk {
      char padding[32];
    };

    class Snapshot {
    public:
      Snapshot() : ptr_(NULL) {}
      Snapshot(Node* ptr) : ptr_(ptr), val_(*ptr) {}
      
      void update(Node* ptr) {
        ptr_ = ptr;
        val_ = *ptr;
      }

      const Node& node() const { return val_; }
      bool isModified() const { return ptr_->as_uint64() != val_.as_uint64(); }
      bool isJoinable(Node* head) const { return val_.next == index(head) + val_.count; }
      
      bool compare_and_swap(const Node& new_val) {
        if(isModified() == false &&
           __sync_bool_compare_and_swap(ptr_->as_uint64_ptr(), val_.as_uint64(), new_val.as_uint64())) {
          val_ = new_val;
          return true;
        }
        return false;
      }

      uint32_t index(Node* head) const { return ptr_ - head; }
      
    private:
      Node* ptr_;
      Node  val_;
    };

    static const uint32_t MAX_RETRY_COUNT = 10; // XXX: for test

  public:
    Allocator(void* region, uint32_t size) 
      : node_count_(size/(sizeof(Node)+sizeof(Chunk))),
        nodes_(reinterpret_cast<Node*>(region)),
        chunks_(reinterpret_cast<Chunk*>(nodes_+node_count_)) {
    }

    operator bool() const { return node_count_ > 2; }
    
    void init() {
      nodes_[0].next   = 1;
      nodes_[0].count  = 0;
      nodes_[0].status = Node::FREE;

      nodes_[1].next   = node_count_;
      nodes_[1].count  = node_count_-1;
      nodes_[1].status = Node::FREE;
    }

    uint32_t allocate(uint32_t size) {
      if(size == 0) {
        return 0; // invalid argument
      }

      uint32_t required_chunk_count = (size+sizeof(Chunk)-1) / sizeof(Chunk);
      
      Snapshot cand;
      if(find_candidate(IsEnoughChunk(required_chunk_count), cand) == false) {
        return 0; // out of memory
      }

      Node new_node = {cand.node().next,
                       cand.node().count - required_chunk_count,
                       Node::FREE};
      
      if(cand.compare_and_swap(new_node) == false) {
        return allocate(size);
      }
      
      uint32_t alloced_node_index = cand.index(nodes_) + new_node.count;
      nodes_[alloced_node_index].count = required_chunk_count;

      return alloced_node_index;
    }

    bool release(uint32_t index) {
      if(index == 0 || index >= node_count_) {
        assert(index < node_count_);
        return true;
      }

      uint32_t node_index = index;
      Snapshot pred;
      if(find_candidate(IsPredecessor(node_index), pred) == false) {
        return false;
      }
      if(pred.node().status != Node::FREE) {
        std::cerr << "!! " << pred.node().status << std::endl;
      }
      /*
      if(! (node_index >= pred.index(nodes_)+pred.node().count)) {
        std::cout << "!! " << node_index << ", " << pred.index(nodes_)+pred.node().count << std::endl;
      }
      */
      assert(node_index >= pred.index(nodes_)+pred.node().count);
      assert(pred.node().status == Node::FREE);

      Node* node = &nodes_[node_index];
      Node new_pred_node = {0, 0, Node::FREE};
      if(node_index == pred.index(nodes_) + pred.node().count) { 
        new_pred_node.next  = pred.node().next;
        new_pred_node.count = pred.node().count + node->count;
      } else {
        node->next   = pred.node().next;
        node->status = Node::FREE;
        
        new_pred_node.next  = node_index;
        new_pred_node.count = pred.node().count;
      }
      
      if(pred.compare_and_swap(new_pred_node) == false) {
        return release(index);
      }
      
      return true;
    }

    template<typename T>
    T* ptr(uint32_t index) const { return reinterpret_cast<T*>(chunks_ + index); }

  private:
    class IsEnoughChunk {
    public:
      IsEnoughChunk(uint32_t required_chunk_count) : count_(required_chunk_count) {}
      
      bool operator()(const Snapshot& curr) const {
        //return curr.node().status == Node::FREE && curr.node().count >= count_;
        return curr.node().status == Node::FREE && curr.node().count > count_;
      }

    private:
      const uint32_t count_;
    };

    class IsPredecessor {
    public:
      IsPredecessor(uint32_t node_index) : node_index_(node_index) {}

      bool operator()(const Snapshot& curr) const {
        return node_index_ < curr.node().next;
      }

    private:
      uint32_t node_index_;
    };

    template<class Callback>
    bool find_candidate(const Callback& fn, Snapshot& node, uint32_t retry=0) {
      Snapshot head(&nodes_[0]);
      return find_candidate(fn, head, node, retry);
    }

    template<class Callback>
    bool find_candidate(const Callback& fn, Snapshot& pred, Snapshot& curr, uint32_t retry) {
      if(retry > MAX_RETRY_COUNT) {
        assert(retry <= MAX_RETRY_COUNT);
        return false;
      }
      
      if(pred.node().next == node_count_) {
        return false;
      }
      
      if(go_next_node(pred, curr) == false) {
        usleep(1);
        return find_candidate(fn, curr, retry+1);
      }

      // assert(curr.node().status != Node::JOIN_TAIL);
      if(fn(curr)) {
        return true;
      }
      
      pred = curr;
      return find_candidate(fn, pred, curr, retry);
    }
   
    // TODO: delete
    bool go_next_node(Snapshot& pred, Snapshot& curr) {
      return get_next_snapshot(pred, curr) &&
        update_node_status(pred, curr) &&
        join_nodes_if_need(pred, curr);
    }

    // TODO: 不変項を明記しておく
    bool get_next_snapshot(Snapshot& pred, Snapshot& curr) const {
      if(pred.node().next == node_count_) {
        return false;
      }

      curr.update(&nodes_[pred.node().next]);

      if(pred.isModified()) {
        return false;
      }
      
      // 何故か 32bit環境の'gcc version 4.4.6 20110731 (Red Hat 4.4.6-3) (GCC)'の場合に、
      // 以下のケースに該当する場合があるので、その対処を入れておく (コンパイラのバグ？)
      if(curr.node().status & Node::JOIN_HEAD && curr.isJoinable(nodes_) == false) {
        return false;
      }
      
      if(! (pred.node().status & Node::JOIN_HEAD &&
            curr.node().status & Node::JOIN_TAIL)) {
        // NOTE: predがjoin(JOIN_TAIL)されてしまった場合はここに来るかも
        // (JOIN_TAIL側はJOIN時にアトミックに内容が更新されないので)
        //assert(!(curr.node().status & Node::JOIN_TAIL));
        if(curr.node().status & Node::JOIN_TAIL) {
          return false;
        }
      }
      return true;
    }

    bool update_node_status(Snapshot& pred, Snapshot& curr) {
      if(pred.isJoinable(nodes_) == false) {
        return true;
      }

      Node new_pred_node = {pred.node().next,
                            pred.node().count,
                            pred.node().status | Node::JOIN_HEAD};
      if(pred.compare_and_swap(new_pred_node) == false) {
        return false;
      }
      
      Node new_curr_node = {curr.node().next,
                            curr.node().count,
                            curr.node().status | Node::JOIN_TAIL};
      if(curr.compare_and_swap(new_curr_node) == false) {
        return false;
      }
      
      return true;
    }
    
    bool join_nodes_if_need(Snapshot& pred, Snapshot& curr) {
      if(! (pred.node().status & Node::JOIN_HEAD &&
            curr.node().status & Node::JOIN_TAIL)) {
        assert(!(curr.node().status & Node::JOIN_TAIL));
        return true;
      }
      assert(pred.node().next == curr.index(nodes_));
      assert(pred.isJoinable(nodes_));
     uint32_t curr_status = curr.node().status;
      Node new_pred_node = {curr.node().next,
                            pred.node().count + curr.node().count,
                            (pred.node().status & ~Node::JOIN_HEAD) |
                            (curr.node().status & ~Node::JOIN_TAIL)};
      if(pred.compare_and_swap(new_pred_node) == false) {
        return false;
      }
      if(! (curr_status & Node::JOIN_HEAD)) {
        assert(! (pred.node().status & Node::JOIN_HEAD));
      }
      curr = pred;
      return true;
    }

  private:
    const uint32_t node_count_;
    Node*          nodes_;
    Chunk*         chunks_;
  };
}

#endif
