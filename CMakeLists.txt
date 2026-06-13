cmake_minimum_required(VERSION 3.20)

set(KEEBTYPE_VERSION "0.1.0" CACHE STRING "Keebtype package version")

project(keebtype
  VERSION ${KEEBTYPE_VERSION}
  DESCRIPTION "Tray app that plays keyboard switch sounds while typing"
  LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

option(KEEBTYPE_BUILD_TESTS "Build keebtype unit tests" ON)

include(FetchContent)

FetchContent_Declare(
  nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
  URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

set(MINIAUDIO_INSTALL OFF CACHE BOOL "" FORCE)
set(MINIAUDIO_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(MINIAUDIO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MINIAUDIO_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(MINIAUDIO_NO_EXTRA_NODES ON CACHE BOOL "" FORCE)
set(MINIAUDIO_NO_LIBVORBIS ON CACHE BOOL "" FORCE)
set(MINIAUDIO_NO_LIBOPUS ON CACHE BOOL "" FORCE)

FetchContent_Declare(
  miniaudio_src
  GIT_REPOSITORY https://github.com/mackron/miniaudio.git
  GIT_TAG 9634bedb5b5a2ca38c1ee7108a9358a4e233f14d
)
FetchContent_MakeAvailable(miniaudio_src)

FetchContent_Declare(
  stb_src
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG 31c1ad37456438565541f4919958214b6e762fb4
)
FetchContent_MakeAvailable(stb_src)

add_library(stb_vorbis STATIC
  src/audio/stb_vorbis_impl.c
)
target_include_directories(stb_vorbis PRIVATE
  ${stb_src_SOURCE_DIR}
)

add_library(keebtype_core STATIC
  src/audio/audio_engine.cpp
  src/audio/mixer.cpp
  src/platform/paths.cpp
  src/soundpack/soundpack.cpp
)

target_include_directories(keebtype_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(keebtype_core PUBLIC
  nlohmann_json::nlohmann_json
)

target_link_libraries(keebtype_core PRIVATE
  miniaudio
  stb_vorbis
)

add_executable(keebtype
  src/main.cpp
)

target_link_libraries(keebtype PRIVATE
  keebtype_core
)

if(APPLE)
  enable_language(OBJCXX)
  set_target_properties(keebtype PROPERTIES
    MACOSX_BUNDLE ON
    OUTPUT_NAME "Keebtype"
    MACOSX_BUNDLE_BUNDLE_NAME "Keebtype"
    MACOSX_BUNDLE_GUI_IDENTIFIER "io.keebtype.Keebtype"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
  )
  target_sources(keebtype PRIVATE
    src/input/input_monitor_mac.mm
    src/tray/tray_mac.mm
  )
  set_source_files_properties(
    src/input/input_monitor_mac.mm
    src/tray/tray_mac.mm
    PROPERTIES COMPILE_FLAGS "-fobjc-arc"
  )
  target_link_libraries(keebtype PRIVATE
    "-framework AppKit"
    "-framework ApplicationServices"
    "-framework CoreFoundation"
    "-framework CoreAudio"
    "-framework AudioToolbox"
    "-framework AudioUnit"
  )
  add_custom_command(TARGET keebtype POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:keebtype>/Contents/Resources"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CMAKE_CURRENT_SOURCE_DIR}/soundpacks"
      "$<TARGET_BUNDLE_DIR:keebtype>/Contents/Resources/soundpacks"
    COMMAND ${CMAKE_COMMAND} -E rm -f
      "$<TARGET_BUNDLE_DIR:keebtype>/Contents/Resources/soundpacks/.DS_Store"
      "$<TARGET_BUNDLE_DIR:keebtype>/Contents/Resources/soundpacks/cherrymx-brown-abs/.DS_Store"
    COMMAND /usr/bin/xattr -cr "$<TARGET_BUNDLE_DIR:keebtype>"
    COMMAND /usr/bin/codesign --force --deep --sign - "$<TARGET_BUNDLE_DIR:keebtype>"
  )
elseif(WIN32)
  target_sources(keebtype PRIVATE
    src/input/input_monitor_win.cpp
    src/tray/tray_win.cpp
  )
  target_link_libraries(keebtype PRIVATE
    user32
    shell32
    gdi32
  )
else()
  target_sources(keebtype PRIVATE
    src/input/input_monitor_linux.cpp
    src/tray/tray_linux.cpp
  )
  find_package(Threads REQUIRED)
  target_link_libraries(keebtype PRIVATE
    Threads::Threads
    dl
    m
  )
endif()

install(TARGETS keebtype
  BUNDLE DESTINATION .
  RUNTIME DESTINATION .
)

if(NOT APPLE)
  install(DIRECTORY soundpacks
    DESTINATION .
    PATTERN ".DS_Store" EXCLUDE
  )
endif()

if(KEEBTYPE_BUILD_TESTS)
  enable_testing()
  add_executable(keebtype_tests
    tests/test_main.cpp
  )
  target_link_libraries(keebtype_tests PRIVATE
    keebtype_core
  )
  target_compile_definitions(keebtype_tests PRIVATE
    KEEBTYPE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
  )
  add_test(NAME keebtype_tests COMMAND keebtype_tests)
endif()

set(CPACK_PACKAGE_NAME "Keebtype")
set(CPACK_PACKAGE_VENDOR "David Chandra")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Keebtype")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CPACK_VERBATIM_VARIABLES YES)

if(APPLE)
  set(CPACK_GENERATOR "DragNDrop")
  set(CPACK_PACKAGE_FILE_NAME "Keebtype-${PROJECT_VERSION}-macos-arm64")
  set(CPACK_DMG_VOLUME_NAME "Keebtype ${PROJECT_VERSION}")
  set(CPACK_DMG_FORMAT "UDZO")
elseif(WIN32)
  set(CPACK_GENERATOR "NSIS")
  set(CPACK_PACKAGE_FILE_NAME "Keebtype-${PROJECT_VERSION}-windows-x64-setup")
  set(CPACK_NSIS_DISPLAY_NAME "Keebtype")
  set(CPACK_NSIS_PACKAGE_NAME "Keebtype")
  set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
  set(CPACK_NSIS_EXECUTABLES_DIRECTORY ".")
  set(CPACK_PACKAGE_EXECUTABLES "keebtype" "Keebtype")
endif()

include(CPack)
