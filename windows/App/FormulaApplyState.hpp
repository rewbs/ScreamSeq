#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace ScreamSeq {
// Applying a point can pump native input while its document write completes.
// Retention belongs to the submitted text, never to a later editor generation.
struct FormulaApplyState {
  enum class Result { Busy, Rejected, Accepted, NewerDraft };
  std::wstring baseline;
  bool accepted=false;
private:
  bool applying_=false;
public:
  bool pending()const{return applying_;}
  void changed(){accepted=false;}
  bool retained(std::wstring_view text)const{return applying_||(!accepted&&text!=baseline);}
  template<class Apply,class Generation,class Text>
  Result apply(uint64_t submitted,std::wstring text,Apply use,Generation generation,Text source){
    if(applying_)return Result::Busy;
    applying_=true;struct Reset {bool &value;~Reset(){value=false;}} reset{applying_};
    if(!use())return Result::Rejected;
    baseline=text;accepted=submitted==generation()&&text==source();
    return accepted?Result::Accepted:Result::NewerDraft;
  }
};
}
