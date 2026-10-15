/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>
#include <memory>

namespace deskflow::windowshare {

// One decoder belongs to one stream and is used from one worker thread only.
// Packets are complete Annex B access units, including SPS/PPS at keyframes.
class VideoDecoder final
{
public:
  struct Result
  {
    QImage image;
    QString error;
    bool hardwareFrame = false;
    bool reusedTransferBuffer = false;
  };

  explicit VideoDecoder(bool preferHardware = true);
  ~VideoDecoder();
  VideoDecoder(const VideoDecoder &) = delete;
  VideoDecoder &operator=(const VideoDecoder &) = delete;
  Result decode(const QByteArray &accessUnit);
  void reset();
  QString backend() const;

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

} // namespace deskflow::windowshare
