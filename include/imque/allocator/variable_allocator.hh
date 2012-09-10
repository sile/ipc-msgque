#ifndef IMQUE_ALLOCATOR_VARIABLE_ALLOCATOR_HH
#define IMQUE_ALLOCATOR_VARIABLE_ALLOCATOR_HH
#include <iostream> // XXX: for debug
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
          JOIN_TAIL = 2,
          ALLOCATED = 3
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
    
    // ロックフリーな可変長ブロックアロケータ。
    // 一つのインスタンスで(実際に)割当可能なメモリ領域の最大長は sizeof(Chunk)*NODE_COUNT_LIMIT = 512MB
    class VariableAllocator {
      typedef VariableAllocatorAux::Node Node;
      typedef atomic::Snapshot<Node> NodeSnapshot;
      typedef VariableAllocatorAux::Chunk Chunk;
      
      static const int RETRY_LIMIT = 32;
      static const int FAST_RETRY_LIMIT = 1;
      static const uint32_t NODE_COUNT_LIMIT = 0x1000000; // 24bit

    public:
      // region: 割当に使用するメモリ領域。
      // size: regionのサイズ。メモリ領域の内の sizeof(Node)/sizeof(Chunk) は管理用に利用される。
      VariableAllocator(void* region, uint32_t size)
        : node_count_(size/(sizeof(Node)+sizeof(Chunk))),
          nodes_(reinterpret_cast<Node*>(region)),
          chunks_(reinterpret_cast<Chunk*>(nodes_+node_count_)) {
      }
      
      operator bool() const { return nodes_ != NULL && node_count_ > 2 && node_count_ < NODE_COUNT_LIMIT; }

      // 初期化メソッド。
      // コンストラクタに渡した region につき一回呼び出す必要がある。
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
      
      // メモリ割当を行う。
      // 要求したサイズの割当に失敗した場合は 0 を、それ以外はメモリ領域参照用の識別子(記述子)を返す。
      // (識別子を ptrメソッド に渡すことで、実際のメモリ領域を参照可能)
      //
      // メモリ割当は、領域不足以外に、極めて高い競合下で楽観的ロックの試行回数(RETRY_LIMIT)を越えた場合にも失敗する。
      //
      // このメソッドが返す識別子の値は NODE_COUNT_LIMIT 未満の値(= 24bitに収まる値)であることが保証されている。
      // ※ つまり、呼び出し側は、上位8bitが0bitであることを前提にしたコードを書くことができる
      uint32_t allocate(uint32_t size) {
        if(size == 0) {
          return 0; // invalid argument
        }

        uint32_t need_chunk_count = (size+sizeof(Chunk)-1) / sizeof(Chunk);
      
        NodeSnapshot cand;
        if(findCandidate(IsEnoughChunk(need_chunk_count), cand) == false) {
          return 0; // out of memory (or exceeded retry limit)
        }

        uint32_t new_count = cand.node().count - need_chunk_count;
        if(cand.compare_and_swap(cand.node().changeCount(new_count)) == false) {
          return allocate(size);
        }
      
        uint32_t allocated_node_index = index(cand) + new_count; // memory descriptor
        nodes_[allocated_node_index] = cand.node().changeCount(need_chunk_count);
        nodes_[allocated_node_index].next = 1; // XXX: 参照カウント実験
        nodes_[allocated_node_index].status = Node::ALLOCATED;
        return allocated_node_index; 
      }

      bool refincr(uint32_t md) {
        for(;;) {
          NodeSnapshot snap(nodes_ + md);
          if(snap.node().status != Node::ALLOCATED) { // XXX: これだけでは一周してしまっているケースを検出できない
            return false;
          }

          if(snap.node().next == 0) {
            return false;
          }
        
          Node n = snap.node().changeNext(snap.node().next+1);
          if(snap.compare_and_swap(n)) {
            return true;
          }
        }
      }

      bool refdecr(uint32_t md) {
        for(;;) {
          NodeSnapshot snap(nodes_ + md);
          assert(snap.node().status == Node::ALLOCATED);
        
          Node n = snap.node().changeNext(snap.node().next-1);
          if(snap.compare_and_swap(n)) {
            return n.next == 0;
          }
        }
      }

      // allocateメソッドで割り当てたメモリ領域を解放する。(解放に成功した場合は trueを、失敗した場合は false を返す)
      // md(メモリ記述子)が 0 の場合は何も行わない。
      //
      // メモリ解放は、極めて高い競合下で楽観的ロックの試行回数(RETRY_LIMIT)を越えた場合に失敗することがある。
      bool release(uint32_t md) {
        std::cout << "@ in release" << std::endl;
        if(refdecr(md) == false) {
          return true;
        }
        std::cout << "@ real release" << std::endl;
        return releaseImpl(md, RETRY_LIMIT, false);
      }
      
      // 楽観的ロック失敗時の試行回数が少ない以外は releaseメソッド と同様。
      bool fastRelease(uint32_t md) {
        if(refdecr(md) == false) {
          return true;
        }
        return releaseImpl(md, FAST_RETRY_LIMIT, true);
      }

      // allocateメソッドが返したメモリ記述子から、対応する実際にメモリ領域を取得する
      template<typename T>
      T* ptr(uint32_t md) const { return reinterpret_cast<T*>(chunks_ + md); }

      template<typename T>
      T* ptr(uint32_t md, uint32_t offset) const { return reinterpret_cast<T*>(ptr<char>(md)+offset); }
      
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

        const uint32_t node_index_;
      };
      
      template<class Callback>
      bool findCandidate(const Callback& fn, NodeSnapshot& node, int retry=RETRY_LIMIT) {
        NodeSnapshot head(&nodes_[0]);
        return findCandidate(fn, head, node, retry);
      }

      template<class Callback>
      bool findCandidate(const Callback& fn, NodeSnapshot& pred, NodeSnapshot& curr, int retry) {
        if(retry < 0) {
          return false;
        }
        
        if(pred.node().next == node_count_) { // 終端ノードに達した
          return false;
        }
        
        if(getNextSnapshot(pred, curr) == false ||  
           updateNodeStatus(pred, curr) == false ||
           joinNodesIfNeed(pred, curr) == false) { 
          return findCandidate(fn, curr, retry-1);
        }

        if(fn(curr)) {
          return true;
        }

        pred = curr;
        return findCandidate(fn, pred, curr, retry);
      }

      bool getNextSnapshot(NodeSnapshot& pred, NodeSnapshot& curr) const {
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

      bool updateNodeStatus(NodeSnapshot& pred, NodeSnapshot& curr) {
        if(isJoinable(pred) == false) {
          return true;
        }

        // 二つのノードが結合可能(領域が隣接している)なら、ステータスを更新する
        return (pred.compare_and_swap(pred.node().changeStatus(pred.node().status | Node::JOIN_HEAD)) &&
                curr.compare_and_swap(curr.node().changeStatus(curr.node().status | Node::JOIN_TAIL)));
      }
    
      bool joinNodesIfNeed(NodeSnapshot& pred, NodeSnapshot& curr) {
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

      bool releaseImpl(uint32_t md, int retry_limit, bool fast) {
        if(md == 0 || md >= node_count_) {
          assert(md < node_count_);
          return true;
        }

        uint32_t node_index = md;
        NodeSnapshot pred;
        if(findCandidate(IsPredecessor(node_index), pred, retry_limit) == false) {
          return false;
        }
        // 極めて高い競合下では、以下のassertionがfalseになる場合はある。
        // 原因はおそらくABA問題で Node.version に割り当てるビット量を増やせば発生頻度は低下する。
        // ※ ただしFixedAllocatorと併用する場合は、ほぼ間違いなくといって良いほど、この問題は起こらないので、
        //    現状の割り当てビット数で問題ない。
        assert(node_index >= index(pred)+pred.node().count);

        Node* node = &nodes_[node_index];
        Node new_pred_node;
        bool is_neighbor = node_index == index(pred)+pred.node().count;
        if(is_neighbor) {
          // 隣接している場合は、解放の時点で結合してしまう
          new_pred_node = pred.node().changeCount(pred.node().count + node->count);
        } else {
          new_pred_node = pred.node().changeNext(node_index);
          node->next = pred.node().next;
          node->status = Node::AVAILABLE;
        }
      
        if(pred.compare_and_swap(new_pred_node) == false) {
          return fast ? false : releaseImpl(md, retry_limit, fast);
        }
      
        return true;
      }

    private:
      const uint32_t node_count_;
      Node* nodes_;
      Chunk* chunks_;      
    };
  }
}

#endif
