#pragma once
#include "SampleCapture.hpp"
namespace ScreamSeq {
// No loopback, monitoring, endpoint-default mutation or capture on enumeration.
std::vector<CaptureDevice> captureDevices();
std::unique_ptr<SampleCapture> makeWasapiCapture();
}
