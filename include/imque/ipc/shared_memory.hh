#ifndef IMQUE_IPC_SHARED_MEMORY_HH
#define IMQUE_IPC_SHARED_MEMORY_HH

#include <string>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h> 
#include <fcntl.h>

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

namespace imque {
  namespace ipc {
    class SharedMemory {
    public:
      // 親子プロセス間で共有可能な無名メモリ領域を作成する
      SharedMemory(size_t size) : ptr_(MAP_FAILED), size_(size) {
	ptr_ = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
      }

      // 複数プロセス間で共有可能な名前つきメモリ領域を作成する
      SharedMemory(const std::string& filepath, size_t size, mode_t mode=0660) 
	: ptr_(MAP_FAILED), size_(size) {
	int fd = open(filepath.c_str(), O_CREAT|O_RDWR, mode);
	if(fd == -1) {
	  return;
	}

	if(ftruncate(fd, size) == 0) {
	  ptr_ = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	}
	close(fd);
      }
    
      ~SharedMemory() {
	if(ptr_ != MAP_FAILED) {
	  munmap(ptr_, size_);
	}
      }

      operator bool() const { return ptr_ != MAP_FAILED; }

      template <class T>
      T* ptr() const { return *this ? reinterpret_cast<T*>(ptr_) : NULL; }

      template <class T>
      T* ptr(size_t offset) const { return reinterpret_cast<T*>(ptr<char>()+offset); }
  
      size_t size() const { return size_; }
    
    private:
      void* ptr_;
      const size_t size_;
    };
  }
}

#endif
