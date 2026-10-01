#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace Tracker {
// A small, immutable expression program. Compilation is control-thread only;
// evaluation has bounded work, fixed stack storage, and no external effects.
class CurveFormula {
  enum Op : uint8_t { Constant, Variable, Negate, Not, Add, Subtract, Multiply, Divide, Modulo, Power,
    Less, LessEqual, Greater, GreaterEqual, Equal, NotEqual, And, Or, Select,
    Sin, Cos, Tan, Abs, Sqrt, Exp, Log, Log2, Log10, Floor, Ceil, Round, Tanh,
    Min, Max, Clamp, Mix, Smooth, Fract, Triangle, Saw, Square, Noise };
  struct Instruction { Op op; double number = 0; };
  std::string source_;
  std::vector<Instruction> code_;
  class Parser {
    CurveFormula &program; size_t at = 0; unsigned depth = 0;
    [[noreturn]] void fail(const std::string &why) const { throw std::invalid_argument("Formula at character " + std::to_string(at + 1) + ": " + why); }
    void space() { while(at < program.source_.size() && std::isspace(static_cast<unsigned char>(program.source_[at]))) ++at; }
    bool take(std::string_view token) { space(); if(program.source_.compare(at, token.size(), token) != 0) return false; at += token.size(); return true; }
    void emit(Op op, double value = 0) { if(program.code_.size() >= 128) fail("use at most 128 operations"); program.code_.push_back({op,value}); }
    std::string identifier() { space(); const auto start = at; while(at < program.source_.size() && (std::isalnum(static_cast<unsigned char>(program.source_[at])) || program.source_[at] == '_')) ++at; return program.source_.substr(start, at-start); }
    void expression(int minimum = 0) {
      if(++depth > 32) fail("expression nesting exceeds 32");
      if(take("-")) { expression(7); emit(Negate); }
      else if(take("+")) expression(7);
      else if(take("!")) { expression(7); emit(Not); }
      else if(take("(")) { expression(); if(!take(")")) fail("expected )"); }
      else {
        space(); const auto c = at < program.source_.size() ? program.source_[at] : '\0';
        if(std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
          char *end = nullptr; const char *begin = program.source_.c_str()+at; double value = std::strtod(begin,&end);
          if(end == begin || !std::isfinite(value)) fail("expected a finite number"); at += size_t(end-begin); emit(Constant,value);
        } else {
          const auto name = identifier(); if(name.empty()) fail("expected a value");
          if(take("(")) {
            struct Function { std::string_view name; Op op; unsigned args; };
            static constexpr Function functions[] = {{"sin",Sin,1},{"cos",Cos,1},{"tan",Tan,1},{"abs",Abs,1},{"sqrt",Sqrt,1},{"exp",Exp,1},{"log",Log,1},{"log2",Log2,1},{"log10",Log10,1},{"floor",Floor,1},{"ceil",Ceil,1},{"round",Round,1},{"tanh",Tanh,1},{"min",Min,2},{"max",Max,2},{"pow",Power,2},{"clamp",Clamp,3},{"mix",Mix,3},{"lerp",Mix,3},{"smoothstep",Smooth,1},{"fract",Fract,1},{"tri",Triangle,1},{"saw",Saw,1},{"square",Square,1},{"noise",Noise,2},{"if",Select,3}};
            const auto found = std::find_if(std::begin(functions),std::end(functions),[&](auto f){return f.name==name;});
            if(found == std::end(functions)) fail("unknown function '"+name+"'");
            for(unsigned i=0;i<found->args;++i) { if(i && !take(",")) fail("expected ,"); expression(); }
            if(!take(")")) fail("wrong argument count or missing )"); emit(found->op);
          } else {
            static constexpr std::string_view names[] = {"start","end","t","row","beat","beats","duration","startBeat","endBeat","L"};
            auto found=std::find(std::begin(names),std::end(names),name);
            if(found!=std::end(names)) emit(Variable,double(found-std::begin(names)));
            else if(name=="beatOffset" || name=="offset") emit(Variable,2);
            else if(name=="S" || name=="startValue" || name=="active_keyframe_value") emit(Variable,0);
            else if(name=="endValue" || name=="next_keyframe_value") emit(Variable,1);
            else if(name=="pi" || name=="PI") emit(Constant,std::numbers::pi);
            else if(name=="tau") emit(Constant,2*std::numbers::pi);
            else if(name=="e" || name=="E") emit(Constant,std::numbers::e);
            else fail("unknown variable '"+name+"'");
          }
        }
      }
      struct Operator { std::string_view token; Op op; int precedence; };
      static constexpr Operator operators[] = {{"||",Or,1},{"&&",And,2},{"==",Equal,3},{"!=",NotEqual,3},{"<=",LessEqual,4},{">=",GreaterEqual,4},{"<",Less,4},{">",Greater,4},{"+",Add,5},{"-",Subtract,5},{"**",Power,7},{"*",Multiply,6},{"/",Divide,6},{"%",Modulo,6},{"^",Power,7}};
      for(;;) {
        space(); const Operator *found=nullptr;
        for(const auto &candidate:operators) if(program.source_.compare(at,candidate.token.size(),candidate.token)==0) {found=&candidate;break;}
        if(!found || found->precedence < minimum) break;
        at += found->token.size(); expression(found->precedence+(found->op==Power?0:1)); emit(found->op);
      }
      if(minimum==0 && take("?")) { expression(); if(!take(":")) fail("expected :"); expression(); emit(Select); }
      --depth;
    }
  public:
    explicit Parser(CurveFormula &p):program(p) {}
    void run() { expression(); space(); if(at!=program.source_.size()) fail("unexpected text"); }
  };
public:
  struct Context { double start=0,end=0,t=0,row=0,beat=0,beats=0,duration=0,startBeat=0,endBeat=0; };
  CurveFormula() = default;
  explicit CurveFormula(std::string source):source_(std::move(source)) {
    if(source_.empty() || source_.size()>2048 || source_.find('\0')!=std::string::npos) throw std::invalid_argument("Formula requires 1..2048 characters");
    Parser(*this).run();
  }
  const std::string &source() const { return source_; }
  size_t operations() const { return code_.size(); }
  size_t bytes() const { return source_.size()+code_.size()*sizeof(Instruction); }
  bool operator==(const CurveFormula &other) const { return source_==other.source_; }
  double evaluate(const Context &c) const noexcept {
    const double linear=c.start+(c.end-c.start)*c.t;
    if(code_.empty()) return linear;
    const double variables[]={c.start,c.end,c.t,c.row,c.beat,c.beats,c.duration,c.startBeat,c.endBeat,linear};
    std::array<double,128> stack{}; size_t size=0;
    for(const auto &i:code_) {
      if(i.op==Constant || i.op==Variable) { stack[size++]=i.op==Constant?i.number:variables[size_t(i.number)]; continue; }
      auto &a=stack[size-1];
      switch(i.op) {
      case Negate:a=-a;break; case Not:a=!a;break;
      case Sin:a=std::sin(a);break;case Cos:a=std::cos(a);break;case Tan:a=std::tan(a);break;
      case Abs:a=std::abs(a);break;case Sqrt:a=std::sqrt(a);break;case Exp:a=std::exp(a);break;
      case Log:a=std::log(a);break;case Log2:a=std::log2(a);break;case Log10:a=std::log10(a);break;
      case Floor:a=std::floor(a);break;case Ceil:a=std::ceil(a);break;case Round:a=std::round(a);break;case Tanh:a=std::tanh(a);break;
      case Smooth:a=std::clamp(a,0.0,1.0);a=a*a*(3-2*a);break;
      case Fract:a-=std::floor(a);break;case Saw:a=2*(a-std::floor(a))-1;break;
      case Triangle:a=1-4*std::abs(a-std::floor(a)-0.5);break;case Square:a=a-std::floor(a)<0.5?1:-1;break;
      case Clamp:case Mix:case Select: {
        const auto z=stack[--size],y=stack[--size]; auto &x=stack[size-1];
        if(i.op==Clamp) x=std::max(std::min(y,z),std::min(std::max(y,z),x));
        else if(i.op==Mix) x=x+(y-x)*z; else x=x?y:z; break;
      }
      default: {
        const auto y=stack[--size];auto &x=stack[size-1];
        switch(i.op) {
        case Add:x+=y;break;case Subtract:x-=y;break;case Multiply:x*=y;break;case Divide:x/=y;break;case Modulo:x=std::fmod(x,y);break;case Power:x=std::pow(x,y);break;
        case Less:x=x<y;break;case LessEqual:x=x<=y;break;case Greater:x=x>y;break;case GreaterEqual:x=x>=y;break;case Equal:x=x==y;break;case NotEqual:x=x!=y;break;
        case And:x=x&&y;break;case Or:x=x||y;break;case Min:x=std::min(x,y);break;case Max:x=std::max(x,y);break;
        case Noise: { // Smooth, reproducible value noise; no mutable RNG state.
          const auto hash=[&](double n){const auto v=std::sin(n*127.1+y*311.7)*43758.5453123;return v-std::floor(v);};
          const double base=std::floor(x),t=x-base,w=t*t*(3-2*t);x=hash(base)*(1-w)+hash(base+1)*w;break;
        }
        default:break;
        } break;
      }
      }
    }
    // Domain errors and overflow cannot inject NaN/Inf into a plugin. They fall
    // back to the segment's linear interpolation; valid results are normalized.
    return std::clamp(std::isfinite(stack[0])?stack[0]:linear,0.0,1.0);
  }
};
}
