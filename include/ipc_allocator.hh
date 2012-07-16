#ifndef IPC_ALLOCATOR_HH
#define IPC_ALLOCATOR_HH

#include <inttypes.h>
#include <string.h>

#include <assert.h>

struct alloc_entry {
  uint32_t next;
  uint32_t size:31; 
  uint32_t merged:1;
  
  uint64_t uint64() const { return *(uint64_t*)this; }
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
    entries_[0].merged = 0;

    entries_[1].next = 0;
    entries_[1].size = size_-sizeof(alloc_entry);
    entries_[1].merged = 0;
  }
  
  uint32_t allocate(uint32_t size) {
    if(size == 0) { 
      return 0;
    }
    
    uint32_t block_count = ((size+sizeof(entry_header)) / sizeof(alloc_entry)) + 1; // XXX: 適当

    alloc_entry cur;
    alloc_entry prev;
    alloc_entry* pprev = find_candidate_prev(prev, cur, block_count*sizeof(alloc_entry)); //size);
    if(pprev == NULL) {
      return 0;
    }
    alloc_entry* pcur = &entries_[prev.next];
    if(pprev->uint64() != prev.uint64()) {
      return allocate(size);
    }
    
    alloc_entry new_cur;
    new_cur.next = cur.next;
    new_cur.size = cur.size - (block_count * sizeof(alloc_entry));
    new_cur.merged = 0;

    if(__sync_bool_compare_and_swap((uint64_t*)pcur, cur.uint64(), new_cur.uint64())) {
      uint32_t index = prev.next + new_cur.size / sizeof(alloc_entry);
      entry_header* h = (entry_header*)&entries_[index];
      h->size = block_count * sizeof(alloc_entry);

      return index;
    } else {
      return allocate(size);
    }
  }
  
  void release(uint32_t index, int retry=0) {
    alloc_entry* pprev = &entries_[0];
    alloc_entry prev = *pprev;
    alloc_entry next; 
    while(pprev != NULL && 
          (prev.next != 0 && prev.next < index)) {
      pprev = get_next(pprev, prev, next);
      if(prev.next == next.next) {
        std::cout << "@ " << prev.next << " < " << index << std::endl;
        // dump();
      }
      assert(prev.next != next.next);
      prev = next;
    }

    if(pprev == NULL) {
      release(index, retry+1);
      return;
    }

    if(prev.next == index) {
      std::cout << "@@ " << prev.next << " == " << index << std::endl;
    }
    assert(prev.next != index);

    entry_header* h = (entry_header*)&entries_[index];
    h->ae.next = prev.next;
    h->ae.size = h->size;
    
    alloc_entry new_prev;
    new_prev.next = index;
    new_prev.size = prev.size;
    new_prev.merged = 0;

    if(! __sync_bool_compare_and_swap((uint64_t*)pprev, prev.uint64(), new_prev.uint64())) {
      release(index, retry+1);
    }
  }

  void* get_ptr(uint32_t index) {
    return (void*)((char*)&entries_[index] + sizeof(entry_header));
  }

  void dump() {
    std::cout << "--------------" << std::endl;
    alloc_entry* head = &entries_[0];
    for(;;) {
      std::cout << "[" << head-entries_ << "] " << head->next << ", " << head->size << std::endl;
      if(head->next == 0) {
        break;
      }
      head = &entries_[head->next];
    }
  }

private:
  alloc_entry* get_next(alloc_entry* pe, alloc_entry& e, alloc_entry& next) {
    e = *pe;
    alloc_entry* pnext = &entries_[e.next];
    next = *pnext;
    if(e.uint64() == pe->uint64()) {
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

    if(size < cur.size) {
      return pprev;
    }

    if(cur.next == 0) {
      return NULL;
    }

    alloc_entry next;
    alloc_entry* pnext = get_next(pcur, cur, next);
    if(pnext == NULL) {
      return find_candidate_prev(prev, cur, size);
    }

    if(cur.next == (pcur-entries_)+cur.size/sizeof(alloc_entry)) {
      next.merged = 1;
      alloc_entry new_cur;
      new_cur.next = next.next;
      new_cur.size = cur.size + next.size;
      if(pnext->uint64() == next.uint64() &&
         __sync_bool_compare_and_swap((uint64_t*)pcur, cur.uint64(), new_cur.uint64())) {
        assert(pnext->uint64() == next.uint64());
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
