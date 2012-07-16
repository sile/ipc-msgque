#ifndef IPC_ALLOCATOR_HH
#define IPC_ALLOCATOR_HH

#include <inttypes.h>
#include <string.h>

#include <assert.h>

class allocator {
  struct entry {
    uint32_t next;
    uint32_t size:30; 
    uint32_t status:2;

    entry() {}
    entry(uint32_t next, uint32_t size, uint32_t status=0) 
      : next(next), size(size), status(status) {}
    uint64_t uint64() const { return *(uint64_t*)this; }
  };

  struct candidate {
    entry* pprev;
    entry  prev;
    entry* pcur;
    entry  cur;
  };

public:
  allocator(void* ptr, uint32_t size) 
    : entries_((entry*)ptr), 
      capacity_(size / sizeof(entry)) {
    assert(capacity_ > 2);
  }

  void init() {
    entries_[0].next   = 1;
    entries_[0].size   = 0;
    entries_[0].status = 0;

    entries_[1].next   = capacity_;
    entries_[1].size   = capacity_ - 1;
    entries_[1].status = 0;
  }

  uint32_t allocate(uint32_t byte_size) {
    if(byte_size == 0) {
      return 0;
    }
    
    uint32_t size = ((byte_size+sizeof(entry)-1) / sizeof(entry)) + 1;
    
    candidate cand;
    if(find_candidate(cand, size) == false) {
      return 0;
    }

    entry new_cur(cand.cur.next,
                  cand.cur.size - size);
    
    if(__sync_bool_compare_and_swap((uint64_t*)cand.pcur, cand.cur.uint64(), new_cur.uint64())) {
      uint32_t index = cand.pcur-entries_ + new_cur.size;
      entries_[index].size = size;
      return index;
    } else {
      return allocate(size);
    }
  }

private:
  bool find_candidate(candidate& cand, uint32_t size, int retry=0) {
    cand.pprev = &entries_[0];
    cand.prev  = *cand.pprev;
    return find_candidate2(cand, size, retry);
  }

  bool find_candidate2(candidate& cand, uint32_t size, int retry) {
    assert(retry < 100);
    
    if(fill_next_entry(cand) == false) {
      return find_candidate(cand, size, retry+1);
    }

    if(mark_marge_status(cand) == false) {
      return find_candidate(cand, size, retry+1);
    }
    
    if(merge_adjacent_entries(cand) == false) {
      return find_candidate(cand, size, retry+1);
    }

    if(cand.cur.status == 0 && size < cand.cur.size) {
      return true;
    }

    if(cand.cur.next >= capacity_) {
      return false;
    }

    cand.pprev = cand.pcur;
    cand.prev  = cand.cur;
    return find_candidate2(cand, size, retry);
  }
  
  bool fill_next_entry(candidate& cand) {
    cand.pcur = &entries_[cand.prev.next];
    cand.cur  = *cand.pcur;
    return cand.pprev->uint64() == cand.prev.uint64();
  }

  bool merge_adjacent_entries(candidate& cand) {
    if(!(cand.prev.status & 1 && cand.cur.status & 2)) {
      return true;
    }
    assert(cand.prev.next == (cand.pprev-entries_) + cand.prev.size);

    entry new_prev(cand.cur.next,
                   cand.prev.size + cand.cur.size,
                   cand.prev.status & 2 | cand.cur.status & 1);
    
    return __sync_bool_compare_and_swap((uint64_t*)cand.pprev, cand.prev.uint64(), new_prev.uint64());
  }

  bool mark_marge_status(candidate& cand) {
    if(cand.prev.next == (cand.pprev-entries_) + cand.prev.size) {
      entry new_prev(cand.prev.next,
                     cand.prev.size,
                     cand.prev.status | 1);
      if(__sync_bool_compare_and_swap((uint64_t*)cand.pprev, cand.prev.uint64(), new_prev.uint64())) {
        cand.prev = new_prev;

        entry new_cur(cand.cur.next,
                      cand.cur.size,
                      cand.cur.status | 2);
        if(__sync_bool_compare_and_swap((uint64_t*)cand.pcur, cand.cur.uint64(), new_cur.uint64())) {
          cand.cur = new_cur;
          return true;
        }
      }
      return false;
    }
    return true;
  }

private:  
  entry* entries_;
  const uint32_t capacity_;
};
/*
  void release(uint32_t index, int retry=0) {
    assert(retry < 1000);
    
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

    if(prev.merged != 0) {
      // TODO: merge処理追加 (無限ループ対策)
      if(prev.merged & 1 && next.merged & 2) {
        alloc_entry new_prev;
        new_prev.next = next.next;
        new_prev.size = prev.size + next.size;
        new_prev.merged = prev.merged & 2;
        if(next.merged & 1) {
          new_prev.merged |= 1;
        }
        if(__sync_bool_compare_and_swap((uint64_t*)pprev, prev.uint64(), new_prev.uint64())) {
          
        } else {
          usleep(100);
        }
      } else {
        usleep(1000);
      }

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
    h->ae.merged = 0;
    
    alloc_entry new_prev;
    new_prev.next = index;
    new_prev.size = prev.size;
    new_prev.merged = 0;

    if(new_prev.next == (pprev-entries_)+prev.size/sizeof(alloc_entry)) {
      new_prev.next = prev.next;
      new_prev.size = prev.size + h->size;
    }

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
      std::cout << "[" << head-entries_ << "] " << head->next << ", " << head->size << ", " << head->merged << std::endl;
      if(head->next == 0) {
        break;
      }
      head = &entries_[head->next];
    }
  }

private:

private:
  void* ptr_;
  alloc_entry* entries_;
  uint32_t size_;

  // TODO: total_size;
  // TODO: free_size;
};
*/
#endif
