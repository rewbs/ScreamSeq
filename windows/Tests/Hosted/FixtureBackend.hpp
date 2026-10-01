#pragma once
#include <cstdint>
#include <span>
struct FixtureParameterCall { uint32_t id; double value; uint32_t offset; };
std::span<const FixtureParameterCall> fixtureParameterCalls();
// Test-only DSP fixtures, NOT a Windows VST3 implementation or loader test.
void fixtureEffectDelay(bool);
void fixtureObserve(bool);
uint64_t fixtureObservedFrames();
uint64_t fixtureClockErrors();
uint64_t fixtureProcessCalls();
uint32_t fixtureLargestBlock();
uint64_t fixtureCreated();
double fixtureMIDIBeat();
