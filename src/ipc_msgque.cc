#include <ipc_msgque.hh>
#include <string.h>

#include <iostream>

bool msgque_queue_t::push(uint32_t value) {
  uint32_t l_read = next_read;
  uint32_t l_write = next_write;

  if(l_read == (next_write+1)%size) {
    return false;
  }

  que_ent_t *pent = &entries[l_write];
  que_ent_t ent = *pent;
  if(ent.flag == 1) {
    __sync_bool_compare_and_swap(&next_write, l_write, (l_write+1)%size);
    return push(value);
  }
  
  que_ent_t new_ent;
  new_ent.flag = 1;
  new_ent.value = value;
  if(__sync_bool_compare_and_swap((uint32_t*)pent, ent.uint32(), new_ent.uint32())) {
    __sync_bool_compare_and_swap(&next_write, l_write, (l_write+1)%size);
    return true;
  } else {
    return push(value);
  }
}

uint32_t msgque_queue_t::pop() {
  uint32_t l_read = next_read;
  uint32_t l_write = next_write;
  
  if(l_read == l_write) {
    return false;
  }
  
  que_ent_t *pent = &entries[l_read];
  que_ent_t ent = *pent;
  if(ent.flag == 0) {
    __sync_bool_compare_and_swap(&next_read, l_read, (l_read+1)%size);
    // 別のプロセスが取得済
    return pop();
  }
  
  que_ent_t new_ent;
  new_ent.flag = 0;
  if(__sync_bool_compare_and_swap((uint32_t*)pent, ent.uint32(), new_ent.uint32())) {
    __sync_bool_compare_and_swap(&next_read, l_read, (l_read+1)%size);
    return ent.value;
  } else {
    return pop();
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
  
  bool ret = que_->push(index);
  if(ret == false) {
    alc_.release(index);
    return false;
  }
  return true;
}

bool msgque_t::pop(std::string& buf) {
  uint32_t idx = que_->pop();
  if(idx == 0) {
    return false;
  }
  
  std::size_t size = alc_.ptr<std::size_t>(idx)[0];
  buf.append(&alc_.ptr<char>(idx)[sizeof(std::size_t)], size);
  alc_.release(idx);
  return true;
}
