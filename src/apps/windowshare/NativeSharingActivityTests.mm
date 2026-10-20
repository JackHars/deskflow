/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "LocalInputActivity.h"
#include "NativeInputScheduling_mac.h"
#include "NativeSharingActivity_mac.h"
#include <array>
#include <cstdio>
#include <set>
#include <stdexcept>
#include <type_traits>

@interface DFActivityProcess : NSProcessInfo
@property(nonatomic) NSUInteger begun;
@property(nonatomic) NSUInteger ended;
@property(nonatomic) NSActivityOptions observedOptions;
@property(nonatomic) BOOL refuse;
@property(nonatomic, strong) NSMutableSet<id<NSObject>> *tokens;
@end

@implementation DFActivityProcess
- (id<NSObject>)beginActivityWithOptions:(NSActivityOptions)options reason:(NSString *)reason
{
  if (!reason.length)
    throw std::runtime_error("Activity needs a diagnostic reason");
  ++_begun;
  _observedOptions = options;
  if (_refuse)
    return nil;
  if (!_tokens)
    _tokens = [NSMutableSet set];
  id<NSObject> token = [[NSObject alloc] init];
  [_tokens addObject:token];
  return token;
}
- (void)endActivity:(id<NSObject>)token
{
  if (![_tokens containsObject:token])
    throw std::runtime_error("Activity ended twice or with another export's token");
  [_tokens removeObject:token];
  ++_ended;
}
@end

using deskflow::windowshare::native::InputScheduling;
using deskflow::windowshare::native::SharingActivity;

static void check(bool value, const char *message)
{
  if (!value)
    throw std::runtime_error(message);
}

namespace {
std::array<int, 4> schedulingTokens{};
std::set<pthread_override_t> schedulingLive;
unsigned schedulingStarted = 0, schedulingEnded = 0;
bool schedulingRefused = false;
pthread_override_t startScheduling(pthread_t thread, qos_class_t qos, int priority)
{
  check(
      pthread_equal(thread, pthread_self()) && qos == QOS_CLASS_USER_INTERACTIVE && priority == 0,
      "Input scheduling changed another thread or requested a different priority"
  );
  auto token = reinterpret_cast<pthread_override_t>(&schedulingTokens.at(schedulingStarted++));
  if (schedulingRefused)
    return nullptr;
  check(schedulingLive.insert(token).second, "Input scheduling reused a live override");
  return token;
}
int endScheduling(pthread_override_t token)
{
  check(schedulingLive.erase(token) == 1, "Input ended another operation's scheduling override");
  ++schedulingEnded;
  return 0;
}
void schedulingLifetime()
{
  struct NativeOperationFailure
  {
  };
  static_assert(!std::is_copy_constructible_v<InputScheduling>);
  static_assert(!std::is_move_constructible_v<InputScheduling>);
  {
    InputScheduling first(startScheduling, endScheduling);
    check(first.active(), "Input did not acquire a scheduling override");
    try {
      InputScheduling nested(startScheduling, endScheduling);
      check(nested.active() && schedulingLive.size() == 2, "Nested input replaced an existing override");
      throw NativeOperationFailure{};
    } catch (const NativeOperationFailure &) {
    }
    check(schedulingEnded == 1 && schedulingLive.size() == 1, "Failed native work leaked its override");
  }
  check(schedulingEnded == 2 && schedulingLive.empty(), "Completed input kept elevated scheduling");
  schedulingRefused = true;
  {
    InputScheduling refused(startScheduling, endScheduling);
    check(!refused.active(), "Refused scheduling appeared acquired");
  }
  check(schedulingEnded == 2 && schedulingLive.empty(), "Missing scheduling token was ended");
  {
    InputScheduling native;
    check(native.active(), "macOS refused the scoped native input scheduling request");
  }
}
} // namespace

int main()
{
  @autoreleasepool {
    try {
      schedulingLifetime();
      static_assert(!std::is_copy_constructible_v<SharingActivity>);
      static_assert(!std::is_move_constructible_v<SharingActivity>);
      static_assert((SharingActivity::options & NSActivityIdleSystemSleepDisabled) == 0);
      static_assert((SharingActivity::options & NSActivityIdleDisplaySleepDisabled) == 0);
      auto *process = [[DFActivityProcess alloc] init];
      {
        SharingActivity first(process);
        check(first.active() && process.begun == 1 && process.ended == 0, "Export did not acquire an activity");
        check(
            process.observedOptions == NSActivityUserInitiatedAllowingIdleSystemSleep,
            "Interactive export used background scheduling or inhibited sleep"
        );
        {
          SharingActivity second(process);
          check(second.active() && process.tokens.count == 2, "Overlapping exports did not retain distinct activities");
        }
        check(process.ended == 1 && process.tokens.count == 1, "Closing one export released another export's activity");
      }
      check(process.ended == 2 && process.tokens.count == 0, "Last export leaked an activity");
      try {
        SharingActivity failedExport(process);
        throw std::runtime_error("Capture setup failed");
      } catch (const std::runtime_error &) {
      }
      check(
          process.begun == 3 && process.ended == 3 && process.tokens.count == 0,
          "Failed capture setup leaked its activity"
      );
      process.refuse = YES;
      {
        SharingActivity refused(process);
        check(!refused.active(), "Failed activity acquisition appeared active");
      }
      check(process.begun == 4 && process.ended == 3, "Missing activity token was ended");
      // Exercise Foundation itself without a window, event tap, capture or input.
      {
        SharingActivity native;
        check(native.active(), "Foundation refused the interactive scheduling activity");
        deskflow::windowshare::LocalInputActivity core;
        check(core.active(), "The C++ Core owner did not retain its own process activity");
      }
      std::puts("Sharing activity lifetime and native acquisition checks passed");
      return 0;
    } catch (const std::exception &error) {
      std::fprintf(stderr, "%s\n", error.what());
      return 1;
    }
  }
}
