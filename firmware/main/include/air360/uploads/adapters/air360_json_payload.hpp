#pragma once

#include <string>

#include "air360/uploads/measurement_batch.hpp"

namespace air360 {

bool validateAir360JsonBatch(const MeasurementBatch& batch, std::string& error);
std::string buildAir360JsonBody(const MeasurementBatch& batch);

}  // namespace air360
