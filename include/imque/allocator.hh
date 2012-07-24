#ifndef IMQUE_ALLOCATOR_HH
#define IMQUE_ALLOCATOR_HH

#include <inttypes.h>
#include <cassert>

namespace imque {
  /**
   * 複数プロセス・複数スレッドで共有されているメモリ領域の割り当てに使えるアロケータ
   */
  class Allocator {
    /** 
     * 補助構造体・クラス群
     */
    // 空き領域を管理するリンクリストのノード
    struct Node {
      uint32_t next;     // 次のノードのインデックス
      uint32_t count:30; // このノードが管理する空きチャンク数: sizeof(Chunk)*count = 割り当て可能なバイト数
      uint32_t status:2; // ノードのステータス

      enum STATUS {
        FREE      = 0, // 割り当て可能
        JOIN_HEAD = 1, // 後ろのノードとの結合待ち
        JOIN_TAIL = 2  // 前のノードとの結合待ち
      };

      uint64_t* uint64_ptr() { return reinterpret_cast<uint64_t*>(this); }
      uint64_t uint64() const { return *reinterpret_cast<const uint64_t*>(this); }
    } __attribute__((__packed__));

    // 実際に割り当てられるチャンク領域
    // インデックスが1のノードは、インデックスが1のチャンクを管理している
    struct Chunk {
      char padding[32];
    };
    
    // ノードの特定時点でのスナップショットを保持するクラス
    class Snapshot {
    public:
      Snapshot() : ptr_(NULL) {}
      Snapshot(Node* ptr) { update(ptr); }
      
      void update(Node* ptr) {
        ptr_ = ptr;

        uint64_t val = __sync_add_and_fetch(ptr->uint64_ptr(), 0); // atomicなロード関数がないので、その代替
        val_ = *reinterpret_cast<Node*>(&val);
      }

      const Node& node() const { return val_; }
      bool isModified() const { return ptr_->uint64() != val_.uint64(); }
      bool isJoinable(Node* head) const { return val_.next == index(head) + val_.count; }
      
      bool compare_and_swap(const Node& new_val) {
        if(isModified() == false &&
           __sync_bool_compare_and_swap(ptr_->uint64_ptr(), val_.uint64(), new_val.uint64())) {
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
    
  private:
    static const uint32_t MAX_RETRY_COUNT = 512; 

  public:
    // コンストラクタ
    // 
    // region: 割り当て可能なメモリ領域
    // size: 割り当て可能なメモリ領域のバイト数
    //
    // [スペースに関する注意事項]
    // - 渡されたメモリ領域の内の 1/5 は空き領域の管理用に使用される。
    // - また、一つの割り当てごとに平均して 16byte のオーバーヘッドがある。
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

    // メモリ割り当てを行う。
    // 失敗した場合は 0 を、成功した場合は割り当てたメモリ領域参照用のID値を返す。
    // ※ ID値を ptr() メソッドに渡すことで、実際に利用可能なメモリ領域を取得できる
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

    // sizeバイトを利用(割当)可能にするためには、何バイト分の領域をコンストラクタに渡せば良いかを返す
    // ※ あくまでも目安である程度の誤差はある
    static size_t calc_need_byte_size(size_t size) {
      return (sizeof(Node)+sizeof(Chunk))*size / sizeof(Chunk);
    }

    // テスト用メソッド: 他と競合が発生するような状況で実行したら、結果が不正になることがあるので注意
    uint32_t allocatedNodeCount() const {
      uint32_t allocated_count = 0;
      
      Snapshot pred(&nodes_[0]);
      Snapshot curr;
      while(get_next_snapshot(pred, curr)) {
        allocated_count += (curr.node().next - curr.index(nodes_)) - curr.node().count;
        pred = curr;
      }
      allocated_count += (node_count_ - curr.node().next);

      return allocated_count;
    }
    
  private:
    // 現在のノードが、十分な(要求以上の)空き領域を管理しているかどうかを判定するためのコールバック
    class IsEnoughChunk {
    public:
      IsEnoughChunk(uint32_t required_chunk_count) : count_(required_chunk_count) {}
      
      bool operator()(const Snapshot& curr) const {
        // NOTE: '>='ではなく'>'で比較していることに注意
        //       ※ ノードの先頭部分も割り当ててしまうとリンクリストが壊れるので、そこは除外している
        return curr.node().status == Node::FREE && curr.node().count > count_;
      }

    private:
      const uint32_t count_;
    };

    // 現在のノードが、指定されたインデックス(のノード)の直前となるものかを判定するためのコールバック
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
      
      if(pred.node().next == node_count_) { // 終端ノードに達した
        return false;
      }
      
      if(get_next_snapshot(pred, curr) == false ||  
         update_node_status(pred, curr) == false ||
         join_nodes_if_need(pred, curr) == false) { 
        usleep(retry+1);
        return find_candidate(fn, curr, retry+1);
      }

      if(fn(curr)) {
        return true;
      }
      
      pred = curr;
      return find_candidate(fn, pred, curr, retry);
    }

    // predの次のノード(のスナップショット)を取得して curr に設定する
    bool get_next_snapshot(Snapshot& pred, Snapshot& curr) const {
      if(pred.node().next == node_count_) {
        return false;
      }

      curr.update(&nodes_[pred.node().next]);

      if(pred.isModified()) {
        return false;
      }

      assert(! (curr.node().status & Node::JOIN_HEAD && curr.isJoinable(nodes_)==false));
      
      if((! pred.node().status & Node::JOIN_HEAD) && curr.node().status & Node::JOIN_TAIL) {
        // 前半のノードが結合可能状態になっていないのに、後半のノードだけ結合可能にマークされている
        // => pred がその一つ前のノードと結合されてしまった場合は、ここに来る可能性あり
        //    1) predを取得 => predにはNode::JOIN_TAILマークのみが付いている
        //    2) predとその前のノード(predpred)が結合 => この時点でpredの内容は実質無効になる (ただし値は変わらない)
        //    3) predpredとcurrは結合可能 => currにNode::JOIN_TAILマークが付く
        //    4) predからcurrを取得 => predは無効になっているけど、値は以前から変わっていないのでそれを検出できない
        //       => この時点で、上のif文を満たす条件が成立する
        return false;
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
