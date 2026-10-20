#include "editor/TrackerDocument.hpp"
#include <iostream>
#include <memory>
#include <stdexcept>

using namespace Tracker;
namespace {
void check(bool okay,const char *message){if(!okay)throw std::runtime_error(message);}
void reject(Document &document,int order,int pattern,const std::string &operation) {
  const auto before=document.native();const auto bytes=document.snapshotData();
  const auto revision=document.revision,undo=document.historyHead(false),redo=document.historyHead(true);
  bool rejected=false;
  try{document.orderEditChanges(order,pattern,operation);}catch(const std::invalid_argument &){rejected=true;}
  check(rejected,"Invalid order operation passed preflight");rejected=false;
  try{document.editOrder(order,pattern,operation);}catch(const std::invalid_argument &){rejected=true;}
  check(rejected&&document.native()==before&&document.snapshotData()==bytes&&document.revision==revision&&
    document.historyHead(false)==undo&&document.historyHead(true)==redo,"Invalid order operation changed state or history");
}
void checks() {
  auto storage=std::make_unique<Document>();auto &document=*storage;
  const int pattern=document.song().Order()[0];
  const auto initial=document.native();const auto revision=document.revision;
  const auto initialBytes=document.snapshotData();
  check(!document.orderEditChanges(0,0,"move")&&!document.orderEditChanges(0,pattern,"assign"),"Exact move/assign must be no-ops");
  document.editOrder(0,0,"move");document.editOrder(0,pattern,"assign");
  check(document.native()==initial&&document.snapshotData()==initialBytes&&document.revision==revision&&!document.canUndo(),
    "No-op changed song bytes, created history or changed identity");
  reject(document,-1,0,"move");reject(document,0,-1,"move");reject(document,0,99999,"assign");reject(document,0,65536,"assign");
  reject(document,0,0,"remove");reject(document,0,0,"up");reject(document,0,0,"unknown");
  document.editOrder(0,pattern,"after");const auto inserted=document.native();
  const auto &sequence=inserted.sequences.at(document.song().Order.GetCurrentSequenceIndex()).orders;
  check(sequence.size()==2&&sequence[0].id!=sequence[1].id,"Repeated pattern occurrences need distinct stable order identities");
  check(document.orderEditChanges(0,1,"move"),"Identical pattern values must not hide a real occurrence move");
  document.editOrder(0,1,"move");const auto moved=document.native();
  const auto &movedOrders=moved.sequences.at(document.song().Order.GetCurrentSequenceIndex()).orders;
  check(movedOrders[0].id==sequence[1].id&&movedOrders[1].id==sequence[0].id,"Move lost occurrence identities");
  document.undo();check(document.native()==inserted&&document.canRedo(),"Undo failed to restore order identities");
  const auto undoRevision=document.revision,redo=document.historyHead(true),head=document.historyHead(false);
  document.editOrder(0,0,"move");document.editOrder(0,pattern,"assign");
  check(document.revision==undoRevision&&document.historyHead(true)==redo&&document.historyHead(false)==head,
    "No-op discarded the Redo branch or advanced revision");
  document.redo();check(document.native()==moved,"Redo lost the complete occurrence move");
}
}
int main(){try{checks();std::cout<<"PASS shared order validation, exact no-op admission and repeated-pattern occurrence history\n";return 0;}
catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
