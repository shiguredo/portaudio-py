cmake_minimum_required(VERSION 4.2)
project(portaudio_py LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# プラットフォームの検出
if(APPLE)
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum macOS deployment version")
    endif()
    message(STATUS "macOS deployment target: ${CMAKE_OSX_DEPLOYMENT_TARGET}")
elseif(WIN32)
    message(STATUS "Building for Windows")
elseif(UNIX AND NOT APPLE)
    message(STATUS "Building for Linux")
else()
    message(FATAL_ERROR "Unsupported platform")
endif()

# レイアウト: ExternalProject の依存関係は _deps 配下で管理
if(DEFINED SKBUILD_PROJECT_DIR)
    set(PROJECT_ROOT "${SKBUILD_PROJECT_DIR}")
else()
    set(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
endif()

set(DEPS_DIR "${PROJECT_ROOT}/_deps")
message(STATUS "Dependencies directory: ${DEPS_DIR}")

# Python と nanobind を検索
find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)
find_package(nanobind CONFIG REQUIRED)

# 依存関係用の ExternalProject
include(ExternalProject)

# JSON から依存関係のバージョンを読み込み
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/deps.json DEPS_JSON)

# PortAudio の設定を解析
string(JSON PORTAUDIO_REF GET ${DEPS_JSON} portaudio ref)
string(JSON PORTAUDIO_GIT_REPOSITORY GET ${DEPS_JSON} portaudio url)

message(STATUS "PortAudio ref: ${PORTAUDIO_REF}")

# 静的ライブラリのビルドオプションを設定
set(BUILD_SHARED_LIBS OFF CACHE BOOL "静的ライブラリをビルド" FORCE)

# ExternalProject ビルド用の並列ビルドジョブ数を決定
if(DEFINED ENV{CMAKE_BUILD_PARALLEL_LEVEL})
    set(PARALLEL_JOBS "$ENV{CMAKE_BUILD_PARALLEL_LEVEL}")
else()
    include(ProcessorCount)
    ProcessorCount(NCORES)
    if(NOT NCORES OR NCORES EQUAL 0)
        set(NCORES 2)
    endif()
    set(PARALLEL_JOBS "${NCORES}")
endif()

# ========== PortAudio ==========
message(STATUS "Setting up PortAudio...")
set(PORTAUDIO_SOURCE_DIR "${DEPS_DIR}/portaudio/${PORTAUDIO_REF}/source")
set(PORTAUDIO_BUILD_DIR "${DEPS_DIR}/portaudio/${PORTAUDIO_REF}/build")
set(PORTAUDIO_INSTALL_DIR "${DEPS_DIR}/portaudio/${PORTAUDIO_REF}/install")

# プラットフォーム固有の設定
if(WIN32)
    set(PORTAUDIO_LIB ${PORTAUDIO_INSTALL_DIR}/lib/portaudio.lib)
else()
    set(PORTAUDIO_LIB ${PORTAUDIO_INSTALL_DIR}/lib/libportaudio.a)
endif()

# プラットフォーム固有の CMake 引数
if(APPLE)
    set(PORTAUDIO_PLATFORM_ARGS
        -DPA_USE_COREAUDIO=ON
        -DPA_USE_JACK=OFF
        -DPA_USE_ALSA=OFF
        -DPA_USE_OSS=OFF
    )
elseif(WIN32)
    set(PORTAUDIO_PLATFORM_ARGS
        -DPA_USE_WASAPI=ON
        -DPA_USE_WDMKS=OFF
        -DPA_USE_WDMKS_DEVICE_INFO=OFF
        -DPA_USE_ASIO=OFF
        -DPA_USE_DS=ON
        -DPA_USE_WMME=ON
    )
elseif(UNIX AND NOT APPLE)
    set(PORTAUDIO_PLATFORM_ARGS
        -DPA_USE_ALSA=ON
        -DPA_USE_PULSEAUDIO=ON
        -DPA_USE_JACK=OFF
        -DPA_USE_OSS=OFF
    )
endif()

# ビルド済みかチェック
if(EXISTS ${PORTAUDIO_LIB})
    message(STATUS "PortAudio already built: ${PORTAUDIO_LIB}")
    add_custom_target(portaudio_build)
else()
    message(STATUS "Building PortAudio...")
    ExternalProject_Add(
        portaudio_build
        GIT_REPOSITORY ${PORTAUDIO_GIT_REPOSITORY}
        GIT_TAG ${PORTAUDIO_REF}
        GIT_SHALLOW FALSE
        SOURCE_DIR ${PORTAUDIO_SOURCE_DIR}
        BINARY_DIR ${PORTAUDIO_BUILD_DIR}
        STAMP_DIR ${PORTAUDIO_BUILD_DIR}/stamp
        TMP_DIR ${PORTAUDIO_BUILD_DIR}/tmp
        UPDATE_COMMAND ""
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        CMAKE_ARGS
            -DCMAKE_INSTALL_PREFIX=${PORTAUDIO_INSTALL_DIR}
            -DCMAKE_BUILD_TYPE=Release
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
            -DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}
            -DPA_BUILD_SHARED_LIBS=OFF
            -DPA_BUILD_TESTS=OFF
            -DPA_BUILD_EXAMPLES=OFF
            ${PORTAUDIO_PLATFORM_ARGS}
        BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config Release --parallel ${PARALLEL_JOBS}
        INSTALL_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config Release --target install
        BUILD_BYPRODUCTS ${PORTAUDIO_LIB}
    )
endif()

# メインモジュールを作成
nanobind_add_module(
    portaudio_ext
    NB_STATIC
    FREE_THREADED
    src/portaudio_ext.cpp
)

# ビルド順序を保証するため依存関係を追加
add_dependencies(portaudio_ext portaudio_build)

# インクルードディレクトリ
target_include_directories(portaudio_ext PRIVATE
    ${PORTAUDIO_INSTALL_DIR}/include
)

# ライブラリをリンク
if(APPLE)
    target_link_libraries(portaudio_ext PRIVATE
        ${PORTAUDIO_LIB}
        "-framework AudioToolbox"
        "-framework AudioUnit"
        "-framework CoreAudio"
        "-framework CoreFoundation"
        "-framework CoreServices"
    )
elseif(WIN32)
    target_link_libraries(portaudio_ext PRIVATE
        ${PORTAUDIO_LIB}
        winmm
        ole32
        uuid
    )
else()
    # Linux
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(ALSA REQUIRED alsa)
    pkg_check_modules(PULSE REQUIRED libpulse)

    target_link_libraries(portaudio_ext PRIVATE
        ${PORTAUDIO_LIB}
        ${ALSA_LIBRARIES}
        ${PULSE_LIBRARIES}
        pthread
        m
    )
endif()

# .pyi スタブファイルを生成
nanobind_add_stub(
    portaudio_ext_stub
    MODULE portaudio_ext
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/portaudio_ext.pyi
    PYTHON_PATH $<TARGET_FILE_DIR:portaudio_ext>
    DEPENDS portaudio_ext
)

# 拡張モジュールをインストール
install(TARGETS portaudio_ext LIBRARY DESTINATION portaudio)

# .pyi スタブファイルをインストール
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/portaudio_ext.pyi DESTINATION portaudio)

# py.typed マーカーファイルをインストール（型情報を含むパッケージであることを示す）
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/portaudio/py.typed DESTINATION portaudio)
