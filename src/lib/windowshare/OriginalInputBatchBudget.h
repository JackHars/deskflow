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
// half the existing receipt lifetime for observed native work, leaving the
// remainder for mapping, transport and scheduling variation. Every event still
// needs its original live receipt and fresh native authority at submission.
class OriginalInputBatchBudget
{
public:
  // A full 16-point protocol batch can outlive its receipt after one native
  // scheduling stall. Keep the queued prefix smaller even after fast samples;
  // the rest stays ordered in the existing queue and maps after dispatch ack.
  static constexpr std::size_t maximumPrefix = inputCutoffMaximumBatch / 2;
  std::size_t limit() const
  {
    if (m_perRecord.count() <= 0)
      return 4;
    return std::clamp<std::size_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(inputCutoffMaximumLifetime / 2) / m_perRecord, 1,
        maximumPrefix
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
