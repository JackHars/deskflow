/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <QJsonObject>
#include <functional>
#include <optional>

namespace deskflow::windowshare {

// Write-ahead recovery bookkeeping, not a native drag or input authority.
// Preparing a capture does not own the source's current position/focus. Only
// an attempted native mutation or committed remote session requires rollback.
class NativeSourceTransaction final
{
public:
  bool arm(const std::function<bool()> &persist)
  {
    if (m_returning || !persist)
      return false;
    if (m_durable)
      return true;
    m_restoreJournalRequired = true;
    const bool durable = persist();
    if (m_returning)
      return false;
    m_durable = durable;
    return durable;
  }
  bool attemptMutation()
  {
    if (!m_durable || m_returning)
      return false;
    m_mutated = true;
    return true;
  }
  // Display creation can rearrange a source before an explicit AX move. The
  // attempted display change needs the same write-ahead recovery as placement.
  bool armMutation(const std::function<bool()> &persist)
  {
    return arm(persist) && attemptMutation();
  }
  bool commit()
  {
    if (!m_durable || m_returning)
      return false;
    m_committed = true;
    return true;
  }
  bool journalRestoreRequired() const noexcept
  {
    return m_restoreJournalRequired;
  }
  bool restoreRequired() const noexcept
  {
    return m_mutated || m_committed;
  }
  bool beginReturn() noexcept
  {
    if (m_returning)
      return false;
    m_returning = true;
    return true;
  }

private:
  bool m_restoreJournalRequired = false, m_durable = false;
  bool m_mutated = false, m_committed = false, m_returning = false;
};

// Old journals conservatively require restoration. Only an explicit boolean
// false written by the new host can discard an untouched preparation without
// enumerating, moving, unminimizing or raising a native source window.
inline std::optional<bool> nativeJournalRestoreRequired(const QJsonObject &record)
{
  if (!record.contains(QStringLiteral("restoreRequired")))
    return true;
  const auto value = record.value(QStringLiteral("restoreRequired"));
  return value.isBool() ? std::optional(value.toBool()) : std::nullopt;
}

struct NativeSourceReturnAccess
{
  // Exact retained entry/owner lifetime, checked after every borrowed callback.
  std::function<bool()> current;
  std::function<void()> fence, releaseInput, stopCapture;
  std::function<bool()> restoreSource;
  std::function<void()> retainFailure, finish;
};
enum class NativeSourceReturnResult
{
  Aborted,
  Closed,
  RecoveryRetained
};

// The actual NativeHost return procedure. Isolated tests supply native-free
// callbacks; production supplies exact identity checks and durable journaling.
// Reentrant returns cannot run the procedure twice. In particular, cancelling
// an untouched preparation performs no source query or native mutation.
inline NativeSourceReturnResult
returnNativeSource(NativeSourceTransaction &transaction, const NativeSourceReturnAccess &access)
{
  if (!access.current || !access.fence || !access.releaseInput || !access.stopCapture || !access.restoreSource ||
      !access.retainFailure || !access.finish || !access.current() || !transaction.beginReturn())
    return NativeSourceReturnResult::Aborted;
  access.fence();
  if (!access.current())
    return NativeSourceReturnResult::Aborted;
  access.releaseInput();
  if (!access.current())
    return NativeSourceReturnResult::Aborted;
  access.stopCapture();
  if (!access.current())
    return NativeSourceReturnResult::Aborted;
  const bool restored = !transaction.restoreRequired() || access.restoreSource();
  if (!access.current())
    return NativeSourceReturnResult::Aborted;
  if (!restored) {
    access.retainFailure();
    if (!access.current())
      return NativeSourceReturnResult::Aborted;
  }
  access.finish(); // Removes the entry; current() is no longer expected true.
  return restored ? NativeSourceReturnResult::Closed : NativeSourceReturnResult::RecoveryRetained;
}

} // namespace deskflow::windowshare
