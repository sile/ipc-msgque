#ifndef IPC_ALLOCATOR_HH
#define IPC_ALLOCATOR_HH

#include <inttypes.h>
#include <string.h>

struct alloc_entry {
  uint32_t next;
  uint32_t size;
};

struct entry_header {
  uint32_t size;
  // uint32_t isize; // inversed size
};

class allocator {
public:
  allocator(void* ptr, uint32_t size) : ptr_(ptr), entries_((alloc_entry*)ptr), size_(size) {
    memset(ptr_, 0, size_);
    entries_[0].next = 1;
    entries_[1].size = size_-sizeof(alloc_entry);
  }
  
  uint32_t allocate(uint32_t size) {
    if(size == 0) { 
      return 0;
    }
    
    alloc_entry* head = &entries_[0];
    alloc_entry* prev = find_candidate_prev(head, size);
    if(prev == NULL) {
      return 0;
    }

    union {
      alloc_entry new_prev;
      uint64_t ll;
    } a;
    uint32_t block_count = ((size+sizeof(entry_header)) / sizeof(alloc_entry)) + 1; // XXX: 適当
    a.new_prev.next = prev->next + block_count;
    a.new_prev.size = prev->size - block_count * sizeof(alloc_entry);
    alloc_entry cur = *prev;

    if(__sync_bool_compare_and_swap((uint64_t*)prev, *(uint64_t*)prev, a.ll)) {
      entry_header* h = (entry_header*)&entries_[cur.next];
      h->size = size;
      return cur.next;
    } else {
      return allocate(size);
    }
  }
  
  void release(uint32_t index) {
  }

  void* get_ptr(uint32_t index) {
    return (void*)&entries_[index];
  }

private:
  alloc_entry* find_candidate_prev(alloc_entry* prev, uint32_t size) {
    if(prev->next*sizeof(alloc_entry) + size >= size_) {
      return NULL;
    }
    
    if(prev->size >= sizeof(entry_header) + size) {
      return prev;
    }
    return find_candidate_prev(&entries_[prev->next], size);
  }

private:
  void* ptr_;
  alloc_entry* entries_;
  uint32_t size_;
};

#endif
