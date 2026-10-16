/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "NativeSharingActivity_mac.h"
#include <cstdio>
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

using deskflow::windowshare::native::SharingActivity;

static void check(bool value, const char *message)
{
  if (!value)
    throw std::runtime_error(message);
}

int main()
{
  @autoreleasepool {
    try {
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
      }
      std::puts("Sharing activity lifetime and native acquisition checks passed");
      return 0;
    } catch (const std::exception &error) {
      std::fprintf(stderr, "%s\n", error.what());
      return 1;
    }
  }
}
