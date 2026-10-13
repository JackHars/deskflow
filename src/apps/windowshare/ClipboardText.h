/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "Core.h"
#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <functional>

namespace deskflow::windowshare {

inline constexpr auto clipboardTextCapability = "clipboard-text-v1";
inline constexpr qsizetype maximumClipboardTextBytes = 8192;
inline constexpr std::uint64_t maximumClipboardSequence = (std::uint64_t{1} << 53) - 1;

// Clipboard focus is independent of an input route: this grants no keyboard,
// pointer, original-input or compositor authority.
struct ClipboardFocusBinding
{
  Owner owner;
  std::uint64_t focusRequest = 0, focusRevision = 0, geometry = 0;
  bool operator==(const ClipboardFocusBinding &) const = default;
};
bool validClipboardFocus(const ClipboardFocusBinding &binding);

// Local-only retained identity. The callback must recheck the same native
// process/window/lifecycle or painted Viewer lifetime; it never crosses TLS.
struct ClipboardFocusObservation
{
  ClipboardFocusBinding binding;
  std::function<bool()> current;
  bool live() const;
};
struct ClipboardTextMessage
{
  ClipboardFocusBinding focus;
  std::uint64_t sequence = 0;
  QString text;
  bool operator==(const ClipboardTextMessage &) const = default;
};
bool validClipboardText(const QString &text);
QJsonArray clipboardTextCapabilities(QJsonArray capabilities, bool enabled);
bool peerSupportsClipboardText(const QJsonObject &negotiation);
QJsonObject encodeClipboardText(const ClipboardTextMessage &message);
ClipboardTextMessage parseClipboardText(const Packet &packet);

// A metadata-only source for platforms which do not report clipboard changes
// while another application is active. A change is eligible only across two
// observations of the same still-live focus. Focus acquisition/replacement is
// a baseline, never permission to publish existing pasteboard contents. This
// observes focus continuity; it cannot identify the pasteboard's producer.
class ClipboardChangeMonitor final : public QObject
{
public:
  struct Callbacks
  {
    std::function<std::optional<ClipboardFocusObservation>()> focus;
    std::function<std::optional<std::int64_t>()> readRevision;
    std::function<void(const ClipboardFocusObservation &)> changed;
  };
  explicit ClipboardChangeMonitor(Callbacks callbacks, QObject *parent = nullptr);
  void setEnabled(bool enabled);
  void resetBaseline();
  bool poll();

private:
  Callbacks m_callbacks;
  bool m_enabled = false, m_busy = false;
  std::uint64_t m_generation = 1;
  std::optional<std::int64_t> m_revision;
  std::optional<ClipboardFocusObservation> m_focus;
};

// Event-driven, bounded text-only synchronization. It does not read clipboard
// contents on enable/focus changes. Production supplies QClipboard operations;
// regression tests supply private strings, so they never touch a pasteboard.
class FocusedClipboard final : public QObject
{
public:
  struct Callbacks
  {
    std::function<std::optional<ClipboardFocusObservation>()> focus;
    std::function<std::optional<QString>()> readText;
    std::function<bool(const QString &)> writeText;
    std::function<bool(const ClipboardTextMessage &)> send;
  };
  explicit FocusedClipboard(bool policy, Callbacks callbacks, QObject *parent = nullptr);
  void setNegotiated(bool enabled);
  bool enabled() const;
  bool localChanged(std::optional<ClipboardFocusObservation> change = {});
  bool receive(const ClipboardTextMessage &message);

private:
  const bool m_policy;
  Callbacks m_callbacks;
  bool m_negotiated = false, m_busy = false;
  std::uint64_t m_generation = 1, m_sent = 0, m_received = 0;
  // Keep an applied digest across focus changes and disconnects: a delayed
  // dataChanged signal must not echo old peer content into a replacement focus.
  QByteArray m_appliedDigest, m_localDigest;
};

} // namespace deskflow::windowshare
