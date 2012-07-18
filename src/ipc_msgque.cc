#include <ipc_msgque.hh>
#include <string.h>

bool msgque_queue_t::push(uint32_t value, allocator& alc) {
  uint32_t idx = alc.allocate(sizeof(cons_t));
  if(idx == 0) {
    return false;
  }
  
  cons_t* entry = alc.ptr<cons_t>(idx);
  for(;;) {
    uint32_t l_head = head;
    entry->car = value;
    entry->cdr = l_head;
    if(__sync_bool_compare_and_swap(&head, l_head, idx)) {
      return true;
    } 
  }
}

uint32_t msgque_queue_t::pop(allocator& alc) {
  uint32_t l_head = head;
  if(l_head == 0) {
    return false;
  }

  cons_t* next = alc.ptr<cons_t>(l_head);
  if(__sync_bool_compare_and_swap(&head, l_head, next->cdr)) {
    alc.release(l_head);
    return next->car;
  } else {
    return pop(alc);
  }
}

bool msgque_t::push(const void* data, std::size_t size) {
  if(is_full()) {
    return false;
  }

  uint32_t index = alc_.allocate(size);
  if(index == 0) {
    return false;
  }
  
  void* buf = alc_.ptr<void>(index);
  memcpy(buf, data, size);
  
  bool ret = que_->push(index, que_alc_);
  if(ret == false) {
    alc_.release(index);
    return false;
  }
  return true;
}

msgque_data_t msgque_t::pop() {
  msgque_data_t data(NULL, 0);
  return data;
}
