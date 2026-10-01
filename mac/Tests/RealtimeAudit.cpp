#include <atomic>
#include <cstdlib>
#include <new>
#include <pthread.h>
// dyld interposition is confined to the test executable and its loaded images.
// The replacement image's own references resolve to the original functions.
namespace {
thread_local bool active = false;
thread_local uint64_t allocations = 0, deallocations = 0, locks = 0;
} // namespace
extern "C" void tracker_audit_begin() {
  allocations = deallocations = locks = 0;
  active = true;
}
extern "C" void tracker_audit_end(uint64_t *a, uint64_t *d, uint64_t *l) {
  active = false;
  *a = allocations;
  *d = deallocations;
  *l = locks;
}
static void *audit_malloc(size_t n) {
  if (active)
    ++allocations;
  return malloc(n);
}
static void *audit_calloc(size_t n, size_t size) {
  if (active)
    ++allocations;
  return calloc(n, size);
}
static void *audit_realloc(void *p, size_t n) {
  if (active)
    ++allocations;
  return realloc(p, n);
}
static void audit_free(void *p) {
  if (active && p)
    ++deallocations;
  free(p);
}
static int audit_memalign(void **p, size_t a, size_t n) {
  if (active)
    ++allocations;
  return posix_memalign(p, a, n);
}
static int audit_lock(pthread_mutex_t *m) {
  if (active)
    ++locks;
  return pthread_mutex_lock(m);
}
static int audit_rwread(pthread_rwlock_t *m) {
  if (active)
    ++locks;
  return pthread_rwlock_rdlock(m);
}
static int audit_rwwrite(pthread_rwlock_t *m) {
  if (active)
    ++locks;
  return pthread_rwlock_wrlock(m);
}
#define INTERPOSE(replacement, original)                                                                               \
  __attribute__((used)) static struct {                                                                                \
    const void *newFunction;                                                                                           \
    const void *oldFunction;                                                                                           \
  } interpose_##original                                                                                               \
      __attribute__((section("__DATA,__interpose"))) = {(const void *)&replacement, (const void *)&original};
INTERPOSE(audit_malloc, malloc)
INTERPOSE(audit_calloc, calloc)
INTERPOSE(audit_realloc, realloc)
INTERPOSE(audit_free, free)
INTERPOSE(audit_memalign, posix_memalign)
INTERPOSE(audit_lock, pthread_mutex_lock)
INTERPOSE(audit_rwread, pthread_rwlock_rdlock)
INTERPOSE(audit_rwwrite, pthread_rwlock_wrlock)
