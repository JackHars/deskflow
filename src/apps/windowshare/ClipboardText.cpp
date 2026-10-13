/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "ClipboardText.h"
#include "KeyboardRouteProtocol.h"
#include "Transport.h"
#include <QCryptographicHash>
#include <QPointer>
#include <QScopeGuard>
#include <QThread>
#include <cmath>

namespace deskflow::windowshare {
namespace {
void require(bool condition, const char *message)
{
  if (!condition)
    throw std::runtime_error(message);
}
std::uint64_t integer(const QJsonValue &value)
{
  const auto number = value.toDouble(-1);
  require(
      value.isDouble() && std::isfinite(number) && number >= 1 && number <= maximumClipboardSequence &&
          std::floor(number) == number,
      "Invalid clipboard revision"
  );
  return static_cast<std::uint64_t>(number);
}
QByteArray digest(const QString &text)
{
  return QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256);
}
} // namespace

bool validClipboardFocus(const ClipboardFocusBinding &binding)
{
  for (const auto value :
       {binding.owner.session, binding.owner.epoch, binding.focusRequest, binding.focusRevision, binding.geometry})
    if (!value || value > maximumClipboardSequence)
      return false;
  return true;
}
bool ClipboardFocusObservation::live() const
{
  return validClipboardFocus(binding) && current && current();
}
bool validClipboardText(const QString &text)
{
  if (text.size() > maximumClipboardTextBytes || text.contains(QChar('\0')))
    return false;
  const auto utf8 = text.toUtf8();
  return utf8.size() <= maximumClipboardTextBytes && QString::fromUtf8(utf8) == text;
}
QJsonArray clipboardTextCapabilities(QJsonArray capabilities, bool enabled)
{
  if (enabled)
    capabilities.append(QLatin1String(clipboardTextCapability));
  return capabilities;
}
bool peerSupportsClipboardText(const QJsonObject &negotiation)
{
  // Reuse the bounded, unique-name parser, without making clipboard permission
  // depend on an input-route capability.
  (void)peerSupportsKeyboardRoute(negotiation);
  return negotiation["capabilities"].toArray().contains(QLatin1String(clipboardTextCapability));
}
QJsonObject encodeClipboardText(const ClipboardTextMessage &message)
{
  require(
      validClipboardFocus(message.focus) && message.sequence && message.sequence <= maximumClipboardSequence &&
          validClipboardText(message.text),
      "Invalid focused clipboard text"
  );
  return {
      {"focusRequest", static_cast<qint64>(message.focus.focusRequest)},
      {"focusRevision", static_cast<qint64>(message.focus.focusRevision)},
      {"geometry", static_cast<qint64>(message.focus.geometry)},
      {"sequence", static_cast<qint64>(message.sequence)},
      {"text", message.text}
  };
}
ClipboardTextMessage parseClipboardText(const Packet &packet)
{
  require(
      packet.type == MessageType::ClipboardText && packet.payload.size() <= maxControlPayload,
      "Invalid clipboard packet"
  );
  const auto object = parseObject(packet);
  require(object["text"].isString(), "Missing clipboard text");
  ClipboardTextMessage message{
      {{packet.session, packet.epoch},
       integer(object["focusRequest"]),
       integer(object["focusRevision"]),
       integer(object["geometry"])},
      integer(object["sequence"]),
      object["text"].toString()
  };
  require(object == encodeClipboardText(message), "Unexpected clipboard fields");
  return message;
}

ClipboardChangeMonitor::ClipboardChangeMonitor(Callbacks callbacks, QObject *parent)
    : QObject(parent),
      m_callbacks(std::move(callbacks))
{
}
void ClipboardChangeMonitor::setEnabled(bool enabled)
{
  if (QThread::currentThread() != thread() || m_enabled == enabled)
    return;
  m_enabled = enabled;
  resetBaseline();
}
void ClipboardChangeMonitor::resetBaseline()
{
  if (QThread::currentThread() != thread())
    return;
  ++m_generation;
  m_revision.reset();
  m_focus.reset();
}
bool ClipboardChangeMonitor::poll()
{
  if (QThread::currentThread() != thread() || !m_enabled || m_busy || !m_callbacks.focus || !m_callbacks.readRevision ||
      !m_callbacks.changed)
    return false;
  const QPointer<ClipboardChangeMonitor> alive(this);
  const auto generation = m_generation;
  const auto callbacks = m_callbacks;
  const auto previous = m_focus;
  const auto revision = m_revision;
  m_busy = true;
  const auto reset = qScopeGuard([alive] {
    if (alive)
      alive->m_busy = false;
  });
  const auto current = [&] { return alive && alive->m_enabled && alive->m_generation == generation; };
  const auto observation = callbacks.focus();
  if (!current())
    return false;
  const bool observed = observation && observation->live();
  if (!current())
    return false;
  const bool continuous = observed && previous && previous->binding == observation->binding && previous->live();
  if (!current())
    return false;
  const auto count = callbacks.readRevision();
  if (!current())
    return false;
  const bool stillObserved = observed && observation->live();
  if (!current())
    return false;
  const bool stillContinuous = continuous && previous->live();
  if (!current())
    return false;
  // Inaccessible metadata or focus invalidates the baseline. A new sample must
  // establish its own baseline before a later change can trigger a text read.
  m_revision = count && *count >= 0 ? count : std::nullopt;
  m_focus = m_revision && stillObserved ? observation : std::nullopt;
  if (!m_revision || !revision || *m_revision == *revision || !stillContinuous || !stillObserved)
    return false;
  callbacks.changed(*observation);
  if (!current())
    return false;
  const bool live = observation->live();
  return current() && live;
}

