#ifndef IMQUE_ATOMIC_ATOMIC
#define IMQUE_ATOMIC_ATOMIC

namespace imque {
  namespace atomic {
    template<typename T>
    bool compare_and_swap(T* place, T old_value, T new_value) {
      return __sync_bool_compare_and_swap(place, old_value, new_value);
    }

    template<typename T>
    T add_and_fetch(T* place, T delta) {
      return __sync_add_and_fetch(place, delta);
    }

    template<typename T>
    T fetch(T* place) {
      return add_and_fetch(place, 0);
    }
  }
}

#endif
