# SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
# SPDX-License-Identifier: MIT

if(WIN32)
  set(extra_libs version)
endif()

create_test(
  NAME SecureUtilsTests
  DEPENDS net
  LIBS base arch mt io ${extra_libs}
  SOURCE SecureUtilsTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/net"
)

create_test(
  NAME FingerprintTests
  DEPENDS net
  LIBS base arch mt io ${extra_libs}
  SOURCE FingerprintTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/net"
)

create_test(
  NAME FingerprintDatabaseTests
  DEPENDS net
  LIBS base arch mt io ${extra_libs}
  SOURCE FingerprintDatabaseTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/net"
)

if(BUILD_WINDOWSHARE)
  create_test(
    NAME SecurePeerIdentityTests
    DEPENDS net
    LIBS base arch mt io app ${extra_libs}
    SOURCE SecurePeerIdentityTests.cpp
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/net"
  )
  set_tests_properties(SecurePeerIdentityTests PROPERTIES TIMEOUT 60)
endif()

