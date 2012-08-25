#ifndef IMQUE_QUEUE_HH
#define IMQUE_QUEUE_HH

#include "ipc/shared_memory.hh"
#include "queue/queue_impl.hh"
#include <string>
#include <sys/types.h>

namespace imque {
  // ロックフリーなFIFOキュー
  // マルチプロセス間で使用可能
  class Queue {
  public:
    // 親子プロセス間で共有可能なキューを作成する
    // shm_size は共有メモリ領域のサイズ
    Queue(size_t entry_count, size_t shm_size)
      : shm_(shm_size),
        impl_(entry_count, shm_) {
    }
      
    // 複数プロセス間で共有可能なキューを作成する
    // shm_size は共有メモリ領域のサイズ
    // filepath は共有メモリのマッピングに使用するファイルのパス
    Queue(size_t entry_count, size_t shm_size, const std::string& filepath, mode_t mode=0660)
      : shm_(filepath, shm_size, mode),
        impl_(entry_count, shm_) {
    }

    operator bool() const { return shm_ && impl_; }

    // 初期化メソッド。一つの共有キューにつき、一度呼び出す必要がある。
    void init() {
      if(*this) {
        impl_.init();
      }
    }

    // キューに要素を追加する (キューに空きがない場合は false を返す)
    // datav および sizev は count 分のサイズを持ち、それらを全て結合したデータがキューには追加される
    bool enqv(const void** datav, size_t* sizev, size_t count) { return impl_.enqv(datav, sizev, count); }
    
    // キューに要素を追加する (キューに空きがない場合は false を返す)
    bool enq(const void* data, size_t size) { return impl_.enq(data, size); }

    // キューから要素を取り出し buf に格納する (キューが空の場合は false を返す)
    bool deq(std::string& data) { return impl_.deq(data); }

    // キューが空なら true を返す
    bool isEmpty() const { return impl_.isEmpty(); }

    // キューに満杯なら true を返す
    bool isFull()  const { return impl_.isFull(); }
    
    // キューへの要素追加に失敗した回数を返す
    size_t overflowedCount() const { return impl_.overflowedCount(); }

    // 要素追加に失敗した回数カウントを初期化する
    void resetOverflowedCount() { impl_.resetOverflowedCount(); }

  private:
    ipc::SharedMemory shm_;
    queue::QueueImpl  impl_;
  };
}

#endif
