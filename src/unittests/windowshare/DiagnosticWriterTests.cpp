/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "DiagnosticOutput.h"
#include "DiagnosticWriter.h"
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace deskflow::windowshare;
using namespace std::chrono_literals;
namespace {
void check(bool condition, const char *message)
{
  if (!condition)
    throw std::runtime_error(message);
}
std::string loss(std::uint64_t dropped, std::uint64_t oversized)
{
  return "loss:" + std::to_string(dropped) + ":" + std::to_string(oversized);
}
struct Gate
{
  std::mutex mutex;
  std::condition_variable changed;
  bool entered = false, released = false, destroyed = false;
  std::vector<std::string> lines;
  bool write(std::string_view line)
  {
    std::unique_lock lock(mutex);
    entered = true;
    changed.notify_all();
    if (!changed.wait_for(lock, 5s, [&] { return released; }))
      return false;
    lines.emplace_back(line);
    return true;
  }
  void wait()
  {
    std::unique_lock lock(mutex);
    check(changed.wait_for(lock, 5s, [&] { return entered; }), "worker entered sink");
  }
  void release()
  {
    std::lock_guard lock(mutex);
    released = true;
    changed.notify_all();
  }
};
void boundedRecords()
{
  const auto gate = std::make_shared<Gate>();
  DiagnosticWriter writer([gate](std::string_view line) { return gate->write(line); }, loss);
  check(!writer.append(""), "empty records are not admitted");
  check(writer.append("first"), "first admitted");
  gate->wait();
  for (std::size_t i = 1; i < DiagnosticWriter::maximumRecords; ++i)
    check(writer.append(std::to_string(i)), "producer progresses while sink blocked");
  check(!writer.append("overflow"), "record bound includes in-flight line");
  check(!writer.append(std::string(DiagnosticWriter::maximumLine + 1, 'x')), "oversized record refused");
  const auto status = writer.status();
  check(status.records == DiagnosticWriter::maximumRecords, "bounded retained count");
  check(status.dropped == 1 && status.oversized == 1, "explicit loss counters");
  gate->release();
  check(writer.finish(5s), "accepted records drain");
  check(gate->lines.size() == DiagnosticWriter::maximumRecords + 1, "one loss notice and accepted lines");
  check(gate->lines[0] == "first" && gate->lines[1] == "loss:1:1", "loss visible once sink recovers");
  for (std::size_t i = 1; i < DiagnosticWriter::maximumRecords; ++i)
    check(gate->lines[i + 1] == std::to_string(i), "accepted order preserved");
  check(writer.status().bytes == 0 && writer.status().records == 0, "in-flight accounting retires");
  check(!writer.append("after stop"), "stop refuses further work");
}
void boundedBytes()
{
  const auto gate = std::make_shared<Gate>();
  DiagnosticWriter writer([gate](std::string_view line) { return gate->write(line); }, loss);
  const std::string line(DiagnosticWriter::maximumLine, 'x');
  check(writer.append(line), "maximum line admitted");
  gate->wait();
  const auto count = DiagnosticWriter::maximumBytes / line.size();
  for (std::size_t i = 1; i < count; ++i)
    check(writer.append(line), "byte budget available");
  check(!writer.append("x"), "byte budget includes blocked sink record");
  check(writer.status().bytes == DiagnosticWriter::maximumBytes, "byte limit retained exactly");
  gate->release();
  check(writer.finish(5s) && !writer.status().failed, "byte-bound queue drains");
  check(gate->lines.size() == count + 1, "byte overflow reported");
}
void boundedShutdown()
{
  const auto gate = std::make_shared<Gate>();
  struct Lifetime
  {
    std::shared_ptr<Gate> gate;
    ~Lifetime()
    {
      std::lock_guard lock(gate->mutex);
      gate->destroyed = true;
      gate->changed.notify_all();
    }
  };
  auto lifetime = std::make_shared<Lifetime>();
  lifetime->gate = gate;
  const std::weak_ptr<Lifetime> weak = lifetime;
  {
    DiagnosticWriter writer(
        [owned = std::move(lifetime)](std::string_view line) { return owned->gate->write(line); }, loss
    );
    check(writer.append("blocked"), "shutdown fixture admitted");
    gate->wait();
    check(!writer.finish(0ms), "shutdown returns with a blocked sink");
    check(!writer.append("late"), "detached writer rejects new input");
  }
  check(!weak.expired(), "worker owns its sink after writer destruction");
  gate->release();
  std::unique_lock lock(gate->mutex);
  check(gate->changed.wait_for(lock, 5s, [&] { return gate->destroyed; }), "detached worker releases its sink");
  check(gate->lines == std::vector<std::string>{"blocked"}, "accepted line survives owner destruction");
}
void outputFailure()
{
  for (bool throws : {false, true}) {
    unsigned calls = 0;
    DiagnosticWriter writer(
        [&](std::string_view) {
          ++calls;
          if (throws)
            throw std::runtime_error("sink failed");
          return false;
        },
        loss
    );
    check(writer.append("failure"), "failure fixture admitted");
    check(writer.finish(5s), "failed sink terminates");
    check(calls == 1 && writer.status().failed, "sink failure is terminal without retry loop");
    check(!writer.append("late"), "failed sink refuses more work");
  }
}
void concurrentProducers()
{
  const auto gate = std::make_shared<Gate>();
  DiagnosticWriter writer([gate](std::string_view line) { return gate->write(line); }, loss);
  check(writer.append("first"), "concurrent fixture admitted");
  gate->wait();
  std::vector<std::thread> producers;
  std::array<bool, 4> admitted{true, true, true, true};
  for (unsigned p = 0; p < admitted.size(); ++p)
    producers.emplace_back([&, p] {
      for (unsigned i = 0; i < 20; ++i)
        admitted[p] &= writer.append(std::to_string(p) + ":" + std::to_string(i));
    });
  for (auto &producer : producers)
    producer.join();
  for (bool accepted : admitted)
    check(accepted, "all concurrent records admitted within bound");
  gate->release();
  check(writer.finish(5s) && !writer.status().failed, "concurrent records drain");
  std::array<unsigned, 4> next{};
  for (std::size_t i = 1; i < gate->lines.size(); ++i) {
    const auto &line = gate->lines[i];
    const auto p = unsigned(line[0] - '0');
    check(p < next.size(), "valid producer identity");
    check(line == std::to_string(p) + ":" + std::to_string(next[p]++), "per-producer order retained");
  }
  for (unsigned count : next)
    check(count == 20, "all concurrent lines retained");
}
#ifndef _WIN32
void descriptorLifetimeAndClosedPipe()
{
  int descriptors[2];
  check(pipe(descriptors) == 0, "private pipe created");
  const auto output = std::make_shared<DiagnosticOutput>(descriptors[1]);
  close(descriptors[1]);
  {
    DiagnosticWriter writer([output](std::string_view line) { return output->write(line); }, loss);
    check(writer.append("owned descriptor\n") && writer.finish(5s), "owned duplicate survives original close");
    check(!writer.status().failed, "duplicate write succeeds");
    std::array<char, 32> buffer{};
    const auto size = read(descriptors[0], buffer.data(), buffer.size());
    check(size == 17 && std::string_view(buffer.data(), std::size_t(size)) == "owned descriptor\n", "exact pipe bytes");
  }
  close(descriptors[0]);
  DiagnosticWriter writer([output](std::string_view line) { return output->write(line); }, loss);
  check(writer.append("closed reader") && writer.finish(5s), "closed diagnostic reader does not kill process");
  check(writer.status().failed, "closed reader is terminal output failure");
}
#endif
} // namespace
int main()
{
  try {
    boundedRecords();
    boundedBytes();
    boundedShutdown();
    outputFailure();
    concurrentProducers();
#ifndef _WIN32
    descriptorLifetimeAndClosedPipe();
#endif
    std::cout << "Diagnostic writer bounds, loss, shutdown, failures and ordering pass\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
