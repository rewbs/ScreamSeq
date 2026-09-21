#pragma once
// Windows presentation state only. Musical data continues to belong to Document.
#include "../Api/SessionAdapter.hpp"
#include <array>
#include <string>

namespace ScreamSeq {
struct WorkspaceRect {
	float x{}, y{}, w{}, h{};
	bool contains(float px, float py) const { return px>=x && py>=y && px<x+w && py<y+h; }
};
struct WorkspaceGeometry {
	WorkspaceRect pattern, inspector, graph, automation, verticalDivider, horizontalDivider;
};
struct InspectorState {
	bool pinned=false, opened=false, hidden=false;
	Api::Json target=Api::Json::object(), origin=Api::Json::object();
	unsigned sample=1;
};
class WorkspaceState {
public:
	std::array<InspectorState,2> panels;
	std::string layout="Compose", active="notes", focus="pattern";
	float rightWidth=340, lowerHeight=180;

	static size_t index(const std::string &id) {
		if(id=="notes") return 0;
		if(id=="samples") return 1;
		throw Api::ApiError(-32602,"Choose notes or samples; other Windows editors are unavailable");
	}
	InspectorState &panel(const std::string &id) { return panels[index(id)]; }
	const InspectorState &panel(const std::string &id) const { return panels[index(id)]; }
	bool visible() const { return layout!="Pattern focus" && !panel(active).hidden; }
	void place(const std::string &id, bool hide) {
		panel(id).hidden=hide;
		if(hide && active==id) {
			const std::string other=id=="notes" ? "samples" : "notes";
			if(!panel(other).hidden) active=other;
			if(focus==id) focus="pattern";
		} else if(!hide && panel(active).hidden) active=id;
	}
	void capture(const std::string &id, const Api::Json &position, unsigned sample, bool force=false) {
		auto &p=panel(id);
		if(!p.opened) { p.origin=position; p.opened=true; }
		if(force || !p.pinned) {
			p.target=position;
			if(sample) p.sample=sample;
		}
	}
	void show(const std::string &id, const Api::Json &position, unsigned sample, bool takeFocus) {
		if(layout=="Pattern focus") layout="Compose";
		active=id; panel(id).hidden=false;
		if(focus!="pattern" && focus!=id) focus="pattern";
		capture(id,position,sample);
		if(takeFocus) focus=id;
	}
	WorkspaceGeometry geometry(float width,float height,float minimumLowerHeight=128) const {
		WorkspaceGeometry g;
		const float left=170, top=88, bottom=std::max(top+180,height-44);
		const bool compose=layout!="Pattern focus";
		const float right=visible() ? width-std::clamp(rightWidth,300.0f,std::max(300.0f,width-left-350)) : width-8;
		const float split=compose ? bottom-std::clamp(std::max(lowerHeight,minimumLowerHeight),128.0f,std::max(128.0f,bottom-top-180)) : bottom;
		g.pattern={left,top,std::max(1.0f,right-left-6),std::max(1.0f,split-top-6)};
		if(visible()) { g.inspector={right,top,std::max(1.0f,width-right-8),std::max(1.0f,split-top-6)}; g.verticalDivider={right-6,top,6,split-top}; }
		if(compose) {
			const float divider=visible() ? right : left+(width-left)*0.62f;
			g.graph={left,split,divider-left-6,bottom-split};
			g.automation={divider,split,width-divider-8,bottom-split};
			g.horizontalDivider={left,split-6,width-left-8,6};
		}
		return g;
	}
};
}
