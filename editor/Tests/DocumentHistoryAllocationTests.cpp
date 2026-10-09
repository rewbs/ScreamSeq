#include "editor/TrackerDocument.hpp"
#include <cstdlib>
#include <iostream>
#include <new>
#if defined(_WIN32)
#include <malloc.h>
#endif

namespace {
thread_local ptrdiff_t failAfter = -1;
void allocation() {
  if(failAfter < 0) return;
  if(failAfter-- == 0) { failAfter = -1; throw std::bad_alloc(); }
}
void check(bool okay, const char *message) {
  if(!okay) throw std::runtime_error(message);
}
}

// This standalone executable has no plugin/UI threads. Only allocations during
// one document history call are faulted; fixture creation and assertions are not.
void *operator new(std::size_t size) {
  allocation();
  if(auto *p = std::malloc(size ? size : 1)) return p;
  throw std::bad_alloc();
}
void *operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
void operator delete[](void *p, std::size_t) noexcept { std::free(p); }
void *operator new(std::size_t size, std::align_val_t alignment) {
  allocation();
  void *p = nullptr;
#if defined(_WIN32)
  p = _aligned_malloc(size ? size : 1, std::size_t(alignment));
#else
  if(posix_memalign(&p, std::size_t(alignment), size ? size : 1)) p = nullptr;
#endif
  if(p) return p;
  throw std::bad_alloc();
}
void *operator new[](std::size_t size, std::align_val_t alignment) { return ::operator new(size, alignment); }
void operator delete(void *p, std::align_val_t) noexcept {
#if defined(_WIN32)
  _aligned_free(p);
#else
  std::free(p);
#endif
}
void operator delete[](void *p, std::align_val_t alignment) noexcept { ::operator delete(p, alignment); }
void operator delete(void *p, std::size_t, std::align_val_t alignment) noexcept { ::operator delete(p, alignment); }
void operator delete[](void *p, std::size_t, std::align_val_t alignment) noexcept { ::operator delete(p, alignment); }

