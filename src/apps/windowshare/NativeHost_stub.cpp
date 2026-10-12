/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "NativeHost.h"
#include <stdexcept>
namespace deskflow::windowshare {
struct NativeHost::Impl {};
NativeHost::NativeHost(QString, QObject *parent) : QObject(parent) { throw std::runtime_error("Window hosting requires macOS 13 or later; use --connect on Ubuntu"); }
NativeHost::~NativeHost() = default;
void NativeHost::refresh() {}
void NativeHost::exportWindow(std::uint32_t) {}
void NativeHost::ready(std::uint64_t, std::uint64_t) {}
void NativeHost::input(std::uint64_t, std::uint64_t, const QJsonObject &) {}
void NativeHost::resizeWindow(std::uint64_t, std::uint64_t, int, int) {}
void NativeHost::restore(std::uint64_t) {}
void NativeHost::restoreAll() {}
void NativeHost::releaseAll() {}
}
