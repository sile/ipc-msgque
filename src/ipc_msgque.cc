#include <ipc_msgque.hh>
#include <string.h>

#include <iostream>

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
    return 0;
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
  uint32_t index = alc_.allocate(size + sizeof(std::size_t));
  if(index == 0) {
    return false;
  }

  void* buf = alc_.ptr<void>(index);
  ((std::size_t*)buf)[0] = size;
  memcpy((char*)buf+sizeof(std::size_t), data, size);
  
  bool ret = que_->push(index, que_alc_);
  if(ret == false) {
    alc_.release(index);
    return false;
  }
  return true;
}

msgque_data_t msgque_t::pop() {
  uint32_t idx = que_->pop(que_alc_);
  msgque_data_t data(alc_, idx);
  return data;
}
