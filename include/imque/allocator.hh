#ifndef IMQUE_ALLOCATOR_HH
#define IMQUE_ALLOCATOR_HH

#include <inttypes.h>
#include <cassert>


namespace imque {
  class Allocator {
    struct Node {  // node of free-list
      uint32_t next;     // next free block index
      uint32_t count:29; // available block count
      uint32_t status:3;

      enum STATUS {
        FREE      = 0,
        USED      = 1,
        JOIN_HEAD = 2,
        JOIN_TAIL = 4
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

    static const uint32_t MAX_RETRY_COUNT = 256;

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

    void* allocate(const uint32_t size) {
      if(size == 0) {
        return NULL; // invalid argument
      }

      const uint32_t required_chunk_count = (size+sizeof(Chunk)-1) / sizeof(Chunk);
      
      Snapshot cand;
      if(find_candidate(IsEnoughChunk(required_chunk_count), cand) == false) {
        return NULL; // out of memory
      }

      Node new_node = {cand.node().next,
                       cand.node().count - required_chunk_count,
                       Node::FREE};
      
      if(cand.compare_and_swap(new_node) == false) {
        return allocate(size);
      }
      
      uint32_t alloced_node_index = cand.index(nodes_) + new_node.count;
      nodes_[alloced_node_index].count = required_chunk_count;

      return reinterpret_cast<void*>(&chunks_[alloced_node_index]);
    }

  private:
    class IsEnoughChunk {
    public:
      IsEnoughChunk(uint32_t required_chunk_count) : count(required_chunk_count) {}
      
      bool operator()(const Snapshot& pred, const Snapshot& curr) const {
        return curr.node().count >= count;
      }

    private:
      const uint32_t count;
    };

    template<class Callback>
    bool find_candidate(const Callback& fn, Snapshot& node, uint32_t retry=0) {
      Snapshot head(&nodes_[0]);
      return find_candidate(fn, head, node, retry);
    }

    template<class Callback>
    bool find_candidate(const Callback& fn, Snapshot& pred, Snapshot& curr, uint32_t retry) {
      if(retry > MAX_RETRY_COUNT) {
        return false;
      }
      
      if(get_next_snapshot(pred, curr) == false ||
         update_node_status(pred, curr) == false ||
         merge_adjacent_nodes_if_need(pred, curr) == false) {
        return find_candidate(fn, curr, retry+1);
      }

      if(fn(pred, curr)) {
        return true;
      }
      
      pred = curr;
      return find_candidate(fn, pred, curr, retry);
    }

    bool get_next_snapshot(Snapshot& pred, Snapshot& curr) const {
      if(pred.node().next == node_count_) {
        return false;
      }

      curr.update(&nodes_[pred.node().next]);
      return pred.isModified() == false;
    }

    bool update_node_status(Snapshot& pred, Snapshot& curr) {
      return true;
    }

    bool merge_adjacent_nodes_if_need(Snapshot& pred, Snapshot& curr) {
      return true;
    }

  private:
    const uint32_t node_count_;
    Node*          nodes_;
    Chunk*         chunks_;
  };
}

#endif
