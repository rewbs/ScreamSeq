#pragma once
#include "../../editor/IntegerValidation.hpp"
#include <nlohmann/json.hpp>

namespace ScreamSeq::Api {
inline std::optional<uint64_t> boundedInteger(const nlohmann::json &value,uint64_t low,uint64_t high) {
  if(value.is_number_unsigned())return Tracker::boundedUnsignedInteger(value.get<uint64_t>(),low,high);
  if(value.is_number_integer())return Tracker::boundedUnsignedInteger(value.get<int64_t>(),low,high);
  if(value.is_number_float())return Tracker::boundedUnsignedInteger(value.get<double>(),low,high);
  return {};
}
}
