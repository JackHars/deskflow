/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include <iostream>
#include <windowshare/LifecycleDiagnostics.h>

int main(int argc, char **argv)
{
  QCoreApplication app(argc, argv);
  const bool bounded = app.arguments().contains("--bounded");
  const int records = bounded ? 10000 : 64;
  for (int i = 0; i < records; ++i)
    deskflow::windowshare::lifecycleDiagnostic(
        "Diagnostic fixture", "record", "observed", "No native input or desktop access",
        {{"index", i}, {"padding", QString(bounded ? 2048 : 0, 'x')}}
    );
  std::cout << "producer-complete\n" << std::flush;
}
