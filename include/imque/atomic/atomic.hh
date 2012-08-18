#ifndef IMQUE_ATOMIC_ATOMIC
#define IMQUE_ATOMIC_ATOMIC

#include <inttypes.h>

namespace imque {
  namespace atomic {
    namespace {
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
      
      template<typename From, typename To>
      To* union_conv(From* v) {
        return reinterpret_cast<To*>(v);
      }

      template<typename From, typename To>
      volatile To* union_conv(volatile From* v) {
        return reinterpret_cast<volatile To*>(v);
      }

      template<typename T>
      T union_conv(T v) {
        return v;
      }

      template<int SIZE>
      struct SizeToType { typedef uint8_t TYPE; }; // XXX:
      
      template<> struct SizeToType<1> { typedef uint8_t TYPE; };
      template<> struct SizeToType<2> { typedef uint16_t TYPE; };
      template<> struct SizeToType<4> { typedef uint32_t TYPE; };
      template<> struct SizeToType<8> { typedef uint64_t TYPE; };
    }

    template<typename T, typename T2>
    bool compare_and_swap(T* place, T2 old_value, T2 new_value) {
      typedef typename SizeToType<sizeof(T)>::TYPE uint;
      return __sync_bool_compare_and_swap(union_conv<T, uint>(place), 
                                          union_conv<T2, uint>(old_value), 
                                          union_conv<T2, uint>(new_value));
    }

    template<typename T>
    T add_and_fetch(T* place, int delta) {
      typedef typename SizeToType<sizeof(T)>::TYPE uint;
      return union_conv<uint, T>(__sync_add_and_fetch(union_conv<T, uint>(place), delta));
    }

    template<typename T>
    T fetch_and_add(T* place, int delta) {
      typedef typename SizeToType<sizeof(T)>::TYPE uint;
      return union_conv<uint, T>(__sync_fetch_and_add(union_conv<T, uint>(place), delta));
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
      return add_and_fetch(place, 0);
    }

    template<typename T>
    class Snapshot {
    public:
      Snapshot() : ptr_(NULL) {}
      Snapshot(T* ptr) { update(ptr); }
      
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
