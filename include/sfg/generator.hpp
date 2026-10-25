// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <deko3d.h>
#include <memory>
#include <string>

namespace sfg {
// Caller owns device/queue and MUST drain GPU work before destruction or recreation.
// Cache-only target runtime: no DLL, SPIRV-Cross or UAM is needed on the console.
class Generator {
public:
  static std::unique_ptr<Generator> create(DkDevice device, unsigned width,
      unsigned height, const char* cache_directory, std::string& error);
  ~Generator();
  // Fresh target for the next REAL scene. Contents are owned by this instance.
  const DkImage* input() const;
  // Records only; caller submits. Returns current original and, after warmup,
  // the frame between the previous original and this original.
  bool record(DkCmdBuf cmd, const DkImage*& generated,
      const DkImage*& original, std::string& error, float alpha=0.5f);
  void reset();
private:
  struct State;
  explicit Generator(std::unique_ptr<State> state);
  std::unique_ptr<State> state_;
};
}
