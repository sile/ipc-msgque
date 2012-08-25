## 概要
* FIFOキュー
* ロックフリー
* マルチプロセス間の通信(IPC)に使用可能
* C++ヘッダライブラリ

## バージョン
* 0.0.3

## 対応環境
* gccのver4.1以上
* POSIX準拠のOS

## ライセンス
* MITライセンス
* 詳細はCOPYINGファイルを参照

## 使用方法
* "include/imque/queue.hh" をインクルードする

## API

```c++
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

    // 要素数を取得する
    size_t entryCount() const { return impl_.entryCount(); }
    
    // キューへの要素追加に失敗した回数を返す
    size_t overflowedCount() const { return impl_.overflowedCount(); }

    // 要素追加に失敗した回数カウントを初期化する
    void resetOverflowedCount() { impl_.resetOverflowedCount(); }

  private:
    ipc::SharedMemory shm_;
    queue::QueueImpl  impl_;
  };
}
```

## 使用例(1)# 親子プロセスでキューを共有する場合
```C++
#include <imque/queue.hh>
#include <unistd.h>    // fork, getpid
#include <sys/types.h>
#include <stdio.h>     // sprintf
#include <string.h>    // strlen
#include <iostream>
#include <string>

int main(int argc, char** argv) {
  imque::Queue que(32, 4096);
  if(! que) {
    return 1;
  }

  que.init(); 
  
  for(int i=0; i < 10; i++) {
    if(fork() == 0) {
      // child process
      char buf[1024]; 
      sprintf(buf, "Hello: %d", getpid());
      que.enq(buf, strlen(buf));
      return 0;
    }
  }

  // parent process
  for(int i=0; i < 10; i++) {
    std::string buf;
    while(que.deq(buf) == false);
    std::cout << "receive# " << buf << std::endl;
  }

  return 0;
}
```

## 使用例(2)# 独立したプロセス間でキューを共有する場合
```C++
/**
 * filename: msgque.cc
 */
#include "include/imque/queue.hh"
#include <unistd.h>    // getpid
#include <sys/types.h>
#include <stdio.h>     // sprintf
#include <string.h>    // strlen
#include <iostream>
#include <string>

#define SHM_FILE_PATH "/tmp/msgque.shm"
#define SHM_SIZE 4096
#define QUEUE_ENTRY_COUNT 8

int main(int argc, char** argv) {
  if(argc != 2) {
  usage:
    std::cerr << "Usage: msgque init|enq|deq" << std::endl;
    return 1;
  }

  imque::Queue que(QUEUE_ENTRY_COUNT, SHM_SIZE, SHM_FILE_PATH);
  if(! que) {
    std::cerr << "queue initialization failed" << std::endl;
    return 1;
  }

  std::string cmd=argv[1];
  if(cmd == "init") {
    std::cout << "# init: " << SHM_FILE_PATH << std::endl;
    que.init();
  } else if(cmd == "enq") {
    char buf[1024];
    sprintf(buf, "Hello: %d", getpid());
    
    if(que.enq(buf, strlen(buf))) {
      std::cout << "# enqueue: " << buf << std::endl;
    } else {
      std::cout << "# queue is full" << std::endl;
    }
  } else if(cmd == "deq") {
    std::string buf;
    if(que.deq(buf)) {
      std::cout << "# dequeue: " << buf << std::endl;
    }  else {
      std::cout << "# queue is empty" << std::endl;
    }
  } else {
    goto usage;
  }
  
  std::cout << "# entry count: " << que.entryCount() << std::endl;

  return 0;
}
```
