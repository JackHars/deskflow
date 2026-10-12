/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "GnomeViewportProtocol.h"
#include "UniqueJson.h"
#include <QJsonDocument>
#include <QtEndian>
#include <algorithm>
#include <charconv>
#include <cmath>
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
        GnomeViewportOperation::Invalidated})
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
  fields(object, {"connection",         "session",          "epoch",
                  "geometry",           "focusRequest",     "focusRevision",
                  "surfaceIncarnation", "viewportRevision", "peerFingerprint",
                  "nativeSurface",      "clientWidth",      "clientHeight",
                  "sourceWidth",        "sourceHeight",     "imageWidth",
                  "imageHeight",        "videoX",           "videoY",
                  "videoWidth",         "videoHeight",      "dpr",
                  "surfaceWidth",       "surfaceHeight",    "contentX",
                  "contentY",           "logicalScale"});
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
} // namespace

GnomeViewportRequest parseGnomeViewportRequest(const QJsonObject &object)
{
  GnomeViewportRequest result;
  result.operation = operation(object);
  result.request = sequence(object, "request");
  QSet<QString> expected{"version", "operation", "request"};
  if (result.operation == GnomeViewportOperation::Map || result.operation == GnomeViewportOperation::Check) {
    expected.insert("snapshot");
    result.snapshot = sequence(object, "snapshot");
    require(result.snapshot < result.request, "Map/check must follow its snapshot request");
  }
  if (result.operation == GnomeViewportOperation::Map) {
    expected.insert("x");
    expected.insert("y");
    result.x = number(object, "x", -16777216, 16777216);
    result.y = number(object, "y", -16777216, 16777216);
  }
  require(result.operation != GnomeViewportOperation::Invalidated, "Invalidated is not a request");
  fields(object, expected);
  return result;
}
GnomeViewportReply parseGnomeViewportReply(const QJsonObject &object)
{
  GnomeViewportReply result;
  result.operation = operation(object);
  QSet<QString> expected{"version", "operation"};
  if (result.operation == GnomeViewportOperation::Invalidated) {
    expected.insert("snapshot");
    expected.insert("reason");
    result.snapshot = sequence(object, "snapshot");
    result.reason = reason(object);
  } else {
    expected.insert("request");
    expected.insert("ok");
    result.request = sequence(object, "request");
    require(object.value("ok").isBool(), "Viewport reply needs boolean result");
    result.ok = object.value("ok").toBool();
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
      if (result.operation == GnomeViewportOperation::Map) {
        expected.insert("x");
        expected.insert("y");
        result.x = number(object, "x", 0, 16777216);
        result.y = number(object, "y", 0, 16777216);
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
  if (request.operation == GnomeViewportOperation::Map || request.operation == GnomeViewportOperation::Check)
    result.insert("snapshot", QString::number(request.snapshot));
  if (request.operation == GnomeViewportOperation::Map) {
    result.insert("x", request.x);
    result.insert("y", request.y);
  }
  (void)parseGnomeViewportRequest(result);
  return result;
}
QJsonObject gnomeViewportJson(const GnomeViewportReply &reply)
{
  QJsonObject result{{"version", 1}, {"operation", operationName(reply.operation)}};
  if (reply.operation == GnomeViewportOperation::Invalidated) {
    result.insert("snapshot", QString::number(reply.snapshot));
    result.insert("reason", reply.reason);
  } else {
    result.insert("request", QString::number(reply.request));
    result.insert("ok", reply.ok);
    if (!reply.ok)
      result.insert("reason", reply.reason);
    else if (reply.operation == GnomeViewportOperation::Snapshot)
      result.insert("state", stateJson(reply.state));
    else {
      result.insert("snapshot", QString::number(reply.snapshot));
      if (reply.operation == GnomeViewportOperation::Map) {
        result.insert("x", reply.x);
        result.insert("y", reply.y);
      }
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
