#ifndef IMQUE_ATOMIC_ATOMIC
#define IMQUE_ATOMIC_ATOMIC

#include <inttypes.h>

namespace imque {
  namespace atomic {
    namespace {
      template<typename From, typename To>
      To conv(From v) {
        union {
          From from;
          To to;
        } u;
        u.from = v;
        return u.to;
      }

      template<int SIZE>
      struct SizeToType { typedef uint8_t TYPE; }; // XXX:
      
      template<> struct SizeToType<1> { typedef uint8_t TYPE; };
      template<> struct SizeToType<2> { typedef uint16_t TYPE; };
      template<> struct SizeToType<4> { typedef uint32_t TYPE; };
      template<> struct SizeToType<8> { typedef uint64_t TYPE; };
    }

    template<typename T>
    bool compare_and_swap(T* place, T old_value, T new_value) {
      typedef typename SizeToType<sizeof(T)>::TYPE uint;
      return __sync_bool_compare_and_swap(conv<T*, uint*>(place), 
                                          conv<T, uint>(old_value), 
                                          conv<T, uint>(new_value));
    }

    template<typename T>
    T add_and_fetch(T* place, int delta) {
      typedef typename SizeToType<sizeof(T)>::TYPE uint;
      return __sync_add_and_fetch(conv<T*, uint*>(place), delta);
    }

    template<typename T>
    T fetch_and_add(T* place, int delta) {
      typedef typename SizeToType<sizeof(T)>::TYPE uint;
      return __sync_fetch_and_add(conv<T*, uint*>(place), delta);
    }

    template<typename T>
    T fetch(T* place) {
      return add_and_fetch(place, 0);
    }
  }
}

#endif
