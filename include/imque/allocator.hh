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

    class NodeSnapshot {
    public:
      NodeSnapshot() : ptr_(NULL) {}
      NodeSnapshot(Node* ptr) : ptr_(ptr), val_(*ptr) {}
      
      void update(Node* ptr) {
        ptr_ = ptr;
        val_ = *ptr;
      }
      
      const Node& node() const { return val_; }
      bool isModified() const { return ptr_->as_uint64() != val_.as_uint64(); }

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
      
      NodeSnapshot node;
      if(find_candidate(node, required_chunk_count) == false) {
        return NULL; // out of memory
      }
      
      return NULL;
    }

  private:
    bool find_candidate(NodeSnapshot& node, uint32_t count, uint32_t retry=0) {
      NodeSnapshot head(&nodes_[0]);
      return find_candidate(head, node, count, retry);
    }

    bool find_candidate(NodeSnapshot& pred, NodeSnapshot& curr, uint32_t count, uint32_t retry=0) {
      if(retry > MAX_RETRY_COUNT) {
        return false;
      }
      
      if(get_next_snapshot(pred, curr) == false ||
         update_node_status(pred, curr) == false ||
         merge_adjacent_nodes_if_need(pred, curr) == false) {
        return find_candidate(curr, count, retry+1);
      }

      // TODO:
      
      pred = curr;
      return find_candidate(pred, curr, count, retry);
    }

    bool get_next_snapshot(NodeSnapshot& pred, NodeSnapshot& curr) const {
      if(pred.node().next == node_count_) {
        return false;
      }

      curr.update(&nodes_[pred.node().next]);
      return pred.isModified();
    }

    bool update_node_status(NodeSnapshot& pred, NodeSnapshot& curr) {
      return true;
    }

    bool merge_adjacent_nodes_if_need(NodeSnapshot& pred, NodeSnapshot& curr) {
      return true;
    }

  private:
    const uint32_t node_count_;
    Node*          nodes_;
    Chunk*         chunks_;
  };
}

#endif
