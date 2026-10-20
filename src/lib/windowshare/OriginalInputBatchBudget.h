/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include "InputCutoff.h"
#include <algorithm>
#include <chrono>
#include <cstddef>

namespace deskflow::windowshare {
// Scheduling advice for one input owner, never a delivery lease. Keep roughly
// native work within a short interactive turn, leaving time for mapping,
// transport and scheduling variation. Every event still
// needs its original live receipt and fresh native authority at submission.
class OriginalInputBatchBudget
{
public:
  // macOS may rewrite a timestamp already 100 ms old when posting to a PID.
  // Spending half the 250 ms receipt on one native batch can therefore lose
  // original timestamps even before the receipt expires. Aim for 50 ms of
  // observed work; this is scheduling advice, not a new expiry or a guarantee
  // about the OS. Fast work may still use the full protocol capacity. Exact
  // ordered peer dispatch and every event's original receipt remain required.
  static constexpr auto targetDispatchTime = std::chrono::milliseconds(50);
  static constexpr std::size_t maximumPrefix = inputCutoffMaximumBatch;
  std::size_t limit() const
  {
    if (m_perRecord.count() <= 0)
      return 4;
    return std::clamp<std::size_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(targetDispatchTime) / m_perRecord, 1, maximumPrefix
    );
  }
  void observe(std::size_t records, std::chrono::nanoseconds elapsed)
  {
    if (!records || records > inputCutoffMaximumBatch || elapsed.count() <= 0)
      return;
    const auto count = static_cast<std::chrono::nanoseconds::rep>(records);
    const std::chrono::nanoseconds cost(elapsed.count() / count + (elapsed.count() % count != 0));
    // React immediately to slower delivery; recover capacity gradually after
    // a transient stall. Overflow-free weighted decay retains the slower cost.
    m_perRecord = std::max(cost, m_perRecord - m_perRecord / 4 + cost / 4);
  }

private:
  std::chrono::nanoseconds m_perRecord{};
};
} // namespace deskflow::windowshare
