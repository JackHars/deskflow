# SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
# SPDX-License-Identifier: MIT

if(WIN32)
  set(extra_libs version app mt net)
endif()
if(BUILD_WINDOWSHARE)
  set(windowshare_test_libs io mt net platform app)
endif()

create_test(
  NAME ServerConfigTests
  DEPENDS server
  LIBS base arch ${extra_libs}
  SOURCE ServerConfigTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/server"
)

create_test(
  NAME ServerTests
  DEPENDS server
  LIBS base arch ${windowshare_test_libs} ${extra_libs}
  SOURCE ServerTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/server"
)

create_test(
  NAME ClientProxyTests
  DEPENDS server
  LIBS base arch io mt net platform app ${extra_libs}
  SOURCE ClientProxyTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/server"
)

if(BUILD_WINDOWSHARE AND TARGET windowshare-local-input)
  create_test(
    NAME OriginalInputCoordinatorTests
    DEPENDS server
    LIBS base arch io mt net platform app windowshare-local-input ${extra_libs}
    SOURCE OriginalInputCoordinatorTests.cpp
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/server"
  )
  target_compile_features(OriginalInputCoordinatorTests PRIVATE cxx_std_20)
  set_tests_properties(OriginalInputCoordinatorTests PROPERTIES TIMEOUT 60)
endif()
