#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace Tracker {
inline constexpr uint32_t signalNotePort=UINT32_MAX-1;
// Document-owned, presentation-only data. IDs are local to this presentation
// namespace and must never be used as processor or musical identities.
struct SignalVisualRegion {
  std::string id, title, text, scope;
  bool comment=false,collapsed=false;
  double x=0,y=0,width=320,height=180;
  uint32_t color=0x658b82;
  std::vector<std::string> nodes;
  bool operator==(const SignalVisualRegion &)const=default;
};
struct SignalCableGeometry {
  // Stable real endpoints, before processing-group or visual-frame projection.
  std::string source,target;
  uint32_t output=0,input=0;
  bool modulation=false;
  std::vector<std::array<double,2>> points;
  std::string connection; // Optional stable cable key; distinguishes event channel mappings.
  bool operator==(const SignalCableGeometry &)const=default;
};
struct SignalPresentation {
  std::vector<SignalVisualRegion> regions;
  std::vector<SignalCableGeometry> cables;
  // Stable real node keys; compact cards never alter processors or routes.
  std::vector<std::string> collapsedNodes;
  bool operator==(const SignalPresentation &)const=default;
  bool empty()const{return regions.empty()&&cables.empty()&&collapsedNodes.empty();}
  size_t bytes()const;
  void validate()const;
};
// Never edit audio edges. Removing a node removes its presentation references;
// removing an annotation or a reroute point leaves all processors/routes alone.
void pruneSignalPresentation(SignalPresentation &,const std::set<std::string> &available);
void remapSignalPresentation(SignalPresentation &,const std::map<std::string,std::string> &);
void removeSignalPresentationNode(SignalPresentation &,const std::string &);
void retargetSignalCableGeometry(SignalPresentation &,const SignalCableGeometry &before,const SignalCableGeometry &after);
struct NoteRoute;
struct NoteRouting;
void replaceNoteAssignmentGeometry(SignalPresentation &,const NoteRoute &);
void reconcileNoteCableGeometry(SignalPresentation &,const NoteRouting &);
}
