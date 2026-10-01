#pragma once
#include <cstdint>
// Sanitizers interpose allocation themselves; run the real-time audit in the
// normal build and the memory/undefined-behaviour checks in a separate build.
#ifdef TRACKER_SANITIZER
static void tracker_audit_begin() {}
static void tracker_audit_end(uint64_t *a,uint64_t *f,uint64_t *l) { *a=*f=*l=0; }
#else
extern "C" void tracker_audit_begin();
extern "C" void tracker_audit_end(uint64_t *,uint64_t *,uint64_t *);
#endif
