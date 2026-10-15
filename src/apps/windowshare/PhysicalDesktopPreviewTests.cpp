/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "PhysicalDesktopPreview.h"
#include <QApplication>
#include <QTemporaryDir>
#include <cstdio>
#include <stdexcept>
#include <thread>

using namespace deskflow::windowshare;
namespace {
void check(bool value, const char *reason)
{
  if (!value)
    throw std::runtime_error(reason);
}
void writeFrame(const QString &path, quint32 sequence, QSize size = {1280, 720}, bool corrupt = false)
{
  QImage image(size, QImage::Format_RGB32);
  image.fill(Qt::blue);
  QByteArray frame("DFP1");
  char encoded[4];
  qToBigEndian(sequence, encoded);
  frame.append(encoded, 4);
  QBuffer png;
  png.open(QIODevice::WriteOnly);
  check(image.save(&png, "PNG"), "PNG creation failed");
  frame += corrupt ? QByteArray("invalid PNG") : png.data();
  QSaveFile file(path);
  check(file.open(QIODevice::WriteOnly), "frame open failed");
  file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  check(file.write(frame) == frame.size() && file.commit(), "frame publication failed");
}
} // namespace
int main(int argc, char **argv)
{
  QApplication app(argc, argv);
  try {
    QTemporaryDir directory;
    const auto path = directory.filePath("frame"), receipt = directory.filePath("receipt");
    PhysicalDesktopPreview preview(path, receipt, false);
    preview.refresh();
    check(!preview.fresh() && preview.sequence() == 0, "absent frame looked live");
    writeFrame(path, 1);
    preview.refresh();
    check(preview.fresh() && preview.sequence() == 1 && !preview.isVisible(), "real PNG was not decoded offscreen");
    std::this_thread::sleep_for(std::chrono::milliseconds(2050));
    preview.refresh();
    check(!preview.fresh(), "rereading the same file renewed a stale image");
    writeFrame(path, 2);
    preview.refresh();
    check(preview.fresh(), "new valid image did not restore live preview");
    writeFrame(path, 1);
    preview.refresh();
    check(!preview.fresh(), "replayed image was accepted");
    for (const auto invalid : {QSize(1, 1), QSize(4096, 4096)}) {
      PhysicalDesktopPreview malformed(path, receipt, false);
      writeFrame(path, 1, invalid);
      malformed.refresh();
      check(!malformed.fresh(), "unexpected image dimensions accepted");
    }
    PhysicalDesktopPreview malformed(path, receipt, false);
    writeFrame(path, 1, {1280, 720}, true);
    malformed.refresh();
    check(!malformed.fresh(), "invalid PNG accepted");
    std::puts("Real PNG decoding, stale/replayed frames and invalid image rejection passed without a visible window");
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  }
}
