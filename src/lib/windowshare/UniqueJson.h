/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <stdexcept>
#include <vector>

namespace deskflow::windowshare {

// Call only after QJsonDocument has validated syntax. Qt otherwise silently
// retains the last occurrence of an object key, including escaped equivalents.
inline void requireUniqueJsonKeys(const QByteArray &json)
{
  std::vector<QSet<QString>> objects;
  for (qsizetype i = 0; i < json.size(); ++i) {
    const auto c = json[i];
    if (c == '{')
      objects.emplace_back();
    else if (c == '}')
      objects.pop_back();
    else if (c == '"') {
      const auto start = i++;
      for (; i < json.size() && json[i] != '"'; ++i)
        if (json[i] == '\\')
          ++i;
      auto next = i + 1;
      while (next < json.size() && (json[next] == ' ' || json[next] == '\t' || json[next] == '\r' || json[next] == '\n')
      )
        ++next;
      if (!objects.empty() && next < json.size() && json[next] == ':') {
        const auto decoded = QJsonDocument::fromJson('[' + json.mid(start, i - start + 1) + ']');
        const auto key = decoded.array().first().toString();
        auto &keys = objects.back();
        if (keys.contains(key))
          throw std::runtime_error("Duplicate WindowShare configuration field");
        keys.insert(key);
      }
    }
  }
}

} // namespace deskflow::windowshare
