// Linux/glibc-only test interposer. Loaded at startup by the allocation CTest;
// never linked into the DSP or pedal application. Use libc entry points rather
// than dlsym in allocator wrappers, avoiding recursive resolver allocations.
#include <stddef.h>
#include <stdint.h>
#include <errno.h>
extern void *__libc_malloc(size_t);
extern void *__libc_calloc(size_t,size_t);
extern void *__libc_realloc(void*,size_t);
extern void __libc_free(void*);
extern void *__libc_memalign(size_t,size_t);
static _Thread_local int enabled;
static _Thread_local size_t allocations, releases;
void pog3_malloc_begin(void) { allocations=releases=0; enabled=1; }
void pog3_malloc_end(size_t *a,size_t *f) { enabled=0; *a=allocations; *f=releases; }
void *malloc(size_t n) { if(enabled)++allocations; return __libc_malloc(n); }
void *calloc(size_t n,size_t m) { if(enabled)++allocations; return __libc_calloc(n,m); }
void *realloc(void *p,size_t n) { if(enabled)++allocations; return __libc_realloc(p,n); }
void free(void *p) { if(enabled && p)++releases; __libc_free(p); }
void *memalign(size_t a,size_t n) { if(enabled)++allocations; return __libc_memalign(a,n); }
void *aligned_alloc(size_t a,size_t n) { if(enabled)++allocations; return __libc_memalign(a,n); }
int posix_memalign(void **p,size_t a,size_t n) {
  if(a<sizeof(void*) || (a&(a-1)))return EINVAL;
  if(enabled)++allocations;
  void *q=__libc_memalign(a,n);
  if(!q)return ENOMEM;
  *p=q;return 0;
}