namespace {
size_t annotationSweep(bool merge) {
  for(ptrdiff_t fault=0;fault<10000;++fault) {
    Tracker::Document document;
    document.annotate([](auto &n){n.ensureMixer();});
    const auto gestureBaseline=document.native();
    document.annotate([](auto &n){n.mixer.buses.front().gainDB=-3;});
    if(!merge) {
      document.annotate([](auto &n){n.mixer.buses.front().gainDB=-6;});
      document.undo(); // A failed new edit must preserve this redo branch too.
    }
    const auto before=document.native();
    const auto revision=document.revision,sequence=document.historySequence();
    const auto undo=document.historyHead(false),redo=document.historyHead(true);
    auto candidate=before;candidate.mixer.buses.front().gainDB=-9;
    unsigned publications=0;
    // Prepare the callable wrappers before fault injection just as a native
    // adapter does; fault every allocation within document/history admission.
    std::function<void(Tracker::NativeSong &)> change=[&](auto &n){n=candidate;};
    std::function<void()> publish=[&]{++publications;};
    bool rejected=false;
    failAfter=fault;
    try {document.annotate(change,publish,merge?sequence:0);}
    catch(const std::bad_alloc &){rejected=true;}
    catch(...){failAfter=-1;throw;}
    failAfter=-1;
    if(rejected) {
      check(publications==0,"Mixer controls were published before history allocation finished");
      check(document.native()==before && document.revision==revision && document.historySequence()==sequence &&
        document.historyHead(false)==undo && document.historyHead(true)==redo,
        "Failed mixer admission changed data, revision, Undo or Redo");
      document.annotate(change,publish,merge?sequence:0);
    }
    check(publications==1 && document.native()==candidate && document.revision==revision+1 && !document.canRedo(),
      "Retried mixer admission must publish and commit exactly once");
    document.undo();
    check(document.native()==(merge?gestureBaseline:before),"Mixer admission lost its original Undo baseline");
    document.redo();check(document.native()==candidate,"Mixer admission lost its Redo value");
    if(!rejected) {check(fault>0,"Annotation sweep did not reach allocation staging");return size_t(fault);}
  }
  throw std::runtime_error("Annotation allocation sweep did not terminate");
}

void refusedAnnotation(bool merge) {
  Tracker::Document document;
  document.annotate([](auto &n){n.ensureMixer();});
  const auto baseline=document.native();
  document.annotate([](auto &n){n.mixer.buses.front().gainDB=-3;});
  const auto before=document.native();
  const auto revision=document.revision,sequence=document.historySequence(),undo=document.historyHead(false);
  bool rejected=false;unsigned publications=0;
  try {
    document.annotate([](auto &n){n.mixer.buses.front().gainDB=-9;},[&]{
      ++publications;throw std::runtime_error("Mixer queue rejected");
    },merge?sequence:0);
  } catch(const std::runtime_error &){rejected=true;}
  check(rejected && publications==1 && document.native()==before && document.revision==revision &&
    document.historySequence()==sequence && document.historyHead(false)==undo && !document.canRedo(),
    "Refused mixer publication must roll back staged history");
  document.undo();check(document.native()==baseline,"Refused mixer publication damaged its prior Undo snapshot");
  document.redo();check(document.native()==before,"Refused mixer publication damaged its prior Redo snapshot");
}

size_t sweep(bool redo, bool mixed, bool publication=false) {
  for(ptrdiff_t fault = 0; fault < 10000; ++fault) {
    Tracker::Document document;
    const auto original = document.native();
    const auto dry = document.cell(0, 2, 0);
    auto next = original;
    next.ensureMixer();
    for(unsigned i = 0; i < 12; ++i)
      next.signal.layout["position with a separately allocated stable key " + std::to_string(i)] = {double(i * 20), 150};
    auto note = dry; note.note = 61; note.instrument = 1;
    if(mixed) document.editNative(next, {{0, 2, 0, {}, note}});
    else document.annotate([&](auto &native) { native = next; });
    const auto changed = document.native();
    if(redo) document.undo();
    const auto before = document.native();
    const auto beforeCell = document.cell(0, 2, 0);
    const auto revision = document.revision, sequence = document.historySequence();
    const auto undoHead = document.historyHead(false), redoHead = document.historyHead(true);
    bool rejected = false,published=false;
    std::function<void()> commit;if(publication)commit=[&]{published=true;};
    failAfter = fault;
    try { if(redo) document.redo(commit); else document.undo(commit); }
    catch(const std::bad_alloc &) { rejected = true; }
    catch(...) { failAfter = -1; throw; }
    failAfter = -1;
    if(rejected) {
      check(!published,"External plan was published before an allocation failure");
      check(document.native() == before && document.cell(0, 2, 0) == beforeCell,
        "Allocation failure partially changed native metadata or pattern cells");
      check(document.revision == revision && document.historySequence() == sequence &&
        document.historyHead(false) == undoHead && document.historyHead(true) == redoHead,
        "Allocation failure changed history heads or revision");
      // Retry and reverse the same entry: head equality alone cannot detect a
      // source snapshot damaged by a partially completed throwing move.
      if(redo) document.redo(); else document.undo();
    }
    check(document.native() == (redo ? changed : original) &&
      document.cell(0, 2, 0) == (redo && mixed ? note : dry), "History retry did not restore the complete entry");
    check(document.revision == revision + 1 && document.historySequence() == sequence,
      "History movement changed its identity or advanced revision twice");
    if(redo) document.undo(); else document.redo();
    check(document.native() == before && document.cell(0, 2, 0) == beforeCell,
      "History destination lost data after a rejected allocation");
    if(!rejected) { check(fault > 0, "Allocation injection did not reach history staging"); return size_t(fault); }
  }
  throw std::runtime_error("History allocation sweep did not terminate");
}
void refusedPublication(bool redo) {
  Tracker::Document d;d.annotate([](auto &n){n.ensureMixer();});if(redo)d.undo();
  const auto before=d.native();const auto revision=d.revision,undo=d.historyHead(false),redoHead=d.historyHead(true);
  bool called=false;try {
    auto refuse=[&]{called=true;throw std::runtime_error("Queue rejected");};
    if(redo)d.redo(refuse);else d.undo(refuse);check(false,"Publication rejection did not propagate");
  } catch(const std::runtime_error &){}
  check(called&&d.native()==before&&d.revision==revision&&d.historyHead(false)==undo&&d.historyHead(true)==redoHead,"Refused publication changed data or left staged destination history");
  if(redo)d.redo();else d.undo();if(redo)d.undo();else d.redo();check(d.native()==before,"Refused publication damaged a source snapshot");
  Tracker::Document mixed;auto cell=mixed.cell(0,0,0);cell.note=61;mixed.edit({{0,0,0,{},cell}});if(redo)mixed.undo();
  called=false;bool rejected=false;try{if(redo)mixed.redo([&]{called=true;});else mixed.undo([&]{called=true;});}catch(const std::invalid_argument &){rejected=true;}
  check(rejected&&!called,"Cell history must refuse live publication before invoking the callback");
}

}

int main() {
  try {
    size_t failures = 0;
    for(bool redo : {false, true}) for(bool mixed : {false, true}) failures += sweep(redo, mixed);
    for(bool redo:{false,true}){failures+=sweep(redo,false,true);refusedPublication(redo);}
    for(bool merge:{false,true}){failures+=annotationSweep(merge);refusedAnnotation(merge);}
    std::cout << "PASS " << failures << " injected allocation failures: native admission and mixed-cell Undo/Redo remain atomic, retryable and reversible\n";
    return 0;
  } catch(const std::exception &error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
