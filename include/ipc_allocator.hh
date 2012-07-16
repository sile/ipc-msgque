#ifndef IPC_ALLOCATOR_HH
#define IPC_ALLOCATOR_HH

#include <inttypes.h>
#include <string.h>

#include <assert.h>

struct alloc_entry {
  uint32_t next;
  uint32_t size; 
};

struct entry_header {
  alloc_entry ae;
  uint32_t size;
  // uint32_t isize; // inversed size
  // TODO: timestamp
};

class allocator {
public:
  allocator(void* ptr, uint32_t size) : ptr_(ptr), entries_((alloc_entry*)ptr), size_(size) {
    // TODO: 初期化は別メソッドに分ける (多重初期化防止も含めて)
    entries_[0].next = 1;
    entries_[0].size = 0;

    entries_[1].next = 0;
    entries_[1].size = size_-sizeof(alloc_entry);
  }
  
  uint32_t allocate(uint32_t size) {
    if(size == 0) { 
      return 0;
    }
    
    alloc_entry cur;
    alloc_entry prev;
    alloc_entry* pprev = find_candidate_prev(prev, cur, size);
    if(pprev == NULL) {
      return 0;
    }

    union {
      alloc_entry new_prev;
      uint64_t ll;
    } a;
    uint32_t block_count = ((size+sizeof(entry_header)) / sizeof(alloc_entry)) + 1; // XXX: 適当

    a.new_prev.next = prev.next + block_count;
    a.new_prev.size = prev.size; 

    union {
      alloc_entry new_cur;
      uint64_t ll;
    } b;
    b.new_cur.next = cur.next;
    b.new_cur.size = cur.size - block_count * sizeof(alloc_entry);

    if(b.new_cur.size != 0) {
      alloc_entry _ig;
      alloc_entry next;
      alloc_entry* pnext = get_next(&a.new_prev, _ig, next);
      if(memcmp(pprev, &prev, sizeof(prev)) == 0 && // pnextの領域は未使用か
         __sync_bool_compare_and_swap((uint64_t*)pnext, *(uint64_t*)&next, b.ll)) {
      } else {
        return allocate(size);
      }
    } 

    if(__sync_bool_compare_and_swap((uint64_t*)pprev, *(uint64_t*)&prev, a.ll)) {
      // NOTE: ここでSIGKILLが送られたらメモリリークする
      entry_header* h = (entry_header*)&entries_[prev.next];
      h->size = block_count * sizeof(alloc_entry);
      return prev.next;
    } else {
      return allocate(size);
    }
  }
  
  void release(uint32_t index) {
    alloc_entry cur;
    alloc_entry* pprev = &entries_[0];
    alloc_entry prev = *pprev; // XXX: 
    while(pprev != NULL && prev.next < index) {
      pprev = get_next(pprev, cur, prev); // XXX: 順番
    }

    if(pprev == NULL) {
      release(index);
      return;
    }

    entry_header* h = (entry_header*)&entries_[index];
    h->ae.next = prev.next;
    h->ae.size = h->size;
    
    union {
      alloc_entry new_prev;
      uint64_t ll;
    } a;
    
    a.new_prev.next = index;
    a.new_prev.size = prev.size;

    if(! __sync_bool_compare_and_swap((uint64_t*)pprev, *(uint64_t*)&prev, a.ll)) {
      release(index);
    }
  }

  void* get_ptr(uint32_t index) {
    return (void*)((char*)&entries_[index] + sizeof(entry_header));
  }

  void dump() {
    
  }

private:
  alloc_entry* get_next(alloc_entry* pe, alloc_entry& e, alloc_entry& next) {
    e = *pe;
    alloc_entry* pnext = &entries_[e.next];
    if(memcmp(&e, pe, sizeof(e)) == 0) {
      next = *pnext;
      return pnext;
    } else {
      return NULL;
    }
  }

  alloc_entry* find_candidate_prev(alloc_entry& prev, alloc_entry& cur, uint32_t size) {
    alloc_entry* head = &entries_[0];
    return find_candidate_prev(head, prev, cur, size);
  }

  alloc_entry* find_candidate_prev(alloc_entry* pprev, alloc_entry& prev, alloc_entry& cur, uint32_t size) {
    alloc_entry* pcur = get_next(pprev, prev, cur);
    if(pcur == NULL) {
      return find_candidate_prev(prev, cur, size);
    }

    if(cur.next == 0) {
      return NULL;
    }
    
    if(sizeof(entry_header) + size < cur.size) {
      return pprev;
    }

    alloc_entry next;
    alloc_entry* pnext = get_next(pcur, cur, next);
    if(pnext == NULL) {
      return find_candidate_prev(prev, cur, size);
    }
    
    if(cur.next-prev.next == cur.size/sizeof(alloc_entry)) {
      union {
        alloc_entry new_cur;
        uint64_t ll;
      } a;
      a.new_cur.next = next.next;
      a.new_cur.size = cur.size + next.size;
      if(__sync_bool_compare_and_swap((uint64_t*)pcur, *(uint64_t*)&cur, a.ll)) {
        return find_candidate_prev(pprev, prev, cur, size);
      }
    }

    return find_candidate_prev(pcur, prev, cur, size);
  }

private:
  void* ptr_;
  alloc_entry* entries_;
  uint32_t size_;

  // TODO: total_size;
  // TODO: free_size;
};

#endif
