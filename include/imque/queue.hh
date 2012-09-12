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
    // 親子プロセス間で共有可能な無名キューを作成する
    // shm_size は共有メモリ領域のサイズ (最大約256MB)
    Queue(size_t shm_size)
      : shm_(shm_size),
        impl_(shm_) {
      init();
    }
      
    // 複数プロセス間で共有可能な名前付きキューを作成する
    // shm_size は共有メモリ領域のサイズ (最大約256MB)
    // filepath は共有メモリのマッピングに使用するファイルのパス
    Queue(size_t shm_size, const std::string& filepath, mode_t mode=0660)
      : shm_(filepath, shm_size, mode),
        impl_(shm_) {
      if(*this) {
        impl_.init_once();
      }
    }

    operator bool() const { return shm_ && impl_; }

    // 初期化メソッド。
    // キューを空に戻したい場合や、名前付きキュー用のファイルを使い回して明示的に初期化したい場合などに使用する。
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
    bool isEmpty() { return impl_.isEmpty(); }
    
    // キューへの要素追加に失敗した回数を返す
    size_t overflowedCount() const { return impl_.overflowedCount(); }

    // キューへの要素追加失敗回数の取得と、カウントの初期化をアトミックに行う。
    size_t resetOverflowedCount() { return impl_.resetOverflowedCount(); }

  private:
    ipc::SharedMemory shm_;
    queue::QueueImpl  impl_;
  };
}

#endif