FocusedClipboard::FocusedClipboard(bool policy, Callbacks callbacks, QObject *parent)
    : QObject(parent),
      m_policy(policy),
      m_callbacks(std::move(callbacks))
{
}
void FocusedClipboard::setNegotiated(bool enabled)
{
  if (QThread::currentThread() != thread())
    return;
  enabled = enabled && m_policy;
  if (m_negotiated == enabled)
    return;
  m_negotiated = enabled;
  ++m_generation;
  m_sent = m_received = 0;
  // Delayed signals still see the retained digests; do not publish existing
  // clipboard data as a side effect of enabling a new connection.
}
bool FocusedClipboard::enabled() const
{
  return QThread::currentThread() == thread() && m_policy && m_negotiated;
}
bool FocusedClipboard::localChanged(std::optional<ClipboardFocusObservation> change)
{
  if (!enabled() || m_busy || !m_callbacks.focus || !m_callbacks.readText || !m_callbacks.send ||
      m_sent == maximumClipboardSequence)
    return false;
  const QPointer<FocusedClipboard> alive(this);
  const auto generation = m_generation;
  const auto callbacks = m_callbacks;
  m_busy = true;
  const auto reset = qScopeGuard([alive] {
    if (alive)
      alive->m_busy = false;
  });
  const auto observation = callbacks.focus();
  const auto current = [&] {
    if (!alive || !alive->enabled() || alive->m_generation != generation || !observation)
      return false;
    if (change) {
      if (change->binding != observation->binding || !change->live() || !alive || !alive->enabled() ||
          alive->m_generation != generation)
        return false;
    }
    const bool live = observation->live();
    return alive && alive->enabled() && alive->m_generation == generation && live;
  };
  if (!current())
    return false;
  const auto text = callbacks.readText();
  if (!current() || !text || !validClipboardText(*text))
    return false;
  const auto hash = digest(*text);
  if (hash == m_appliedDigest || hash == m_localDigest)
    return false;
  if (!current())
    return false;
  m_appliedDigest.clear();
  m_localDigest = hash;
  const ClipboardTextMessage message{observation->binding, ++m_sent, *text};
  const bool sent = callbacks.send(message);
  return current() && sent;
}
bool FocusedClipboard::receive(const ClipboardTextMessage &message)
{
  if (!enabled() || !validClipboardFocus(message.focus) || !validClipboardText(message.text) || !message.sequence ||
      message.sequence > maximumClipboardSequence || message.sequence <= m_received)
    return false;
  // Consume even a stale-focus or reentrant update. It cannot be replayed after
  // a focus replacement, and no unbounded deferred clipboard queue is retained.
  m_received = message.sequence;
  if (m_busy || !m_callbacks.focus || !m_callbacks.writeText)
    return false;
  const QPointer<FocusedClipboard> alive(this);
  const auto generation = m_generation;
  const auto callbacks = m_callbacks;
  m_busy = true;
  const auto reset = qScopeGuard([alive] {
    if (alive)
      alive->m_busy = false;
  });
  const auto observation = callbacks.focus();
  const auto current = [&] {
    if (!alive || !alive->enabled() || alive->m_generation != generation || !observation ||
        observation->binding != message.focus)
      return false;
    const bool live = observation->live();
    return alive && alive->enabled() && alive->m_generation == generation && live;
  };
  if (!current())
    return false;
  // Install suppression BEFORE calling a clipboard implementation that can
  // synchronously emit dataChanged, replace focus or destroy this controller.
  m_appliedDigest = digest(message.text);
  m_localDigest.clear();
  const bool written = callbacks.writeText(message.text);
  return current() && written;
}

} // namespace deskflow::windowshare
