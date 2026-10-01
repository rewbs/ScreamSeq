import AppKit

// These are view-only hulls, not mixer buses, graph definitions or editable
// endpoints. The host publishes audible rank, not every inactive delay slot.
struct SignalStageDrawing {
  let key:String,role:String,title:String,summary:String
  let members:[String],audible:[String],tails:Set<String>,inactive:Set<String>
  let rect:NSRect,paintRect:NSRect,titleRect:NSRect,summaryRect:NSRect
  let layout:[NSRect],sockets:[NSPoint]
  let border:NSBezierPath,paths:[NSBezierPath]
}
final class SignalStagePresentation {
  private struct Member:Equatable {
    let id:String,title:String,target:String,role:String,rect:NSRect,input:NSPoint,output:NSPoint
  }
  private struct Activity:Equatable {let id:String,order:Int,tail:Bool}
  private var previousMembers=[Member](),previousActivity=[Activity](),previousNames=[String:String]()
  private var previousPlaying:Bool?
  private(set) var drawings=[SignalStageDrawing]()
  private(set) var generation=0
  func update(nodes:[SignalCanvasNode],activity:[[String:Any]],playing:Bool,names:[String:String])->[SignalStageDrawing]? {
    let members=nodes.compactMap{node -> Member? in
      let parts=node.id.split(separator:":",omittingEmptySubsequences:false)
      guard parts.count==4,parts[0]=="graph",["Row","Persistent","Ordinary"].contains(String(parts[2]))else{return nil}
      return Member(id:node.id,title:node.title,target:String(parts[1]),role:String(parts[2]),rect:node.rect,input:node.portPoint(.init(),output:false),output:node.portPoint(.init(),output:true))
    }
    let visible=Set(members.map(\.id))
    let state=activity.compactMap{item -> Activity? in
      let role=(item["role"] as? String ?? "").capitalized
      let id="graph:\(item["target"] as? String ?? ""):\(role):\(item["graph"] as? String ?? "")"
      guard visible.contains(id)else{return nil}
      return Activity(id:id,order:max(0,item["order"] as? Int ?? 0),tail:item["tail"] as? Bool==true)
    }.sorted{$0.id<$1.id}
    let relevantNames=names.filter{pair in members.contains{$0.target==pair.key}}
    guard members != previousMembers || state != previousActivity || playing != previousPlaying || relevantNames != previousNames else{return nil}
    previousMembers=members;previousActivity=state;previousPlaying=playing;previousNames=relevantNames;generation+=1
    let active=Dictionary(state.map{($0.id,$0)},uniquingKeysWith:{first,_ in first})
    let grouped=Dictionary(grouping:members,by:{$0.target+":"+$0.role})
    drawings=grouped.keys.sorted().compactMap{key in
      guard let members=grouped[key],let first=members.first else{return nil}
      let body=members.dropFirst().reduce(first.rect){$0.union($1.rect)}
      let rect=NSRect(x:body.minX-16,y:max(0,body.minY-8),width:max(220,body.width+32),height:body.maxY+43-max(0,body.minY-8))
      // Footer labels fit the existing inter-row space without moving cards or
      // clipping names when a manually placed card touches the canvas top.
      let titleRect=NSRect(x:rect.minX+10,y:body.maxY+3,width:rect.width-20,height:18)
      let summaryRect=NSRect(x:rect.minX+10,y:body.maxY+21,width:rect.width-20,height:18)
      let ordered=playing ? members.filter{(active[$0.id]?.order ?? 0)>0}.sorted{let a=active[$0.id]!.order,b=active[$1.id]!.order;return a==b ? $0.id<$1.id:a<b}:[]
      let tails=playing ? Set(members.filter{active[$0.id]?.tail==true}.map(\.id)):[]
      let audible=ordered.map(\.id),inactive=playing ? Set(members.map(\.id)).subtracting(audible).subtracting(tails):[]
      let names=ordered.map(\.title).joined(separator:" → ")
      let tailNames=members.filter{tails.contains($0.id)}.map(\.title).joined(separator:", ")
      let summary=playing ? (ordered.isEmpty ? "No audible copy · timing retained":"Audible order: "+names)+(tails.isEmpty ? "":" · Tails: "+tailNames):"Stopped · configured copies; order follows pattern commands"
      var paths=[NSBezierPath]()
      func connect(_ from:NSPoint,_ to:NSPoint){
        let path=NSBezierPath();path.move(to:from)
        let bend=max(24,min(100,abs(to.x-from.x)*0.35))
        path.curve(to:to,controlPoint1:NSPoint(x:from.x+bend,y:from.y),controlPoint2:NSPoint(x:to.x-bend,y:to.y))
        path.move(to:NSPoint(x:to.x-6,y:to.y-3));path.line(to:to);path.line(to:NSPoint(x:to.x-6,y:to.y+3));paths.append(path)
      }
      if let start=ordered.first,let end=ordered.last {
        connect(NSPoint(x:rect.minX,y:start.input.y),start.input)
        for pair in zip(ordered,ordered.dropFirst()){connect(pair.0.output,pair.1.input)}
        connect(end.output,NSPoint(x:rect.maxX,y:end.output.y))
      }
      let paint=paths.reduce(rect){$0.union($1.bounds)}.insetBy(dx:-5,dy:-5)
      return SignalStageDrawing(key:key,role:first.role,title:(relevantNames[first.target] ?? first.target)+" › "+first.role+" · \(members.count) "+(members.count==1 ? "copy":"copies"),summary:summary,members:members.map(\.id),audible:audible,tails:tails,inactive:inactive,rect:rect,paintRect:paint,titleRect:titleRect,summaryRect:summaryRect,layout:members.map(\.rect),sockets:members.flatMap{[$0.input,$0.output]},border:NSBezierPath(roundedRect:rect,xRadius:8,yRadius:8),paths:paths)
    }
    return drawings
  }
}
