#ifndef IMQUE_STAT_HH
#define IMQUE_STAT_HH

namespace imque {
  // 統計値のカウント/計算用の補助クラス
  class Stat {
  public:
    Stat() : count_(0), total_(0) {
    }

    void add(int val) {
      count_++;
      total_ += val;
    }
    
    int count() const { return count_; }
    int avg() const { return count_ == 0 ? 0 : static_cast<int>(total_ / count_); };
    
  private:
    int count_;
    long long total_;
  };
}

#endif
