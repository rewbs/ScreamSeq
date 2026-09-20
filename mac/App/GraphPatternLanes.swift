import AppKit

struct GraphPatternCommand {
  var target:String,graph:String,kind:String
  var position:Int,column:Int,number:Int
  var amount:Double
  var row:Int{position/65536}
  var display:String{let offset=position%65536==0 ? "" : "~";let symbol=["row":"R","start":"S","stop":"X","clear":"CLR","amount":"A","wet":"W"][kind] ?? "?";return offset + (kind=="clear" ? symbol : String(format:"%@%03d %@",symbol,number,["stop"].contains(kind) ? "" : String(format:"%02X",Int((amount.isFinite ? max(0,min(1,amount)) : 0)*255))))}
}
struct GraphPatternLane {var target:String,name:String,column:Int}
final class GraphLaneStrip:NSView {
  weak var pattern:PatternView?
  var lanes=[GraphPatternLane](),commands=[String:GraphPatternCommand](),selected=0,firstLane=0
  var onEdit:((String,Int,Int)->Void)?,onClear:((String,Int,Int)->Void)?
  private var state=""
  override var isFlipped:Bool{true}
  override var acceptsFirstResponder:Bool{true}
  override init(frame:NSRect){super.init(frame:frame);setAccessibilityRole(.group);setAccessibilityLabel("Pattern graph command lanes");setAccessibilityHelp("Graph commands aligned to pattern rows. Arrow keys select row and lane. Return edits, Delete removes the command.")}
  required init?(coder:NSCoder){fatalError()}
  static func key(_ row:Int,_ target:String,_ column:Int)->String{"\(row):\(target):\(column)"}
  func refresh(){guard let pattern else{return};let newState="\(pattern.model.revisionToken):\(pattern.model.pattern):\(pattern.firstRow):\(pattern.cursorRow):\(pattern.playRow):\(pattern.headerHeight):\(pattern.rowHeight):\(firstLane):\(selected)";guard newState != state else{return};state=newState;lanes=pattern.model.graphLanes;commands=pattern.model.graphCommands;selected=min(selected,max(0,lanes.count-1));firstLane=min(firstLane,max(0,lanes.count-1));needsDisplay=true}
  private func text(_ value:String,x:CGFloat,y:CGFloat,color:NSColor,size:CGFloat=11,width:CGFloat=116){let paragraph=NSMutableParagraphStyle();paragraph.lineBreakMode = .byTruncatingTail;(value as NSString).draw(in:NSRect(x:x,y:y,width:width,height:18),withAttributes:[.font:NSFont.monospacedSystemFont(ofSize:size,weight:.regular),.foregroundColor:color,.paragraphStyle:paragraph])}
  override func draw(_ dirty:NSRect){guard let pattern else{return};Theme.bg.setFill();bounds.fill();let header=CGFloat(pattern.headerHeight),height=CGFloat(pattern.rowHeight);Theme.panel.setFill();NSRect(x:0,y:0,width:bounds.width,height:header).fill()
    for i in firstLane..<min(lanes.count,firstLane+Int(ceil(bounds.width/124))){let lane=lanes[i],x=CGFloat(i-firstLane)*124;Theme.border.setFill();NSRect(x:x,y:0,width:1,height:bounds.height).fill();text(lane.name,x:x+7,y:5,color:Theme.gold,size:10);text("GRAPH \(lane.column+1)",x:x+7,y:header-18,color:Theme.muted,size:10)
      for visible in 0..<Int(ceil(max(0,bounds.height-header)/height)){let row=pattern.firstRow+visible;guard row<pattern.model.rows else{break};let y=header+CGFloat(visible)*height
        if row==pattern.playRow && pattern.playPattern==pattern.model.pattern{Theme.accent.withAlphaComponent(0.16).setFill();NSRect(x:x+1,y:y,width:123,height:height).fill()}
        if row==pattern.cursorRow && i==selected{Theme.gold.withAlphaComponent(window?.firstResponder===self ? 0.28 : 0.1).setFill();NSRect(x:x+1,y:y,width:123,height:height).fill()}
        let command=commands[Self.key(row,lane.target,lane.column)];text(command?.display ?? "···",x:x+7,y:y+max(0,(height-16)/2),color:command==nil ? Theme.border : Theme.gold)
      }
    }
  }
  private func move(_ row:Int){guard let pattern else{return};pattern.cursorRow=max(0,min(pattern.model.rows-1,row));pattern.isFollowing=false;pattern.revealCursor();pattern.onCursor?();refresh()}
  override func mouseDown(with event:NSEvent){guard let pattern,!lanes.isEmpty else{return};window?.makeFirstResponder(self);let p=convert(event.locationInWindow,from:nil);selected=min(lanes.count-1,max(0,firstLane+Int(p.x/124)));if p.y>=CGFloat(pattern.headerHeight){move(pattern.firstRow+Int((p.y-CGFloat(pattern.headerHeight))/CGFloat(pattern.rowHeight)))};if event.clickCount>1{edit()};refresh()}
  func edit(){guard let pattern,lanes.indices.contains(selected)else{return};let lane=lanes[selected];onEdit?(lane.target,lane.column,pattern.cursorRow)}
  override func keyDown(with event:NSEvent){guard let pattern,!lanes.isEmpty else{return};switch event.keyCode{case 36:edit();case 125:move(pattern.cursorRow+1);case 126:move(pattern.cursorRow-1);case 123:selected=max(0,selected-1);case 124:selected=min(lanes.count-1,selected+1);case 51,117:if lanes.indices.contains(selected){let lane=lanes[selected];onClear?(lane.target,lane.column,pattern.cursorRow)};default:super.keyDown(with:event);return};let visible=max(1,Int(bounds.width/124));if selected<firstLane{firstLane=selected};if selected>=firstLane+visible{firstLane=selected-visible+1};refresh()}
  override func scrollWheel(with event:NSEvent){if abs(event.scrollingDeltaX)>abs(event.scrollingDeltaY){firstLane=max(0,min(lanes.count-1,firstLane+Int(event.scrollingDeltaX/8)));refresh()}else{pattern?.scrollWheel(with:event);refresh()}}
}
final class PatternGraphHost:NSView {
  let pattern:PatternView,lanes=GraphLaneStrip(frame:.zero)
  private var visibleCount=0
  init(_ pattern:PatternView){self.pattern=pattern;super.init(frame:.zero);lanes.pattern=pattern;addSubview(pattern);addSubview(lanes)}
  required init?(coder:NSCoder){fatalError()}
  override var isFlipped:Bool{true}
  override func layout(){super.layout();let width=min(bounds.width*0.4,CGFloat(visibleCount)*124);pattern.frame=NSRect(x:0,y:0,width:bounds.width-width,height:bounds.height);lanes.frame=NSRect(x:bounds.width-width,y:0,width:width,height:bounds.height);lanes.isHidden=visibleCount==0}
  func refresh(){if visibleCount != pattern.model.graphLanes.count{visibleCount=pattern.model.graphLanes.count;needsLayout=true};lanes.refresh()}
}
