#ifndef IMQUE_NANO_TIMER_HH
#define IMQUE_NANO_TIMER_HH

#include <sys/time.h>

namespace imque {
  // ナノ秒単位の時間計測用のタイマー
  // ※ 現在は可搬性のために gettimeofday(μ秒単位の時刻取得関数) を使用している
  class NanoTimer {
  public:
    NanoTimer() {
      gettimeofday(&t, NULL);
    }
    
    long elapsed() const {
      timeval now;
      gettimeofday(&now, NULL);
      return ns(now) - ns(t);
    }
    
  private:
    long ns(const timeval& ts) const {
      return static_cast<long>(static_cast<long long>(ts.tv_sec)*1000*1000*1000 + ts.tv_usec*1000);
    }
    
  private:
    timeval t;
  };
}

#endif
