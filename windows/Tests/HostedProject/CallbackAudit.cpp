// Executable-local C++ allocation probe; direct malloc/free, locks and vendor
// internals are outside its coverage. Uses the existing WASAPI audit scope.
#include "windows/Audio/RealtimeAudit.hpp"
#include <malloc.h>
#include <cstdlib>
#include <new>
void *operator new(std::size_t n) {
  ScreamSeq::AudioAudit::allocated();
  if(auto p=std::malloc(n?n:1)) return p;
  throw std::bad_alloc();
}
void *operator new[](std::size_t n) {return ::operator new(n);}
void operator delete(void *p) noexcept {ScreamSeq::AudioAudit::freed(p);std::free(p);}
void operator delete[](void *p) noexcept {::operator delete(p);}
void operator delete(void *p,std::size_t) noexcept {::operator delete(p);}
void operator delete[](void *p,std::size_t) noexcept {::operator delete(p);}
void *operator new(std::size_t n,std::align_val_t alignment) {
  ScreamSeq::AudioAudit::allocated();
  if(auto p=_aligned_malloc(n?n:1,static_cast<std::size_t>(alignment))) return p;
  throw std::bad_alloc();
}
void *operator new[](std::size_t n,std::align_val_t a) {return ::operator new(n,a);}
void operator delete(void *p,std::align_val_t) noexcept {ScreamSeq::AudioAudit::freed(p);_aligned_free(p);}
void operator delete[](void *p,std::align_val_t a) noexcept {::operator delete(p,a);}
void operator delete(void *p,std::size_t,std::align_val_t a) noexcept {::operator delete(p,a);}
void operator delete[](void *p,std::size_t,std::align_val_t a) noexcept {::operator delete(p,a);}
