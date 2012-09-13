/**
 * 名前付きキューを使用したサンプルコマンド。
 * (独立したプロセス間でのキューの共有)
 *
 *
 * [使い方]
 * $ named-sample SHM_FILE_PATH COMMAND ARGS...
 *   - SHM_FILE_PATH: キューが使用する共有メモリ用ファイルのパス
 *   - COMMAND: enq|deq|clear
 *     - enq MESSAGE: キューに要素を追加する
 *     - deq: キューから要素を取り出して、出力する
 *     - clear: キューを空にする
 */
#include <imque/queue.hh>
#include <unistd.h>    // getpid
#include <sys/types.h>
#include <stdio.h>     // sprintf
#include <string.h>    // strlen
#include <sstream>
#include <iostream>
#include <string>

static const int QUEUE_SHM_SIZE = 10 * 1024; // キューが使用する共有メモリのサイズ

int main(int argc, char** argv) {
  if(! (argc == 3 || argc == 4)) {
  usage:
    std::cerr << "Usage: named-sample SHM_FILE_PATH COMMAND(enq|deq|clear) ARGS" << std::endl
              << "  COMMAND and ARGS:" << std::endl
              << "    | enq MESSAGE" << std::endl
              << "    | deq"         << std::endl
              << "    | clear"       << std::endl;
    return 1;
  }

  const char* shm_file_path = argv[1];

  imque::Queue que(QUEUE_SHM_SIZE, shm_file_path);
  if(! que) {
    std::cerr << "queue initialization failed: " << shm_file_path << std::endl;
    return 1;
  }

  const std::string cmd = argv[2];
  if(cmd == "clear" && argc == 3) {
    // clear
    std::cout << "clear: " << shm_file_path << std::endl;
    que.init();

  } else if(cmd == "enq" && argc == 4) {
    // enqueue
    const std::string message = argv[3];
    std::ostringstream out;
    out << "[" << getpid() << "] " << message;
    
    if(que.enq(out.str().data(), out.str().size())) {
      std::cout << "enqueue: " << out.str() << std::endl;
    } else {
      std::cout << "queue is full" << std::endl;
    }
    
  } else if(cmd == "deq" && argc == 3) {
    // dequeue
    std::string buf;
    if(que.deq(buf)) {
      std::cout << "dequeue: " << buf << std::endl;
    }  else {
      std::cout << "queue is empty" << std::endl;
    }
  } else {
    goto usage;
  }

  return 0;
}
