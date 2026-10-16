# SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
# SPDX-License-Identifier: MIT

if (WIN32)
  create_test(
    NAME MSWindowsClipboardTests
    DEPENDS platform
    LIBS base arch
    SOURCE MSWindowsClipboardTests.cpp
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/platform"
  )
elseif(APPLE)
  create_test(
    NAME OSXMainThreadTests
    DEPENDS app
    LIBS platform base arch
    SOURCE OSXMainThreadTests.cpp
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/platform"
  )
  set_tests_properties(OSXMainThreadTests PROPERTIES TIMEOUT 15)
  create_test(
    NAME OSXClipboardTests
    DEPENDS platform
    LIBS base arch
    SOURCE OSXClipboardTests.cpp
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/platform"
  )
  create_test(
    NAME OSXKeyStateTests
    DEPENDS platform
    LIBS base arch
    SOURCE OSXKeyStateTests.cpp
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/platform"
  )
elseif(UNIX)
  if(LIBEI_FOUND)
    create_test(
      NAME EiKeyStateTests
      DEPENDS platform
      LIBS base arch
      SOURCE EiKeyStateTests.cpp
      WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/platform"
    )
    # Compile the producer's view inside the real platform target. The test
    # consumer must independently receive every layout-affecting definition.
    target_sources(platform PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/EiComputerTestLayout.cpp")
    create_test(
      NAME EiComputerTests
      DEPENDS platform
      LIBS base arch app
      SOURCE EiComputerTests.cpp
      WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/platform"
    )
    # A private EIS server exercises the real libei queue without a desktop.
    pkg_check_modules(LIBEIS REQUIRED IMPORTED_TARGET libeis-1.0)
    target_link_libraries(EiComputerTests PkgConfig::LIBEIS)
    set_tests_properties(EiComputerTests PROPERTIES TIMEOUT 15)
  endif()

  if (BUILD_X11_SUPPORT)
    create_test(
      NAME XWindowsClipboardTests
      DEPENDS platform
      LIBS base arch
      SOURCE XWindowsClipboardTests.cpp
      WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/platform"
    )
  endif()
endif()
