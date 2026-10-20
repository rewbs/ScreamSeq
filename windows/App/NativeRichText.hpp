#pragma once
#include "NativeControls.hpp"
#include <richedit.h>

namespace ScreamSeq::NativeRichText {
// A plain-text RichEdit has one character format. Change that default without
// selecting/replacing its text or clearing the musician's local Undo history.
// Rich-text documents need a different policy; do not flatten their formatting.
inline bool applyPlainTextColors(HWND control,bool contrast=NativeControls::highContrast()) {
  if(!(SendMessageW(control,EM_GETTEXTMODE,0,0)&TM_PLAINTEXT))return false;
  CHARFORMAT2W format{};format.cbSize=sizeof(format);format.dwMask=CFM_COLOR;
  format.crTextColor=contrast?GetSysColor(COLOR_WINDOWTEXT):RGB(218,232,241);
  if(!SendMessageW(control,EM_SETCHARFORMAT,SCF_DEFAULT,reinterpret_cast<LPARAM>(&format)))return false;
  SendMessageW(control,EM_SETBKGNDCOLOR,0,contrast?GetSysColor(COLOR_WINDOW):RGB(16,23,31));
  return true;
}
}
