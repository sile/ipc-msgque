/**
 * 無名キューを使用したサンプルコマンド。
 * (親子プロセス間でのキューの共有)
 *
 * 以下の動作を行う:
 *  1] 引数で指定された数だけ(キューを共有する)子プロセスを生成
 *  2] 各子プロセスは、キューにメッセージを追加
 *  3] 親プロセスは、キューからメッセージを取り出し、端末に出力する
 *
 *
 * [使い方]
 * $ anonymous-sample CHILD_PROCESS_COUNT
 */
#include <imque/queue.hh>
#include <unistd.h>    // fork, getpid
#include <sys/types.h>
#include <stdio.h>     // sprintf
#include <string.h>    // strlen
#include <stdlib.h>
#include <iostream>
#include <string>

static const int QUEUE_SHM_SIZE = 10 * 1024; // キューが使用する共有メモリのサイズ

int main(int argc, char** argv) {
  if(argc != 2) {
    std::cerr << "Usage: anonymous-sample CHILD_PROCESS_COUNT" << std::endl;
    return 1;
  }

  const int child_count = atoi(argv[1]);
  
  imque::Queue que(QUEUE_SHM_SIZE);
  if(! que) {
    std::cerr << "queue initialization failed" << std::endl;
    return 1;
  }
  
  for(int i=0; i < child_count; i++) {
    if(fork() == 0) {
      // child process
      char buf[1024]; 
      sprintf(buf, "[%d:%d] Hello", i, getpid());
      que.enq(buf, strlen(buf));
      
      return 0;
    }
  }

  // parent process
  std::string buf;
  for(int i=0; i < child_count;) {
    if(que.deq(buf)) {
      std::cout << "receive# " << buf << std::endl;
      i++;
    } else {
      if(que.overflowedCount() != 0) {
        size_t count = que.resetOverflowedCount();
        std::cout << "queue is full# dropped " << count << " messages" << std::endl;
        i += count;
      }
    }
  }

  return 0;
}
