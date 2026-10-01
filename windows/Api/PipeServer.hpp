#pragma once
#include <nlohmann/json.hpp>
#include <functional>
#include <memory>
#include <string>

namespace ScreamSeq::Api {
// Transport only. Handler runs on ONE background I/O thread, never the audio
// thread. It must marshal to the document/control owner, or read immutable data.
// Handler must return in bounded time. Cancel its pending dispatches BEFORE stop
// when shutting down from the control thread; otherwise a synchronous GUI wait
// and the join would deadlock. Lifecycle methods belong to one owner thread.
class PipeServer {
public:
  using Handler = std::function<nlohmann::json(const nlohmann::json &)>;
  static constexpr std::size_t maxRequestBytes = 32 * 1024 * 1024;
  static constexpr std::size_t maxResponseBytes = 32 * 1024 * 1024;
  explicit PipeServer(std::wstring name, Handler handler, unsigned ioTimeoutMs = 5000);
  ~PipeServer();
  PipeServer(const PipeServer &) = delete;
  PipeServer &operator=(const PipeServer &) = delete;
  void start(); // throws on invalid name/security/first-instance collision
  void stop() noexcept; // cancels pending connect/read/write; idempotent
  const std::wstring &name() const noexcept;
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
