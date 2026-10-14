/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "WindowFamily.h"
#include <QCoreApplication>
#include <QTextStream>
#include <stdexcept>
using namespace deskflow::windowshare;
namespace {
void check(bool condition, const char *message)
{
  if (!condition)
    throw std::runtime_error(message);
}
template <class F> void rejects(F call)
{
  bool rejected = false;
  try {
    call();
  } catch (const std::runtime_error &) {
    rejected = true;
  }
  check(rejected, "Invalid family authority was accepted");
}
} // namespace
int main(int argc, char **argv)
{
  QCoreApplication app(argc, argv);
  try {
    WindowFamily family{{1, 2}, 1, WindowFamilyRole::Sheet, {-50, 20}};
    const auto wire = windowFamilyJson(family);
    check(parseWindowFamily({{"family", wire}}) == family, "Family round trip changed identity");
    check(!parseWindowFamily({}), "Ordinary window became attached");
    for (const auto field : {"parentSession", "parentEpoch", "relation"})
      for (const auto &value :
           {QJsonValue(0), QJsonValue(-1), QJsonValue(1.5), QJsonValue("1"), QJsonValue(9007199254740992.)}) {
        auto bad = wire;
        bad[field] = value;
        rejects([&] { parseWindowFamily({{"family", bad}}); });
      }
    for (const auto &change :
         {QJsonObject{{"role", "window"}}, QJsonObject{{"x", 65537}}, QJsonObject{{"y", "0"}},
          QJsonObject{{"nativeWindow", 123}}}) {
      auto bad = wire;
      for (auto it = change.begin(); it != change.end(); ++it)
        bad.insert(it.key(), it.value());
      rejects([&] { parseWindowFamily({{"family", bad}}); });
    }
    check(peerSupportsWindowFamily({{"capabilities", windowFamilyCapabilities({})}}), "Family capability missing");
    check(
        !peerSupportsWindowFamily({{"capabilities", windowFamilyCapabilities({}, false)}}),
        "Legacy peer gained family capability"
    );
    WindowFamilies graph;
    rejects([&] { graph.open({3, 4}, family); });
    graph.open({1, 2}, {});
    graph.open({9, 10}, {});
    graph.open({3, 4}, family);
    check(graph.blocked({1, 2}) && !graph.blocked({1, 3}), "Modal child blocked the wrong epoch");
    family.offset = {100, 200};
    graph.open({3, 4}, family);
    auto menu = WindowFamily{{3, 4}, 2, WindowFamilyRole::Menu, {2, 3}};
    graph.open({5, 6}, menu);
    check(graph.root({5, 6}) == Owner{1, 2}, "Nested menu lost its recovery owner");
    auto changed = family;
    changed.parent = {9, 10};
    rejects([&] { graph.open({3, 4}, changed); });
    changed = family;
    changed.role = WindowFamilyRole::Drawer;
    rejects([&] { graph.open({3, 4}, changed); });
    rejects([&] { graph.open({3, 5}, family); });
    rejects([&] { graph.open({1, 2}, menu); });
    check(graph.close({1, 3}).empty(), "Stale close removed current family");
    check(graph.close({3, 4}) == std::vector<Owner>{{5, 6}, {3, 4}}, "Child subtree did not retire in order");
    check(!graph.blocked({1, 2}) && graph.root({9, 10}) == Owner{9, 10}, "Dismissal affected another family");
    rejects([&] { graph.open({7, 8}, family); });
    family.relation = 3;
    graph.open({7, 8}, family);
    check(graph.close({1, 2}).size() == 2, "Parent close leaked a child");
    graph.clear();
    graph.open({1, 2}, {});
    family.relation = 1;
    graph.open({3, 4}, family);
    graph.clear();
    graph.open({1, 2}, {});
    graph.open({3, 4}, WindowFamily{{1, 2}, 1, WindowFamilyRole::Drawer, {}});
    graph.open({5, 6}, WindowFamily{{3, 4}, 2, WindowFamilyRole::Menu, {}});
    check(graph.blocked({1, 2}) && graph.blocked({3, 4}), "Nested modal did not block every ancestor");
    graph.clear();
    for (unsigned i = 1; i <= 16; ++i)
      graph.open({i, 1}, {});
    rejects([&] { graph.open({17, 1}, {}); });
    Session root, child;
    root.id = 1;
    root.journaled = true;
    root.prepare(2, {0, 0, 640, 480}, {0, 0, 640, 480});
    root.frameReady(2);
    child.id = 3;
    rejects([&] { child.prepareAttached(4, {0, 0, 320, 240}, root); });
    root.commit(2);
    child.prepareAttached(4, {0, 0, 320, 240}, root);
    rejects([&] { child.commitAttached(4, root); });
    child.frameReady(4);
    rejects([&] { child.commitAttached(3, root); });
    root.journaled = false;
    rejects([&] { child.commitAttached(4, root); });
    root.journaled = true;
    child.commitAttached(4, root);
    check(child.phase == Phase::Remote && !child.journaled, "Child gained an independent recovery record");
    QTextStream(stdout) << "PASS: strict family metadata, immutable bounded ancestry, epoch fencing, relation replay, "
                           "subtree retirement and shared recovery\n";
    return 0;
  } catch (const std::exception &error) {
    QTextStream(stderr) << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
