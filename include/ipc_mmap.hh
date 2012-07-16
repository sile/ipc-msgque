#ifndef IPC_MMAP_HH
#define IPC_MMAP_HH

#include <string>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

class mmap_t {
public:
  mmap_t(const std::string& filepath, size_t size, mode_t mode=0666) {
    open(filepath.c_str(), O_CREAT|O_RDWR, mode); // TODO:
  }

  mmap_t(size_t size) : ptr_(NULL), size_(0) {
    ptr_ = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
    if(ptr_ != MAP_FAILED) {
      size_ = size;
    }
  }

  ~mmap_t() {
    munmap(ptr_, size_); // TODO: error handling
  }
  
  operator bool() const { return ptr_ != MAP_FAILED; }

  template <class T>
  T* ptr() const { return static_cast<T*>(ptr_); }
  
  size_t size() const { return size_; }
  
private:
  void* ptr_;
  size_t size_;
};

#endif 


