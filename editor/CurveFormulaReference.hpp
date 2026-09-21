#pragma once
#include <array>
#include <string_view>
namespace Tracker {
struct CurveFormulaSymbol { std::string_view name, insert, category, description; };
inline constexpr auto curveFormulaSymbols = std::to_array<CurveFormulaSymbol>({
  {"start","start","Value","This node's normalized value (0–1)."},
  {"end","end","Value","Next node's value; equals start on the final segment."},
  {"t","t","Time","Progress through the segment, from 0 to 1."},
  {"row","row","Time","Current fractional pattern row."},
  {"beat","beat","Time","Current beat position within the pattern."},
  {"beats","beats","Time","Beats elapsed since this segment started."},
  {"duration","duration","Time","Segment duration in beats."},
  {"startBeat","startBeat","Time","Segment start in pattern beats."},
  {"endBeat","endBeat","Time","Segment end in pattern beats."},
  {"L","L","Value","Linear interpolation between start and end at the current time."},
  {"beatOffset","beatOffset","Alias","Alias of t: normalized progress, not elapsed beats."},
  {"offset","offset","Alias","Alias of t."},
  {"S","S","Alias","Alias of start."},
  {"startValue","startValue","Alias","Alias of start."},
  {"endValue","endValue","Alias","Alias of end."},
  {"active_keyframe_value","active_keyframe_value","Alias","Alias of start."},
  {"next_keyframe_value","next_keyframe_value","Alias","Alias of end."},
  {"pi","pi","Constant","π (3.14159…). Trigonometric functions use radians."},
  {"PI","PI","Alias","Alias of pi."},
  {"tau","tau","Constant","2 × pi: one complete cycle in radians."},
  {"e","e","Constant","Euler's number (2.71828…)."},
  {"E","E","Alias","Alias of e."},
  {"sin","sin(tau*beats)","Function","sin(x): sine, x in radians."},
  {"cos","cos(tau*beats)","Function","cos(x): cosine, x in radians."},
  {"tan","tan(t)","Function","tan(x): tangent, x in radians."},
  {"abs","abs(L)","Function","abs(x): absolute value."},
  {"sqrt","sqrt(t)","Function","sqrt(x): square root."},
  {"exp","exp(t)","Function","exp(x): e raised to x."},
  {"log","log(1+t)","Function","log(x): natural logarithm."},
  {"log2","log2(1+t)","Function","log2(x): base-2 logarithm."},
  {"log10","log10(1+t)","Function","log10(x): base-10 logarithm."},
  {"floor","floor(t)","Function","floor(x): round down."},
  {"ceil","ceil(t)","Function","ceil(x): round up."},
  {"round","round(t)","Function","round(x): nearest integer."},
  {"tanh","tanh(t)","Function","tanh(x): hyperbolic tangent."},
  {"min","min(start,end)","Function","min(a,b): smaller value."},
  {"max","max(start,end)","Function","max(a,b): larger value."},
  {"pow","pow(t,2)","Function","pow(x,power): exponentiation; also x^power or x**power."},
  {"clamp","clamp(L,0,1)","Function","clamp(value,low,high): limit a value to a range."},
  {"mix","mix(start,end,t)","Function","mix(a,b,weight): interpolate between a and b."},
  {"lerp","lerp(start,end,t)","Function","Alias of mix(a,b,weight)."},
  {"smoothstep","smoothstep(t)","Function","Smooth cubic transition. Input is clamped to 0–1."},
  {"fract","fract(beats)","Function","fract(x): fractional part x − floor(x)."},
  {"tri","tri(beats)","Function","Triangle oscillator, phase in cycles, output −1…1."},
  {"saw","saw(beats)","Function","Saw oscillator, phase in cycles, output −1…1."},
  {"square","square(beats)","Function","Square oscillator, phase in cycles, output −1 or 1."},
  {"noise","noise(beats,17)","Function","noise(position,seed): deterministic smooth noise, output 0–1."},
  {"if","if(t<0.5,start,end)","Function","if(condition,yes,no): choose a result; also condition ? yes : no."}
});
inline constexpr std::string_view curveFormulaNotes =
  "Formulas return a normalized value (0–1), not an interpolation weight.\n"
  "Operators: + - * / % ^ **; < <= > >= == !=; && || !; condition ? yes : no.\n"
  "Whitespace and line breaks are allowed. Comments, assignments and arbitrary code are not.\n"
  "The final node's scripted segment continues to the envelope end.\n"
  "Results are clamped to 0–1. A non-finite result falls back to linear interpolation.\n"
  "Limits: 2,048 characters, 128 operations, 32 nesting levels.\n"
  "Playback evaluates on a bounded 32-sample clock with host ramps between values.";
}
