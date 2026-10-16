/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <QCoreApplication>
#include <QMetaObject>
#include <exception>
#include <functional>
#include <optional>
#include <pthread.h>
#include <stdexcept>
#include <type_traits>
#include <utility>

// Text Input Services can assert main-queue ownership even for read-only
// enumeration. A mutex serializes callers but cannot satisfy that requirement.
// Keep each native operation synchronous, without holding a caller-side lock
// while the main event loop services it. Main-thread callers execute directly.
template <class Function> auto onMacMainThread(Function &&function)
{
  using Result = std::invoke_result_t<Function>;
  static_assert(!std::is_reference_v<Result>);
  if (pthread_main_np())
    return std::invoke(std::forward<Function>(function));
  auto *application = QCoreApplication::instance();
  if (!application || QCoreApplication::closingDown())
    throw std::runtime_error("Native input-source access requires the live main event loop");
  struct Invocation
  {
    Function &function;
    std::optional<std::conditional_t<std::is_void_v<Result>, bool, Result>> result;
    std::exception_ptr error;
  } invocation{function, {}, {}};
  const bool invoked = QMetaObject::invokeMethod(
      application,
      [&invocation] {
        auto &call = invocation;
        try {
          if constexpr (std::is_void_v<Result>) {
            std::invoke(call.function);
            call.result.emplace(true);
          } else {
            call.result.emplace(std::invoke(call.function));
          }
        } catch (...) {
          call.error = std::current_exception();
        }
      },
      Qt::BlockingQueuedConnection
  );
  if (!invoked)
    throw std::runtime_error("Cannot dispatch native input-source access to the main event loop");
  if (invocation.error)
    std::rethrow_exception(invocation.error);
  if constexpr (!std::is_void_v<Result>)
    return std::move(*invocation.result);
}
