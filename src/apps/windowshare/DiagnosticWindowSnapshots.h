/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include <QJsonObject>

namespace deskflow::windowshare {
struct DiagnosticWindowObservation
{
  QJsonObject frame;
  bool onScreen = false;
};

// Worker-owned diagnostic state. The caller supplies immutable UI values and
// fresh WindowServer observations of the exact owned IDs. A mismatched frame
// remains unsettled; moving queries off AppKit must not manufacture readiness.
class DiagnosticWindowSnapshots
{
public:
  QJsonObject
  complete(QJsonObject value, const DiagnosticWindowObservation &source, const DiagnosticWindowObservation &control)
  {
    const auto observation = [](QJsonObject ui, const DiagnosticWindowObservation &native) {
      ui["geometrySettled"] = !native.frame.isEmpty() && native.frame == ui["qtFrame"].toObject();
      ui["onScreen"] = native.onScreen;
      return ui;
    };
    const auto visible = [](const QJsonObject &observed) {
      return observed["geometrySettled"].toBool() && observed["onScreen"].toBool() && observed["qtVisible"].toBool() &&
             observed["nativeVisible"].toBool() && !observed["miniaturized"].toBool();
    };
    const auto sourceState = observation(value["sourceObservation"].toObject(), source);
    const auto controlState = observation(value["controlObservation"].toObject(), control);
    if (m_sourceOriginal.isEmpty() && sourceState["geometrySettled"].toBool())
      m_sourceOriginal = source.frame;
    if (m_controlOriginal.isEmpty() && controlState["geometrySettled"].toBool())
      m_controlOriginal = control.frame;
    value["sourceObservation"] = sourceState;
    value["controlObservation"] = controlState;
    value["sourceVisible"] = visible(sourceState);
    value["controlVisible"] = visible(controlState);
    value["frame"] = source.frame;
    value["controlFrame"] = control.frame;
    value["originalFrame"] = m_sourceOriginal;
    value["controlOriginalFrame"] = m_controlOriginal;
    value["ready"] = !m_sourceOriginal.isEmpty() && sourceState["geometrySettled"].toBool();
    if (source.frame.isEmpty())
      value["windowId"] = value["sourceWindowId"] = 0;
    return value;
  }

private:
  QJsonObject m_sourceOriginal, m_controlOriginal;
};
} // namespace deskflow::windowshare
