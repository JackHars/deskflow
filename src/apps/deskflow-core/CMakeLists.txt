# SPDX-FileCopyrightText: (C) 2024 - 2025 Chris Rizzitello <sithlord48@gmail.com>
# SPDX-FileCopyrightText: (C) 2012 - 2024 Symless Ltd
# SPDX-FileCopyrightText: (C) 2009 - 2012 Nick Bolton
# SPDX-License-Identifier: MIT

set(target ${CMAKE_PROJECT_NAME}-core)

add_executable(${target}
  "${target}.cpp"
  CoreArgs.h
  CoreArgParser.h
  CoreArgParser.cpp
)

if(WIN32)
  # Generate rc file
  set(EXE_DESCRIPTION "${CMAKE_PROJECT_PROPER_NAME} combined server and client application")
  set(EXE_ICON "IDI_DESKFLOW ICON DISCARDABLE \"${CMAKE_SOURCE_DIR}/src/apps/res/deskflow.ico\"")
  configure_file(${CMAKE_SOURCE_DIR}/src/apps/res/windows.rc.in ${target}.rc)

  target_sources(${target} PRIVATE
    ${target}.exe.manifest
    ${CMAKE_SOURCE_DIR}/src/apps/res/deskflow.ico
    ${CMAKE_CURRENT_BINARY_DIR}/${target}.rc
  )
endif()

target_link_libraries(
  ${target}
  arch
  base
  client
  io
  mt
  net
  platform
  server
  app
  Qt6::Widgets
  ${libs})

if(BUILD_WINDOWSHARE AND (APPLE OR CMAKE_SYSTEM_NAME STREQUAL "Linux"))
  if(APPLE)
    set(windowshare_core_definition WINDOWSHARE_CORE_COMPANION=1)
    target_link_libraries(${target} windowshare-local-input)
  else()
    set(windowshare_core_definition WINDOWSHARE_GNOME_PROVIDER=1)
    if(BUILD_X11_SUPPORT)
      list(APPEND windowshare_core_definition WINDOWSHARE_X11_OBSERVER=1)
    endif()
  endif()
  target_compile_definitions(${target} PRIVATE ${windowshare_core_definition})
  add_executable(windowshare-core-args-tests CoreArgParserTests.cpp CoreArgParser.cpp)
  target_compile_definitions(windowshare-core-args-tests PRIVATE ${windowshare_core_definition})
  target_link_libraries(windowshare-core-args-tests PRIVATE common)
endif()

install(
  TARGETS ${target}
  RUNTIME_DEPENDENCY_SET coreDeps
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(BUILD_OSX_BUNDLE)
  set_target_properties(${target} PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "@loader_path/../Libraries;@loader_path/../Frameworks"
    RUNTIME_OUTPUT_DIRECTORY $<TARGET_BUNDLE_CONTENT_DIR:${CMAKE_PROJECT_PROPER_NAME}>/MacOS
    XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME YES
  )
  if (APPLE_CODESIGN_DEV)
    configure_mac_codesign(${target})
  endif()
elseif (WIN32)
  install(RUNTIME_DEPENDENCY_SET coreDeps
    ${WIN32_RUNTIME_DEPENDENCY_SET_ARGS}
    PRE_EXCLUDE_REGEXES ${WIN32_PRE_EXCLUDE_REGEXES}
    POST_EXCLUDE_REGEXES ${WIN32_POST_EXCLUDE_REGEXES}
    RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )
else()
  generate_app_man(${target} "${CMAKE_PROJECT_DESCRIPTION}")
endif()
