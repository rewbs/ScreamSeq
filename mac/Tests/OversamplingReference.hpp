#pragma once
#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>
namespace OversamplingReference {
// Independent full-rate convolution reference. The window's Bessel function is
// evaluated by angular integration, rather than the generator's power series.
static std::vector<double> coefficients(size_t length, double beta) {
  auto bessel = [](double x) {
    double sum = 0;
    for (int i = 0; i < 512; ++i) sum += std::exp(x * std::cos(std::numbers::pi * (i + .5) / 512));
    return sum / 512;
  };
  std::vector<double> h(length); double total = 0; const auto center = double(length / 2);
  for (size_t i = 1; i < length; i += 2) {
    const double n = double(i) - center;
    h[i] = std::sin(std::numbers::pi * n / 2) / (std::numbers::pi * n) *
      bessel(beta * std::sqrt(1 - n * n / (center * center))) / bessel(beta);
    total += h[i];
  }
  for (size_t i = 1; i < length; i += 2) h[i] *= .5 / total;
  h[length / 2] = .5; return h;
}
static std::vector<double> convolution(const std::vector<double> &x, const std::vector<double> &h) {
  std::vector<double> y(x.size());
  for (size_t i = 0; i < x.size(); ++i) for (size_t k = 0; k < h.size() && k <= i; ++k) y[i] += h[k] * x[i - k];
  return y;
}
static std::vector<double> reference(std::vector<double> input, int levels, bool nonlinear) {
  std::vector<std::vector<double>> filters;
  for (int level = 0; level < levels; ++level) {
    filters.push_back(coefficients(level == 0 ? 145 : level == 1 ? 49 : 33, level == 0 ? 11 : 13));
    std::vector<double> up(input.size() * 2);
    for (size_t i = 0; i < input.size(); ++i) up[i * 2] = input[i] * 2;
    input = convolution(up, filters.back());
  }
  if (nonlinear) for (auto &x : input) x = std::tanh(x * 4);
  for (int level = levels - 1; level >= 0; --level) {
    auto down = convolution(input, filters[level]); input.resize(down.size() / 2);
    for (size_t i = 0; i < input.size(); ++i) input[i] = down[i * 2];
  }
  return input;
}
}
