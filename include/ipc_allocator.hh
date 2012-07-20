#ifndef IPC_ALLOCATOR_HH
#define IPC_ALLOCATOR_HH

#include <inttypes.h>
#include <string.h>
#include <stdlib.h>
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

  template <class T>
  T* ptr(uint32_t index) {
    return (T*)&entries_[index+1];
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
      return allocate(byte_size);
    }
  }

  void release(uint32_t index, int retry=0) {
    // TODO: 既に空き領域になっていたらエラー出力
    assert(retry < 500);
    
    candidate cand;
    cand.pprev = &entries_[0];
    cand.prev  = *cand.pprev;
    
    while(cand.prev.next < index) {
      if(! (fill_next_entry(cand) &&
            mark_marge_status(cand) &&
            merge_adjacent_entries(cand))) {
        release(index, retry+1);
        return;
      }

      cand.pprev = cand.pcur;
      cand.prev  = cand.cur;
    }
    
    if(cand.prev.status != 0) {
      usleep(rand() % 1000);
      release(index, retry+1);
      return;
    }
    
    entry* new_next = &entries_[index];
    new_next->next   = cand.prev.next;
    new_next->status = 0;
    
    entry new_prev;
    if(index == (cand.pprev-entries_) + cand.prev.size) {
      new_prev.next = cand.prev.next;
      new_prev.size = cand.prev.size + new_next->size;
      new_prev.status = 0;
    } else {
      new_prev.next = index;
      new_prev.size = cand.prev.size;
      new_prev.status = 0;
    }

    if(! __sync_bool_compare_and_swap((uint64_t*)cand.pprev, cand.prev.uint64(), new_prev.uint64())) {
      release(index, retry+1);
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
    
    if(__sync_bool_compare_and_swap((uint64_t*)cand.pprev, cand.prev.uint64(), new_prev.uint64())) {
      cand.prev = new_prev;
      return fill_next_entry(cand);
    } else {
      return false;
    }
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

#endif
