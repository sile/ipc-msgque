#ifndef IPC_ALLOCATOR_HH
#define IPC_ALLOCATOR_HH

#include <inttypes.h>
#include <string.h>

struct alloc_entry {
  uint32_t next;
  uint32_t size;  // XXX: 二つはいらないかも
};

struct entry_header {
  alloc_entry ae;
  uint32_t size;
  // uint32_t isize; // inversed size
};

class allocator {
public:
  allocator(void* ptr, uint32_t size) : ptr_(ptr), entries_((alloc_entry*)ptr), size_(size) {
    memset(ptr_, 0, size_);
    entries_[0].next = 1;
    entries_[0].size = size_-sizeof(alloc_entry);
  }
  
  uint32_t allocate(uint32_t size) {
    if(size == 0) { 
      return 0;
    }
    
    alloc_entry* head = &entries_[0];
    alloc_entry* prev = find_candidate_prev(*head, head, size);
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
      h->size = block_count * sizeof(alloc_entry);
      return cur.next;
    } else {
      return allocate(size);
    }
  }
  
  void release(uint32_t index) {
    alloc_entry* prev = &entries_[0];
    while(prev->next < index) {
      prev = &entries_[prev->next];
    }

    entry_header* h = (entry_header*)&entries_[index];
    h->ae = *prev;
    
    union {
      alloc_entry new_prev;
      uint64_t ll;
    } a;

    a.new_prev.next = index;
    a.new_prev.size = h->size;

    if(! __sync_bool_compare_and_swap((uint64_t*)prev, *(uint64_t*)prev, a.ll)) {
      release(index);
    }
  }

  void* get_ptr(uint32_t index) {
    return (void*)((char*)&entries_[index] + sizeof(entry_header));
  }

  void dump() {
    
  }

private:
  alloc_entry* find_candidate_prev(alloc_entry e, alloc_entry* prev, uint32_t size) {
    if(e.next*sizeof(alloc_entry) + size >= size_) {
      return NULL;
    }
    
    if(e.size >= sizeof(entry_header) + size) {
      return prev;
    }
    
    alloc_entry next = entries_[e.next];
    alloc_entry* pnext = &entries_[e.next];
    if(memcmp(&e, prev, sizeof(e)) == 0) {
      
      //
      /*
      if(e.next + e.size / sizeof(entry_header) == next.next) {
        union {
          alloc_entry new_next;
          uint64_t ll;
        } a;
        a.new_next.next = next;
        a.new_next
      }
      */

      return find_candidate_prev(next, pnext, size);
    } else {
      alloc_entry* head = &entries_[0];
      return find_candidate_prev(*head, head, size);
    }
  }

private:
  void* ptr_;
  alloc_entry* entries_;
  uint32_t size_;
};

#endif
