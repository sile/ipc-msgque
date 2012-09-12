#ifndef IMQUE_ATOMIC_ATOMIC
#define IMQUE_ATOMIC_ATOMIC

#include <string.h>
#include <inttypes.h>

namespace imque {
  namespace atomic {
    namespace {
      // From から To へと型変換を行う
      template<typename From, typename To>
      To union_conv(From v) {
        union {
          From from;
          To to;
        } u;
        u.to = To();
        u.from = v;
        return u.to;
      }
      
      // ポインタ用
      template<typename From, typename To>
      To* union_conv(From* v) { return reinterpret_cast<To*>(v); }

      // volatileポインタ用
      template<typename From, typename To>
      volatile To* union_conv(volatile From* v) { return reinterpret_cast<volatile To*>(v); }

      // From == To 用
      template<typename T>
      T union_conv(T v) { return v; }
      
      // 型のサイズに対応する uintXXX_t 型にマッピングする
      template<int SIZE> struct SizeToType {};
      template<> struct SizeToType<1> { typedef uint8_t TYPE; };
      template<> struct SizeToType<2> { typedef uint16_t TYPE; };
      template<> struct SizeToType<4> { typedef uint32_t TYPE; };
      template<> struct SizeToType<8> { typedef uint64_t TYPE; };
    }

    // TODO: 場所移動
    template<typename From, typename To>
    To cast(From v) { return union_conv<From,To>(v); }
    
    // 各種アトミック命令
    template<typename T, typename T2>
    bool compare_and_swap(T* place, T2 old_value, T2 new_value) {
      typedef typename SizeToType<sizeof(T)>::TYPE uint;
      return __sync_bool_compare_and_swap(union_conv<T, uint>(place), 
                                          union_conv<T2, uint>(old_value), 
                                          union_conv<T2, uint>(new_value));
    }
    
    template<typename T>
    T fetch_and_add(T* place, int delta) {
      typedef typename SizeToType<sizeof(T)>::TYPE uint;
      return union_conv<uint, T>(__sync_fetch_and_add(union_conv<T, uint>(place), delta));
    }

    template<typename T>
    T fetch_and_clear(T* place) {
      typedef typename SizeToType<sizeof(T)>::TYPE uint;
      return union_conv<uint, T>(__sync_fetch_and_and(union_conv<T, uint>(place), 0));
    }

    template<typename T>
    void add(T* place, int delta) {
      typedef typename SizeToType<sizeof(T)>::TYPE uint;
      __sync_add_and_fetch(union_conv<T, uint>(place), delta);
    }
    
    template<typename T>
    void sub(T* place, int delta) {
      typedef typename SizeToType<sizeof(T)>::TYPE uint;
      __sync_sub_and_fetch(union_conv<T, uint>(place), delta);
    }

    template<typename T>
    T fetch(T* place) {
      return fetch_and_add(place, 0);
    }

    // スナップショットクラス
    template<typename T>
    class Snapshot {
    public:
      Snapshot() : ptr_(NULL) {}
      Snapshot(T* ptr) : ptr_(ptr), val_(atomic::fetch(ptr)) {}
      
      void update(T* ptr) {
        ptr_ = ptr;
        val_ = atomic::fetch(ptr);
      }
      
      const T& node() const { return val_; }
      const T* place() const { return ptr_; }

      bool isModified() const { 
        T tmp_val = atomic::fetch(ptr_);
        return memcmp(&tmp_val, &val_, sizeof(T)) != 0;
      }
      
      bool compare_and_swap(const T& new_val) {
        if(atomic::compare_and_swap(ptr_, val_, new_val)) {
          val_ = new_val;
          return true;
        }
        return false;
      }

      protected:
        T* ptr_;
        T  val_;
    };
  }
}

#endif
