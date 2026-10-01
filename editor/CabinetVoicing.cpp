#include "CabinetVoicing.hpp"
#include <algorithm>
#include <cmath>

namespace Tracker {
namespace {
// Authored voicings: low-cut, low shelf dB at 140 Hz, three resonant bands,
// fourth-order high-cut, reflection times and signed reflection levels.
double checkedRate(double rate) {
  if (!std::isfinite(rate) || rate < 8000 || rate > 384000) throw std::invalid_argument("Unsupported cabinet sample rate");
  return rate;
}
constexpr CabinetModel models[]{
  {"Open 1x8", 110,-3, 380,3,1.1, 1400,4,2.1, 3400,-3,1.2,6200, {.43,.91,1.47},{.10,-.07,.04}},
  {"Open 1x10",85,-1, 290,3,1.0, 1250,3,1.8, 3100,-2,1.0,5800, {.52,1.13,1.86},{.12,-.08,.04}},
  {"Open 1x12",72,0, 220,2,1.0, 1050,3,1.5, 2800,2,1.1,5100, {.64,1.37,2.14},{.13,-.08,.05}},
  {"Open 2x12",62,1, 190,2,1.2, 950,2,1.7, 2600,3,1.2,5000, {.78,1.59,2.61},{.14,-.10,.05}},
  {"Closed 1x12",78,2, 180,4,1.4, 1200,-2,1.4, 3000,3,1.4,4700, {.59,1.21,1.97},{.16,-.12,.07}},
  {"Closed 2x12",65,3, 160,4,1.5, 1000,-3,1.3, 2700,4,1.4,4500, {.73,1.53,2.47},{.18,-.13,.07}},
  {"Closed 4x12",58,4, 140,5,1.7, 850,-4,1.2, 2500,4,1.5,4300, {.92,1.91,3.13},{.19,-.14,.08}},
  {"Bright 4x12",60,2, 150,3,1.5, 1150,-2,1.2, 3500,5,1.4,6100, {.86,1.77,2.91},{.16,-.12,.08}},
  {"Dark 4x12",55,4, 130,5,1.8, 800,-2,1.5, 2200,-3,1.2,3400, {1.01,2.07,3.39},{.20,-.15,.08}},
  {"Vintage 1x12",88,1, 270,3,1.2, 1450,5,2.0, 3300,-4,1.7,4800, {.61,1.29,2.09},{.15,-.11,.06}},
  {"Vintage 2x12",70,2, 210,3,1.2, 1300,4,1.9, 2900,-3,1.5,4700, {.81,1.69,2.73},{.17,-.12,.07}},
  {"Bass 1x15",32,3, 95,4,1.3, 650,-2,1.1, 1900,2,1.1,3200, {1.17,2.43,3.91},{.16,-.09,.05}},
  {"Bass 2x10",38,1, 130,3,1.4, 850,2,1.2, 2700,3,1.0,5600, {.83,1.73,2.83},{.14,-.10,.06}},
  {"Bass 4x10",34,2, 110,4,1.5, 750,1,1.3, 2400,2,1.1,5100, {1.03,2.17,3.49},{.17,-.11,.07}},
  {"Bass 8x10",30,3, 90,5,1.6, 600,-2,1.2, 2100,3,1.2,4600, {1.29,2.69,4.31},{.19,-.13,.08}},
  {"Small radio",260,-4, 620,4,1.3, 1600,6,2.8, 3400,-5,1.5,3600, {.31,.67,1.09},{.12,-.10,.06}},
  {"Lo-fi box",180,-2, 450,5,1.6, 1800,-5,2.4, 2900,3,1.7,2900, {.39,.83,1.33},{.22,-.17,.10}},
  {"Wide-range",24,0, 160,0,.70710678, 1200,0,.70710678, 3800,0,.70710678,11000, {.47,1.03,1.71},{.04,-.03,.02}},
};
}
std::span<const CabinetModel> cabinetModels() noexcept { return models; }
CabinetVoicing::CabinetVoicing(double rate) : rate_(checkedRate(rate)), history_(size_t(std::ceil(rate_ * .005)) + 2) { model(2); }
void CabinetVoicing::model(uint32_t index, uint32_t frames) noexcept {
  const auto &m = models[std::min<size_t>(index, std::size(models) - 1)];
  filters_[0].configure(FilterShape::HighPass,m.lowCut,.7071067811865476,0,rate_,frames);
  filters_[1].configure(FilterShape::LowShelf,140,.7071067811865476,m.lowShelf,rate_,frames);
  filters_[2].configure(FilterShape::Bell,m.bodyFrequency,m.bodyQ,m.bodyGain,rate_,frames);
  filters_[3].configure(FilterShape::Bell,m.coneFrequency,m.coneQ,m.coneGain,rate_,frames);
  filters_[4].configure(FilterShape::Bell,m.biteFrequency,m.biteQ,m.biteGain,rate_,frames);
  filters_[5].configure(FilterShape::LowPass,m.highCut,.541196100146197,0,rate_,frames);
  filters_[6].configure(FilterShape::LowPass,m.highCut,1.30656296487638,0,rate_,frames);
  for (size_t i = 0; i < 3; ++i) { delays_[i].set(std::max(1.,m.reflectionMS[i]*rate_/1000),frames); gains_[i].set(m.reflectionGain[i],frames); }
}
CabinetVoicing::Frame CabinetVoicing::process(Frame input) noexcept {
  for (auto &filter : filters_) filter.process(input[0],input[1]);
  Frame result = input; double normalization = 1;
  for (size_t tap = 0; tap < 3; ++tap) {
    const double delay = delays_[tap].next(), gain = gains_[tap].next();
    const auto whole = size_t(delay); const double fraction = delay-whole;
    const auto &a = history_[(cursor_+history_.size()-whole)%history_.size()];
    const auto &b = history_[(cursor_+history_.size()-whole-1)%history_.size()];
    for (size_t c = 0; c < 2; ++c) result[c] += gain*(a[c]+fraction*(b[c]-a[c]));
    normalization += std::abs(gain);
  }
  history_[cursor_] = input; if (++cursor_ == history_.size()) cursor_ = 0;
  for (auto &x : result) x /= normalization;
  return result;
}
double CabinetVoicing::decaySeconds(double rate) noexcept {
  // Parameter boxes include every model, including intermediate coefficients.
  return StateVariableFilter::decaySecondsRange(1u<<unsigned(FilterShape::HighPass),24,260,.7071067811865476,.7071067811865476,0,0,rate)
    + StateVariableFilter::decaySecondsRange(1u<<unsigned(FilterShape::LowShelf),140,140,.7071067811865476,.7071067811865476,-4,4,rate)
    + StateVariableFilter::decaySecondsRange(1u<<unsigned(FilterShape::Bell),90,620,.70710678,1.8,0,5,rate)
    + StateVariableFilter::decaySecondsRange(1u<<unsigned(FilterShape::Bell),600,1800,.70710678,2.8,-5,6,rate)
    + StateVariableFilter::decaySecondsRange(1u<<unsigned(FilterShape::Bell),1900,3800,.70710678,1.7,-5,5,rate)
    + StateVariableFilter::decaySecondsRange(1u<<unsigned(FilterShape::LowPass),2900,11000,.541196100146197,1.30656296487638,0,0,rate)*2 + .01;
}
}
