/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "SelectedPeerMailbox.h"
#include <limits>
#include <utility>

namespace deskflow::windowshare {

SelectedPeerMailbox::Observation::Observation(
    SelectedPeer peer, std::uint64_t revision, std::uint64_t selection,
    std::shared_ptr<const std::atomic<std::uint64_t>> token, Clock::time_point published
)
    : m_peer(std::move(peer)),
      m_revision(revision),
      m_selection(selection),
      m_token(std::move(token)),
      m_published(published)
{
}

const SelectedPeer &SelectedPeerMailbox::Observation::peer() const noexcept
{
  return m_peer;
}

std::uint64_t SelectedPeerMailbox::Observation::revision() const noexcept
{
  return m_revision;
}

bool SelectedPeerMailbox::Observation::live() const noexcept
{
  return liveAt(Clock::now());
}

bool SelectedPeerMailbox::Observation::liveAt(Clock::time_point now) const noexcept
{
  return now >= m_published && now - m_published < freshness && m_peer.live() && m_token && m_selection &&
         m_token->load(std::memory_order_acquire) == m_selection;
}

SelectedPeerMailbox::SelectedPeerMailbox() : m_token(std::make_shared<std::atomic<std::uint64_t>>(0))
{
}

SelectedPeerMailbox::~SelectedPeerMailbox()
{
  close();
}

bool SelectedPeerMailbox::replace(const SelectedPeer &peer)
{
  return replaceAt(peer, Clock::now());
}

bool SelectedPeerMailbox::replaceAt(const SelectedPeer &peer, Clock::time_point now)
{
  std::lock_guard lock(m_mutex);
  if (m_closed)
    return false;
  if (!peer.live()) {
    clearLocked();
    return false;
  }
  if (!advanceLocked())
    return false;
  const bool same = m_selected && m_selected->peer().sameSelection(peer);
  const auto selection = same ? m_selected->m_selection : m_revision;
  if (!same)
    m_token->store(selection, std::memory_order_release);
  m_selected = Observation(peer, m_revision, selection, m_token, now);
  return true;
}

bool SelectedPeerMailbox::clear()
{
  std::lock_guard lock(m_mutex);
  return clearLocked();
}

bool SelectedPeerMailbox::clearLocked()
{
  if (!advanceLocked())
    return false;
  m_token->store(0, std::memory_order_release);
  m_selected.reset();
  return true;
}

bool SelectedPeerMailbox::advanceLocked()
{
  if (m_closed)
    return false;
  if (m_revision >= std::numeric_limits<std::uint64_t>::max() - 1) {
    closeLocked();
    return false;
  }
  ++m_revision;
  return true;
}

void SelectedPeerMailbox::close()
{
  std::lock_guard lock(m_mutex);
  closeLocked();
}

void SelectedPeerMailbox::closeLocked()
{
  if (m_closed)
    return;
  m_closed = true;
  m_token->store(0, std::memory_order_release);
  m_selected.reset();
  if (m_revision < std::numeric_limits<std::uint64_t>::max())
    ++m_revision;
}

SelectedPeerMailbox::ReadResult SelectedPeerMailbox::readLocked() const
{
  return {m_revision, m_closed, m_selected};
}

SelectedPeerMailbox::ReadResult SelectedPeerMailbox::read() const
{
  std::lock_guard lock(m_mutex);
  return readLocked();
}

std::optional<SelectedPeerMailbox::ReadResult> SelectedPeerMailbox::readAfter(std::uint64_t revision) const
{
  std::lock_guard lock(m_mutex);
  return m_revision == revision ? std::nullopt : std::optional{readLocked()};
}

} // namespace deskflow::windowshare
