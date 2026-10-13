/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include "InputCutoff.h"
#include "InputPreparation.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <cstdint>
#include <optional>
#include <vector>

namespace deskflow::windowshare {

// Private inherited stream only. Four-byte big-endian length followed by one
// closed-schema UTF-8 JSON object. These messages are observations, never input
// delivery authority. uint64 values use canonical decimal strings; dimensions
// and coordinates use finite JSON numbers. The core owns the earliest deadline.
inline constexpr qsizetype gnomeViewportMaximumPayload = 8192;
inline constexpr int gnomeViewportLifetimeMs = 250;

enum class GnomeViewportOperation
{
  Snapshot,
  Map,
  Check,
  Invalidated,
  InputDrain,
  InputSeal,
  InputMap,
  SealInvalidated,
  InputMapBatch,
  PreparationInventory,
  PreparationMap,
  PreparationDrain,
  PreparationFocus,
  PreparationPromote,
  PreparationCleanup
};
inline bool isGnomePreparationOperation(GnomeViewportOperation operation)
{
  return operation >= GnomeViewportOperation::PreparationInventory &&
         operation <= GnomeViewportOperation::PreparationCleanup;
}

// Unfocused painted identity: deliberately no focus lease or All binding.
// The Viewer retains the actual complete paint transform behind this revision.
struct GnomePreparationTarget
{
  std::uint64_t session = 0, epoch = 0, geometry = 0, surfaceIncarnation = 0, paintRevision = 0;
  std::uint32_t nativeSurface = 0;
  QString windowAssociation;
  bool operator==(const GnomePreparationTarget &) const = default;
};
struct GnomePreparationInventory
{
  std::uint64_t connection = 0;
  QString peerFingerprint;
  std::vector<GnomePreparationTarget> targets;
  bool operator==(const GnomePreparationInventory &) const = default;
};

struct GnomeViewportState
{
  std::uint64_t connection = 0, session = 0, epoch = 0, geometry = 0;
  std::uint64_t focusRequest = 0, focusRevision = 0, surfaceIncarnation = 0, viewportRevision = 0;
  QString peerFingerprint;
  QString windowAssociation;
  std::uint32_t nativeSurface = 0;
  double clientWidth = 0, clientHeight = 0, sourceWidth = 0, sourceHeight = 0;
  std::uint32_t imageWidth = 0, imageHeight = 0;
  double videoX = 0, videoY = 0, videoWidth = 0, videoHeight = 0, dpr = 0;
  // Actual QtWayland wl_surface geometry. Surface->widget conversion is
  // (point - content origin) / logicalScale, not division by buffer DPR.
  double surfaceWidth = 0, surfaceHeight = 0, contentX = 0, contentY = 0, logicalScale = 0;
  bool operator==(const GnomeViewportState &) const = default;
};

struct GnomeViewportRequest
{
  GnomeViewportOperation operation = GnomeViewportOperation::Snapshot;
  std::uint64_t request = 0, snapshot = 0;
  double x = 0, y = 0;
  InputCutoffBinding cutoff{};
  std::uint32_t budgetMs = 0;
  std::uint64_t seal = 0;
  struct MapItem
  {
    InputCutoffBinding cutoff;
    double x = 0, y = 0;
    bool operator==(const MapItem &) const = default;
  };
  std::vector<MapItem> points{};
  InputPreparationBinding preparation{};
  GnomePreparationTarget target{};
  bool operator==(const GnomeViewportRequest &) const = default;
};

struct GnomeViewportReply
{
  GnomeViewportOperation operation = GnomeViewportOperation::Snapshot;
  std::uint64_t request = 0, snapshot = 0;
  bool ok = false;
  QString reason;
  GnomeViewportState state;
  double x = 0, y = 0;
  InputCutoffBinding cutoff{};
  std::uint64_t seal = 0;
  std::vector<GnomeViewportRequest::MapItem> points{};
  InputPreparationBinding preparation{};
  GnomePreparationTarget target{};
  GnomePreparationInventory inventory{};
};

// Snapshot success: {version,operation,request,ok:true,state}. Its request ID
// is the retained snapshot ID. Map/check success adds snapshot (map also x/y).
// InputDrain requires cutoff/budgetMs and echoes cutoff/snapshot on success or
// denial. Other denial: {version,operation,request,ok:false,reason}. Revocation:
// {version:1,operation:"invalidated",snapshot,reason}; no request/ok fields.
// One retained snapshot and one revocation are enough; no timer may renew it.
QJsonObject gnomeViewportJson(const GnomeViewportRequest &request);
QJsonObject gnomeViewportJson(const GnomeViewportReply &reply);
GnomeViewportRequest parseGnomeViewportRequest(const QJsonObject &object);
GnomeViewportReply parseGnomeViewportReply(const QJsonObject &object);
QByteArray gnomeViewportFrame(const QJsonObject &object);

// Feed bounded chunks, then drain take(). A failed decoder is terminal. The
// buffer cannot grow beyond two maximum frames; callers also bound queued IO.
class GnomeViewportDecoder final
{
public:
  void append(const QByteArray &bytes);
  std::optional<QJsonObject> take();
  bool empty() const noexcept
  {
    return m_bytes.isEmpty();
  }

private:
  QByteArray m_bytes;
  bool m_failed = false;
};

} // namespace deskflow::windowshare
