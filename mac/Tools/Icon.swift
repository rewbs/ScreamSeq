import AppKit
// Package the shared icon artwork at native sizes, preserving its alpha.
let output=CommandLine.arguments[1]
let source=CommandLine.arguments.count>2 ? CommandLine.arguments[2] : "assets/branding/ScreamSeq.png"
guard let artwork=NSImage(contentsOfFile:source) else {fatalError("Missing icon artwork: \(source)")}
func png(_ size:Int)->Data{
  let bitmap=NSBitmapImageRep(bitmapDataPlanes:nil,pixelsWide:size,pixelsHigh:size,bitsPerSample:8,samplesPerPixel:4,hasAlpha:true,isPlanar:false,colorSpaceName:.deviceRGB,bytesPerRow:0,bitsPerPixel:0)!
  let context=NSGraphicsContext(bitmapImageRep:bitmap)!
  NSGraphicsContext.saveGraphicsState();NSGraphicsContext.current=context
  context.imageInterpolation = .high
  artwork.draw(in:NSRect(x:0,y:0,width:size,height:size),from:.zero,operation:.copy,fraction:1,respectFlipped:false,hints:nil)
  NSGraphicsContext.restoreGraphicsState();return bitmap.representation(using:.png,properties:[:])!
}
var chunks=Data()
func u32(_ value:UInt32)->Data{var value=value.bigEndian;return withUnsafeBytes(of:&value){Data($0)}}
for (type,size) in [("icp4",16),("icp5",32),("icp6",64),("ic07",128),("ic08",256),("ic09",512),("ic10",1024)] {let image=png(size);chunks.append(type.data(using:.ascii)!);chunks.append(u32(UInt32(image.count+8)));chunks.append(image)}
var file=Data("icns".utf8);file.append(u32(UInt32(chunks.count+8)));file.append(chunks);try file.write(to:URL(fileURLWithPath:output))
