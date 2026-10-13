/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "GnomeViewportProtocol.h"
#include "UniqueJson.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QtEndian>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <set>
#include <stdexcept>

namespace deskflow::windowshare {
namespace {
void require(bool condition, const char *reason)
{
  if (!condition)
    throw std::runtime_error(reason);
}
void fields(const QJsonObject &object, const QSet<QString> &expected)
{
  require(object.size() == expected.size(), "Viewport message field count mismatch");
  for (auto it = object.begin(); it != object.end(); ++it)
    require(expected.contains(it.key()), "Unknown viewport field");
}
std::uint64_t sequence(const QJsonObject &object, const char *key)
{
  const auto value = object.value(QLatin1String(key));
  require(value.isString(), "Viewport sequence must be a decimal string");
  const auto bytes = value.toString().toLatin1();
  require(!bytes.isEmpty() && bytes.size() <= 20 && bytes[0] >= '1' && bytes[0] <= '9', "Invalid viewport sequence");
  std::uint64_t result = 0;
  const auto parsed = std::from_chars(bytes.constData(), bytes.constData() + bytes.size(), result);
  require(
      parsed.ec == std::errc{} && parsed.ptr == bytes.constData() + bytes.size() && result, "Invalid viewport sequence"
  );
  return result;
}
double number(const QJsonObject &object, const char *key, double low, double high)
{
  const auto value = object.value(QLatin1String(key));
  const double result = value.toDouble();
  require(value.isDouble() && std::isfinite(result) && result >= low && result <= high, "Invalid viewport number");
  return result;
}
std::uint32_t integer(const QJsonObject &object, const char *key, std::uint32_t maximum)
{
  const double result = number(object, key, 1, maximum);
  require(std::floor(result) == result, "Invalid viewport integer");
  return static_cast<std::uint32_t>(result);
}
QString operationName(GnomeViewportOperation operation)
{
  switch (operation) {
  case GnomeViewportOperation::Snapshot:
    return QStringLiteral("snapshot");
  case GnomeViewportOperation::Map:
    return QStringLiteral("map");
  case GnomeViewportOperation::Check:
    return QStringLiteral("check");
  case GnomeViewportOperation::Invalidated:
    return QStringLiteral("invalidated");
  case GnomeViewportOperation::InputDrain:
    return QStringLiteral("input-drain");
  case GnomeViewportOperation::InputSeal:
    return QStringLiteral("input-seal");
  case GnomeViewportOperation::InputMap:
    return QStringLiteral("input-map");
  case GnomeViewportOperation::SealInvalidated:
    return QStringLiteral("seal-invalidated");
  case GnomeViewportOperation::InputMapBatch:
    return QStringLiteral("input-map-batch");
  case GnomeViewportOperation::PreparationInventory:
    return QStringLiteral("preparation-inventory");
  case GnomeViewportOperation::PreparationMap:
    return QStringLiteral("preparation-map");
  case GnomeViewportOperation::PreparationDrain:
    return QStringLiteral("preparation-drain");
  case GnomeViewportOperation::PreparationFocus:
    return QStringLiteral("preparation-focus");
  case GnomeViewportOperation::PreparationPromote:
    return QStringLiteral("preparation-promote");
  case GnomeViewportOperation::PreparationCleanup:
    return QStringLiteral("preparation-cleanup");
  }
  throw std::runtime_error("Unknown viewport operation");
}
GnomeViewportOperation operation(const QJsonObject &object)
{
  require(number(object, "version", 1, 1) == 1, "Unsupported viewport version");
  const auto name = object.value(QStringLiteral("operation"));
  require(name.isString(), "Viewport operation must be a string");
  for (auto candidate :
       {GnomeViewportOperation::Snapshot, GnomeViewportOperation::Map, GnomeViewportOperation::Check,
        GnomeViewportOperation::Invalidated, GnomeViewportOperation::InputDrain, GnomeViewportOperation::InputSeal,
        GnomeViewportOperation::InputMap, GnomeViewportOperation::SealInvalidated,
        GnomeViewportOperation::InputMapBatch, GnomeViewportOperation::PreparationInventory,
        GnomeViewportOperation::PreparationMap, GnomeViewportOperation::PreparationDrain,
        GnomeViewportOperation::PreparationFocus, GnomeViewportOperation::PreparationPromote,
        GnomeViewportOperation::PreparationCleanup})
    if (name.toString() == operationName(candidate))
      return candidate;
  throw std::runtime_error("Unknown viewport operation");
}
QString reason(const QJsonObject &object)
{
  const auto value = object.value(QStringLiteral("reason"));
  const auto text = value.toString();
  require(
      value.isString() && !text.isEmpty() && text.size() <= 128 &&
          std::all_of(text.begin(), text.end(), [](QChar c) { return c.unicode() >= 32 && c.unicode() < 127; }),
      "Invalid viewport denial reason"
  );
  return text;
}
QJsonObject cutoffJson(const InputCutoffBinding &binding)
{
  const auto &route = binding.forwarding;
  return {
      {"request", QString::number(binding.request)},
      {"coreConnectionGeneration", QString::number(binding.coreConnectionGeneration)},
      {"captureEpoch", QString::number(binding.capture.epoch)},
      {"captureSequence", QString::number(binding.capture.sequence)},
      {"capturePart", QString::number(binding.capture.part)},
      {"viewerConnection", QString::number(binding.viewerConnection)},
      {"session", QString::number(route.owner.session)},
      {"epoch", QString::number(route.owner.epoch)},
      {"generation", QString::number(route.generation)},
      {"focusRequest", QString::number(route.focusRequest)},
      {"focusRevision", QString::number(route.focusRevision)},
      {"geometry", QString::number(route.geometry)},
      {"scope", route.scope == ForwardingScope::All ? QStringLiteral("all") : QStringLiteral("keyboard")}
  };
}
InputCutoffBinding cutoff(const QJsonObject &message)
{
  require(message.value("cutoff").isObject(), "Viewport cutoff missing");
  const auto object = message.value("cutoff").toObject();
  fields(
      object,
      {"request", "coreConnectionGeneration", "captureEpoch", "captureSequence", "capturePart", "viewerConnection",
       "session", "epoch", "generation", "focusRequest", "focusRevision", "geometry", "scope"}
  );
  InputCutoffBinding result;
  result.request = sequence(object, "request");
  const auto connection = sequence(object, "coreConnectionGeneration");
  require(connection <= UINT32_MAX, "Invalid cutoff Core connection");
  result.coreConnectionGeneration = static_cast<std::uint32_t>(connection);
  result.capture.epoch = sequence(object, "captureEpoch");
  result.capture.sequence = sequence(object, "captureSequence");
  const auto part = sequence(object, "capturePart");
  require(part <= maximumOriginalInputParts, "Invalid cutoff translation part");
  result.capture.part = static_cast<decltype(result.capture.part)>(part);
  result.viewerConnection = sequence(object, "viewerConnection");
  result.forwarding.owner = {sequence(object, "session"), sequence(object, "epoch")};
  result.forwarding.generation = sequence(object, "generation");
  result.forwarding.focusRequest = sequence(object, "focusRequest");
  result.forwarding.focusRevision = sequence(object, "focusRevision");
  result.forwarding.geometry = sequence(object, "geometry");
  require(object.value("scope") == QJsonValue(QStringLiteral("all")), "Cutoff requires exact All scope");
  result.forwarding.scope = ForwardingScope::All;
  require(validInputCutoffBinding(result), "Invalid viewport cutoff binding");
  return result;
}
QJsonArray pointsJson(const std::vector<GnomeViewportRequest::MapItem> &points, const InputCutoffBinding &origin)
{
  QJsonArray result;
  auto previous = origin;
  for (const auto &point : points) {
    require(continuesInputCutoffSeal(previous, point.cutoff), "Viewport batch changed its common lifetime");
    result.append(
        QJsonObject{
            {"request", QString::number(point.cutoff.request)},
            {"sequence", QString::number(point.cutoff.capture.sequence)},
            {"part", qint64(point.cutoff.capture.part)},
            {"x", point.x},
            {"y", point.y}
        }
    );
    previous = point.cutoff;
  }
  return result;
}
std::vector<GnomeViewportRequest::MapItem>
mapPoints(const QJsonObject &message, const InputCutoffBinding &origin, bool mapped)
{
  require(message.value("points").isArray(), "Viewport map points missing");
  const auto array = message.value("points").toArray();
  require(!array.empty() && array.size() <= 16, "Viewport map batch exceeds bound");
  std::vector<GnomeViewportRequest::MapItem> result;
  auto previous = origin;
  for (const auto &value : array) {
    require(value.isObject(), "Viewport map point is not an object");
    const auto point = value.toObject();
    fields(point, {"request", "sequence", "part", "x", "y"});
    auto binding = origin;
    binding.request = sequence(point, "request");
    binding.capture.sequence = sequence(point, "sequence");
    binding.capture.part =
        static_cast<decltype(binding.capture.part)>(integer(point, "part", maximumOriginalInputParts));
    require(continuesInputCutoffSeal(previous, binding), "Viewport map points changed or reordered their cutoff");
    result.push_back(
        {binding, number(point, "x", mapped ? 0 : -16777216, 16777216),
         number(point, "y", mapped ? 0 : -16777216, 16777216)}
    );
    previous = binding;
  }
  return result;
}
QJsonObject stateJson(const GnomeViewportState &s)
{
  return {
      {"connection", QString::number(s.connection)},
      {"session", QString::number(s.session)},
      {"epoch", QString::number(s.epoch)},
      {"geometry", QString::number(s.geometry)},
      {"focusRequest", QString::number(s.focusRequest)},
      {"focusRevision", QString::number(s.focusRevision)},
      {"surfaceIncarnation", QString::number(s.surfaceIncarnation)},
      {"viewportRevision", QString::number(s.viewportRevision)},
      {"peerFingerprint", s.peerFingerprint},
      {"windowAssociation", s.windowAssociation},
      {"nativeSurface", qint64(s.nativeSurface)},
      {"clientWidth", s.clientWidth},
      {"clientHeight", s.clientHeight},
      {"sourceWidth", s.sourceWidth},
      {"sourceHeight", s.sourceHeight},
      {"imageWidth", qint64(s.imageWidth)},
      {"imageHeight", qint64(s.imageHeight)},
      {"videoX", s.videoX},
      {"videoY", s.videoY},
      {"videoWidth", s.videoWidth},
      {"videoHeight", s.videoHeight},
      {"dpr", s.dpr},
      {"surfaceWidth", s.surfaceWidth},
      {"surfaceHeight", s.surfaceHeight},
      {"contentX", s.contentX},
      {"contentY", s.contentY},
      {"logicalScale", s.logicalScale}
  };
}
GnomeViewportState state(const QJsonObject &object)
{
  fields(
      object, {"connection",
               "session",
               "epoch",
               "geometry",
               "focusRequest",
               "focusRevision",
               "surfaceIncarnation",
               "viewportRevision",
               "peerFingerprint",
               "windowAssociation",
               "nativeSurface",
               "clientWidth",
               "clientHeight",
               "sourceWidth",
               "sourceHeight",
               "imageWidth",
               "imageHeight",
               "videoX",
               "videoY",
               "videoWidth",
               "videoHeight",
               "dpr",
               "surfaceWidth",
               "surfaceHeight",
               "contentX",
               "contentY",
               "logicalScale"}
  );
  GnomeViewportState s;
  s.connection = sequence(object, "connection");
  s.session = sequence(object, "session");
  s.epoch = sequence(object, "epoch");
  s.geometry = sequence(object, "geometry");
  s.focusRequest = sequence(object, "focusRequest");
  s.focusRevision = sequence(object, "focusRevision");
  s.surfaceIncarnation = sequence(object, "surfaceIncarnation");
  s.viewportRevision = sequence(object, "viewportRevision");
  s.peerFingerprint = object.value("peerFingerprint").toString();
  s.windowAssociation = object.value("windowAssociation").toString();
  const auto associationPrefix = QStringLiteral("org.deskflow.window.");
  require(
      s.windowAssociation.startsWith(associationPrefix) &&
          s.windowAssociation.size() == associationPrefix.size() + 64 &&
          std::all_of(
              s.windowAssociation.begin() + associationPrefix.size(), s.windowAssociation.end(),
              [](QChar c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }
          ),
      "Invalid per-window Wayland association"
  );
  require(
      s.peerFingerprint.size() == 64 &&
          std::all_of(
              s.peerFingerprint.begin(), s.peerFingerprint.end(),
              [](QChar c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }
          ) &&
          s.peerFingerprint != QString(64, '0'),
      "Invalid viewport peer fingerprint"
  );
  s.nativeSurface = integer(object, "nativeSurface", UINT32_MAX);
  constexpr double bound = 16777216;
  s.clientWidth = number(object, "clientWidth", 1, bound);
  s.clientHeight = number(object, "clientHeight", 1, bound);
  s.sourceWidth = number(object, "sourceWidth", 1, bound);
  s.sourceHeight = number(object, "sourceHeight", 1, bound);
  s.imageWidth = integer(object, "imageWidth", 16384);
  s.imageHeight = integer(object, "imageHeight", 16384);
  s.videoX = number(object, "videoX", 0, bound);
  s.videoY = number(object, "videoY", 0, bound);
  s.videoWidth = number(object, "videoWidth", 0.000001, bound);
  s.videoHeight = number(object, "videoHeight", 0.000001, bound);
  s.dpr = number(object, "dpr", 0.0625, 16);
  s.logicalScale = number(object, "logicalScale", 0.0625, 16);
  s.surfaceWidth = number(object, "surfaceWidth", 1, bound);
  s.surfaceHeight = number(object, "surfaceHeight", 1, bound);
  s.contentX = number(object, "contentX", 0, bound);
  s.contentY = number(object, "contentY", 0, bound);
  require(
      s.videoX + s.videoWidth <= s.clientWidth + 1e-6 && s.videoY + s.videoHeight <= s.clientHeight + 1e-6 &&
          // Qt rounds the native QSize after logical scaling. The retained
          // adapter still performs Qt's exact inverse at map time.
          s.contentX + s.clientWidth * s.logicalScale <= s.surfaceWidth + 0.500001 &&
          s.contentY + s.clientHeight * s.logicalScale <= s.surfaceHeight + 0.500001,
      "Viewport content extends outside its surface"
  );
  const double fit = std::min(s.clientWidth / s.imageWidth, s.clientHeight / s.imageHeight);
  require(
      std::abs(s.videoWidth - s.imageWidth * fit) < 1e-6 && std::abs(s.videoHeight - s.imageHeight * fit) < 1e-6 &&
          std::abs(s.videoX - (s.clientWidth - s.videoWidth) / 2) < 1e-6 &&
          std::abs(s.videoY - (s.clientHeight - s.videoHeight) / 2) < 1e-6,
      "Viewport is not the painted aspect-ratio fit"
  );
  return s;
}

QJsonObject preparationJson(const InputPreparationBinding &b)
{
  return {
      {"request", QString::number(b.request)},
      {"coreConnectionGeneration", QString::number(b.coreConnectionGeneration)},
      {"captureEpoch", QString::number(b.capture.epoch)},
      {"captureSequence", QString::number(b.capture.sequence)},
      {"capturePart", QString::number(b.capture.part)}
  };
}
InputPreparationBinding preparation(const QJsonObject &object)
{
  require(object.value("preparation").isObject(), "Missing preparation binding");
  const auto value = object.value("preparation").toObject();
  fields(value, {"request", "coreConnectionGeneration", "captureEpoch", "captureSequence", "capturePart"});
  const auto generation = sequence(value, "coreConnectionGeneration"), part = sequence(value, "capturePart");
  require(generation <= UINT32_MAX && part <= maximumOriginalInputParts, "Invalid preparation generation or part");
  InputPreparationBinding result{
      sequence(value, "request"),
      static_cast<std::uint32_t>(generation),
      {sequence(value, "captureEpoch"), sequence(value, "captureSequence"), static_cast<std::uint16_t>(part)}
  };
  require(validInputPreparationBinding(result), "Invalid preparation binding");
  return result;
}
QJsonObject preparationTargetJson(const GnomePreparationTarget &target)
{
  return {
      {"session", QString::number(target.session)},
      {"epoch", QString::number(target.epoch)},
      {"geometry", QString::number(target.geometry)},
      {"surfaceIncarnation", QString::number(target.surfaceIncarnation)},
      {"paintRevision", QString::number(target.paintRevision)},
      {"nativeSurface", qint64(target.nativeSurface)},
      {"windowAssociation", target.windowAssociation}
  };
}
GnomePreparationTarget preparationTarget(const QJsonObject &value)
{
  fields(
      value,
      {"session", "epoch", "geometry", "surfaceIncarnation", "paintRevision", "nativeSurface", "windowAssociation"}
  );
  GnomePreparationTarget result{
      sequence(value, "session"),
      sequence(value, "epoch"),
      sequence(value, "geometry"),
      sequence(value, "surfaceIncarnation"),
      sequence(value, "paintRevision"),
      integer(value, "nativeSurface", UINT32_MAX),
      value.value("windowAssociation").toString()
  };
  const auto prefix = QStringLiteral("org.deskflow.window.");
  require(
      result.windowAssociation.startsWith(prefix) && result.windowAssociation.size() == prefix.size() + 64 &&
          std::all_of(
              result.windowAssociation.begin() + prefix.size(), result.windowAssociation.end(),
              [](QChar c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }
          ),
      "Invalid preparation role challenge"
  );
  return result;
}
GnomePreparationTarget preparationTargetField(const QJsonObject &value)
{
  require(value.value("target").isObject(), "Missing preparation target");
  return preparationTarget(value.value("target").toObject());
}
QJsonObject inventoryJson(const GnomePreparationInventory &inventory)
{
  QJsonArray targets;
  for (const auto &target : inventory.targets)
    targets.append(preparationTargetJson(target));
  return {
      {"connection", QString::number(inventory.connection)},
      {"peerFingerprint", inventory.peerFingerprint},
      {"targets", targets}
  };
}
GnomePreparationInventory inventory(const QJsonObject &value)
{
  fields(value, {"connection", "peerFingerprint", "targets"});
  GnomePreparationInventory result;
  result.connection = sequence(value, "connection");
  result.peerFingerprint = value.value("peerFingerprint").toString();
  require(
      result.peerFingerprint.size() == 64 &&
          std::all_of(
              result.peerFingerprint.begin(), result.peerFingerprint.end(),
              [](QChar c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }
          ) &&
          result.peerFingerprint != QString(64, '0'),
      "Invalid preparation peer fingerprint"
  );
  require(value.value("targets").isArray(), "Missing preparation inventory");
  const auto targets = value.value("targets").toArray();
  require(!targets.isEmpty() && targets.size() <= 16, "Preparation inventory exceeds bound");
  QSet<QString> associations;
  std::set<std::uint64_t> sessions;
  for (const auto &target : targets) {
    require(target.isObject(), "Invalid preparation target entry");
    auto parsed = preparationTarget(target.toObject());
    require(
        !associations.contains(parsed.windowAssociation) && sessions.insert(parsed.session).second,
        "Duplicate preparation target"
    );
    associations.insert(parsed.windowAssociation);
    result.targets.push_back(std::move(parsed));
  }
  return result;
}
GnomeViewportRequest parsePreparationRequest(const QJsonObject &object, GnomeViewportOperation op)
{
  GnomeViewportRequest result;
  result.operation = op;
  result.request = sequence(object, "request");
  result.preparation = preparation(object);
  result.budgetMs = integer(object, "budgetMs", 500);
  QSet<QString> expected{"version", "operation", "request", "preparation", "budgetMs"};
  if (op != GnomeViewportOperation::PreparationInventory) {
    expected.insert("target");
    result.target = preparationTargetField(object);
  }
  if (op == GnomeViewportOperation::PreparationMap) {
    expected.insert("x");
    expected.insert("y");
    result.x = number(object, "x", -16777216, 16777216);
    result.y = number(object, "y", -16777216, 16777216);
  }
  if (op == GnomeViewportOperation::PreparationPromote) {
    expected.insert("cutoff");
    result.cutoff = cutoff(object);
    require(
        result.cutoff.request > result.preparation.request &&
            result.cutoff.coreConnectionGeneration == result.preparation.coreConnectionGeneration &&
            result.cutoff.capture == result.preparation.capture,
        "Promotion reply changed its original preparation capture"
    );
    require(
        result.cutoff.request > result.preparation.request && result.cutoff.capture == result.preparation.capture &&
            result.cutoff.coreConnectionGeneration == result.preparation.coreConnectionGeneration,
        "Preparation promotion changed capture"
    );
  }
  fields(object, expected);
  return result;
}
GnomeViewportReply parsePreparationReply(const QJsonObject &object, GnomeViewportOperation op)
{
  GnomeViewportReply result;
  result.operation = op;
  result.request = sequence(object, "request");
  result.preparation = preparation(object);
  require(object.value("ok").isBool(), "Preparation reply needs boolean result");
  result.ok = object.value("ok").toBool();
  QSet<QString> expected{"version", "operation", "request", "preparation", "ok"};
  if (op != GnomeViewportOperation::PreparationInventory) {
    expected.insert("target");
    result.target = preparationTargetField(object);
  }
  if (op == GnomeViewportOperation::PreparationPromote) {
    expected.insert("cutoff");
    result.cutoff = cutoff(object);
  }
  if (!result.ok) {
    expected.insert("reason");
    result.reason = reason(object);
  } else if (op == GnomeViewportOperation::PreparationInventory) {
    expected.insert("inventory");
    require(object.value("inventory").isObject(), "Missing preparation inventory result");
    result.inventory = inventory(object.value("inventory").toObject());
  } else if (op == GnomeViewportOperation::PreparationMap) {
    expected.insert("x");
    expected.insert("y");
    result.x = number(object, "x", 0, 16777216);
    result.y = number(object, "y", 0, 16777216);
  } else if (op == GnomeViewportOperation::PreparationFocus) {
    expected.insert("state");
    require(object.value("state").isObject(), "Missing preparation focus result");
    result.state = state(object.value("state").toObject());
    require(
        result.state.session == result.target.session && result.state.epoch == result.target.epoch &&
            result.state.geometry == result.target.geometry &&
            result.state.surfaceIncarnation == result.target.surfaceIncarnation &&
            result.state.nativeSurface == result.target.nativeSurface &&
            result.state.windowAssociation == result.target.windowAssociation,
        "Preparation focus changed target"
    );
  }
  fields(object, expected);
  return result;
}
} // namespace

GnomeViewportRequest parseGnomeViewportRequest(const QJsonObject &object)
{
  GnomeViewportRequest result;
  result.operation = operation(object);
  if (isGnomePreparationOperation(result.operation))
    return parsePreparationRequest(object, result.operation);
  result.request = sequence(object, "request");
  QSet<QString> expected{"version", "operation", "request"};
  if (result.operation == GnomeViewportOperation::Map || result.operation == GnomeViewportOperation::Check ||
      result.operation == GnomeViewportOperation::InputDrain || result.operation == GnomeViewportOperation::InputSeal ||
      result.operation == GnomeViewportOperation::InputMap ||
      result.operation == GnomeViewportOperation::InputMapBatch) {
    expected.insert("snapshot");
    result.snapshot = sequence(object, "snapshot");
    require(result.snapshot < result.request, "Map/check must follow its snapshot request");
  }
  if (result.operation == GnomeViewportOperation::InputDrain || result.operation == GnomeViewportOperation::InputSeal ||
      result.operation == GnomeViewportOperation::InputMap ||
      result.operation == GnomeViewportOperation::InputMapBatch) {
    expected.insert("cutoff");
    expected.insert("budgetMs");
    result.cutoff = cutoff(object);
    result.budgetMs = integer(object, "budgetMs", gnomeViewportLifetimeMs);
  }
  if (result.operation == GnomeViewportOperation::InputMap) {
    expected.insert("seal");
    result.seal = sequence(object, "seal");
    require(result.seal < result.cutoff.request, "Fresh input map must follow its prefix seal");
  }
  if (result.operation == GnomeViewportOperation::InputMapBatch) {
    expected.insert("points");
    result.points = mapPoints(object, result.cutoff, false);
  }
  if (result.operation == GnomeViewportOperation::Map || result.operation == GnomeViewportOperation::InputMap) {
    expected.insert("x");
    expected.insert("y");
    result.x = number(object, "x", -16777216, 16777216);
    result.y = number(object, "y", -16777216, 16777216);
  }
  require(
      result.operation != GnomeViewportOperation::Invalidated &&
          result.operation != GnomeViewportOperation::SealInvalidated,
      "Invalidation is not a request"
  );
  fields(object, expected);
  return result;
}
GnomeViewportReply parseGnomeViewportReply(const QJsonObject &object)
{
  GnomeViewportReply result;
  result.operation = operation(object);
  if (isGnomePreparationOperation(result.operation))
    return parsePreparationReply(object, result.operation);
  QSet<QString> expected{"version", "operation"};
  if (result.operation == GnomeViewportOperation::Invalidated ||
      result.operation == GnomeViewportOperation::SealInvalidated) {
    expected.insert("reason");
    if (result.operation == GnomeViewportOperation::Invalidated) {
      expected.insert("snapshot");
      result.snapshot = sequence(object, "snapshot");
    } else {
      expected.insert("cutoff");
      result.cutoff = cutoff(object);
    }
    result.reason = reason(object);
  } else {
    expected.insert("request");
    expected.insert("ok");
    result.request = sequence(object, "request");
    require(object.value("ok").isBool(), "Viewport reply needs boolean result");
    result.ok = object.value("ok").toBool();
    if (result.operation == GnomeViewportOperation::InputDrain ||
        result.operation == GnomeViewportOperation::InputSeal || result.operation == GnomeViewportOperation::InputMap ||
        result.operation == GnomeViewportOperation::InputMapBatch) {
      expected.insert("cutoff");
      expected.insert("snapshot");
      result.cutoff = cutoff(object);
      result.snapshot = sequence(object, "snapshot");
      require(result.snapshot < result.request, "Invalid input drain snapshot association");
    }
    if (result.operation == GnomeViewportOperation::InputMap) {
      expected.insert("seal");
      result.seal = sequence(object, "seal");
      require(result.seal < result.cutoff.request, "Invalid input map prefix seal");
    }
    if (!result.ok) {
      expected.insert("reason");
      result.reason = reason(object);
    } else if (result.operation == GnomeViewportOperation::Snapshot) {
      expected.insert("state");
      require(object.value("state").isObject(), "Viewport snapshot state missing");
      result.state = state(object.value("state").toObject());
      result.snapshot = result.request;
    } else {
      expected.insert("snapshot");
      result.snapshot = sequence(object, "snapshot");
      require(result.snapshot < result.request, "Invalid viewport snapshot association");
      if (result.operation == GnomeViewportOperation::Map || result.operation == GnomeViewportOperation::InputMap) {
        expected.insert("x");
        expected.insert("y");
        result.x = number(object, "x", 0, 16777216);
        result.y = number(object, "y", 0, 16777216);
      }
      if (result.operation == GnomeViewportOperation::InputMapBatch) {
        expected.insert("points");
        result.points = mapPoints(object, result.cutoff, true);
      }
    }
  }
  fields(object, expected);
  return result;
}
QJsonObject gnomeViewportJson(const GnomeViewportRequest &request)
{
  QJsonObject result{
      {"version", 1}, {"operation", operationName(request.operation)}, {"request", QString::number(request.request)}
  };
  if (isGnomePreparationOperation(request.operation)) {
    result.insert("preparation", preparationJson(request.preparation));
    result.insert("budgetMs", qint64(request.budgetMs));
    if (request.operation != GnomeViewportOperation::PreparationInventory)
      result.insert("target", preparationTargetJson(request.target));
    if (request.operation == GnomeViewportOperation::PreparationMap) {
      result.insert("x", request.x);
      result.insert("y", request.y);
    }
    if (request.operation == GnomeViewportOperation::PreparationPromote)
      result.insert("cutoff", cutoffJson(request.cutoff));
    (void)parseGnomeViewportRequest(result);
    return result;
  }
  if (request.operation == GnomeViewportOperation::Map || request.operation == GnomeViewportOperation::Check ||
      request.operation == GnomeViewportOperation::InputDrain ||
      request.operation == GnomeViewportOperation::InputSeal || request.operation == GnomeViewportOperation::InputMap ||
      request.operation == GnomeViewportOperation::InputMapBatch)
    result.insert("snapshot", QString::number(request.snapshot));
  if (request.operation == GnomeViewportOperation::InputDrain ||
      request.operation == GnomeViewportOperation::InputSeal || request.operation == GnomeViewportOperation::InputMap ||
      request.operation == GnomeViewportOperation::InputMapBatch) {
    require(validInputCutoffBinding(request.cutoff), "Invalid viewport cutoff binding");
    result.insert("cutoff", cutoffJson(request.cutoff));
    result.insert("budgetMs", qint64(request.budgetMs));
  }
  if (request.operation == GnomeViewportOperation::InputMap)
    result.insert("seal", QString::number(request.seal));
  if (request.operation == GnomeViewportOperation::InputMapBatch)
    result.insert("points", pointsJson(request.points, request.cutoff));
  if (request.operation == GnomeViewportOperation::Map || request.operation == GnomeViewportOperation::InputMap) {
    result.insert("x", request.x);
    result.insert("y", request.y);
  }
  (void)parseGnomeViewportRequest(result);
  return result;
}
QJsonObject gnomeViewportJson(const GnomeViewportReply &reply)
{
  QJsonObject result{{"version", 1}, {"operation", operationName(reply.operation)}};
  if (isGnomePreparationOperation(reply.operation)) {
    result.insert("preparation", preparationJson(reply.preparation));
    result.insert("request", QString::number(reply.request));
    result.insert("ok", reply.ok);
    if (reply.operation != GnomeViewportOperation::PreparationInventory)
      result.insert("target", preparationTargetJson(reply.target));
    if (reply.operation == GnomeViewportOperation::PreparationPromote)
      result.insert("cutoff", cutoffJson(reply.cutoff));
    if (!reply.ok)
      result.insert("reason", reply.reason);
    else if (reply.operation == GnomeViewportOperation::PreparationInventory)
      result.insert("inventory", inventoryJson(reply.inventory));
    else if (reply.operation == GnomeViewportOperation::PreparationMap) {
      result.insert("x", reply.x);
      result.insert("y", reply.y);
    } else if (reply.operation == GnomeViewportOperation::PreparationFocus)
      result.insert("state", stateJson(reply.state));
    (void)parseGnomeViewportReply(result);
    return result;
  }
  if (reply.operation == GnomeViewportOperation::Invalidated ||
      reply.operation == GnomeViewportOperation::SealInvalidated) {
    if (reply.operation == GnomeViewportOperation::Invalidated)
      result.insert("snapshot", QString::number(reply.snapshot));
    else
      result.insert("cutoff", cutoffJson(reply.cutoff));
    result.insert("reason", reply.reason);
  } else {
    result.insert("request", QString::number(reply.request));
    result.insert("ok", reply.ok);
    if (reply.operation == GnomeViewportOperation::InputDrain || reply.operation == GnomeViewportOperation::InputSeal ||
        reply.operation == GnomeViewportOperation::InputMap ||
        reply.operation == GnomeViewportOperation::InputMapBatch) {
      require(validInputCutoffBinding(reply.cutoff), "Invalid viewport cutoff binding");
      result.insert("cutoff", cutoffJson(reply.cutoff));
      result.insert("snapshot", QString::number(reply.snapshot));
    }
    if (reply.operation == GnomeViewportOperation::InputMap)
      result.insert("seal", QString::number(reply.seal));
    if (!reply.ok)
      result.insert("reason", reply.reason);
    else if (reply.operation == GnomeViewportOperation::Snapshot)
      result.insert("state", stateJson(reply.state));
    else {
      result.insert("snapshot", QString::number(reply.snapshot));
      if (reply.operation == GnomeViewportOperation::Map || reply.operation == GnomeViewportOperation::InputMap) {
        result.insert("x", reply.x);
        result.insert("y", reply.y);
      }
      if (reply.operation == GnomeViewportOperation::InputMapBatch)
        result.insert("points", pointsJson(reply.points, reply.cutoff));
    }
  }
  (void)parseGnomeViewportReply(result);
  return result;
}
QByteArray gnomeViewportFrame(const QJsonObject &object)
{
  const auto body = QJsonDocument(object).toJson(QJsonDocument::Compact);
  require(!body.isEmpty() && body.size() <= gnomeViewportMaximumPayload, "Viewport payload exceeds limit");
  QByteArray result(4, '\0');
  qToBigEndian<quint32>(static_cast<quint32>(body.size()), result.data());
  return result + body;
}
void GnomeViewportDecoder::append(const QByteArray &bytes)
{
  if (m_failed || bytes.size() > 2 * (gnomeViewportMaximumPayload + 4) - m_bytes.size()) {
    m_failed = true;
    m_bytes.clear();
    throw std::runtime_error("Viewport input exceeds bound or decoder is closed");
  }
  m_bytes += bytes;
}
std::optional<QJsonObject> GnomeViewportDecoder::take()
{
  try {
    require(!m_failed, "Viewport decoder is closed");
    if (m_bytes.size() < 4)
      return {};
    const auto length = qFromBigEndian<quint32>(m_bytes.constData());
    require(length && length <= gnomeViewportMaximumPayload, "Invalid viewport frame length");
    if (m_bytes.size() < qsizetype(length) + 4)
      return {};
    const auto body = m_bytes.mid(4, length);
    m_bytes.remove(0, qsizetype(length) + 4);
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(body, &error);
    require(error.error == QJsonParseError::NoError && document.isObject(), "Invalid viewport JSON");
    requireUniqueJsonKeys(body);
    return document.object();
  } catch (...) {
    m_failed = true;
    m_bytes.clear();
    throw;
  }
}
} // namespace deskflow::windowshare
