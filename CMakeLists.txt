cmake_minimum_required(VERSION 3.16)
project(Beacon VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

include(GNUInstallDirs)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Network Quick Qml QuickControls2 QuickDialogs2 Svg Widgets)
find_package(ZLIB QUIET)

set(KERNEL_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/minecraft-launcher-kernel")

set(KERNEL_SRC
    ${KERNEL_SRC_DIR}/libmcbase/src/mc_str.cpp
    ${KERNEL_SRC_DIR}/libmcbase/src/mc_path.cpp
    ${KERNEL_SRC_DIR}/libmcbase/src/mc_log.cpp
    ${KERNEL_SRC_DIR}/libmcbase/src/mc_hash.cpp
    ${KERNEL_SRC_DIR}/libmcbase/src/mc_http.cpp
    ${KERNEL_SRC_DIR}/libmcbase/src/mc_i18n.cpp
    ${KERNEL_SRC_DIR}/libmcbase/src/mc_zip.cpp
    ${KERNEL_SRC_DIR}/libmcbase/src/mc_download.cpp
    ${KERNEL_SRC_DIR}/libmcbase/src/mc_download_qt.cpp
    ${KERNEL_SRC_DIR}/libmcbase/src/mc_version.cpp
    ${KERNEL_SRC_DIR}/libmcbase/src/mc_library.cpp
    ${KERNEL_SRC_DIR}/libmcbase/src/mc_manifest.cpp
    ${KERNEL_SRC_DIR}/libmcbase/src/mc_mod.cpp
    ${KERNEL_SRC_DIR}/login/private/mc_auth.cpp
    ${KERNEL_SRC_DIR}/login/private/mc_auth_msa.cpp
    ${KERNEL_SRC_DIR}/mcjava/private/mc_java.cpp
    ${KERNEL_SRC_DIR}/downloader/private/mc_asset.cpp
    ${KERNEL_SRC_DIR}/downloader/private/mc_java_dl.cpp
)

set(KERNEL_INCLUDES
    ${KERNEL_SRC_DIR}/libmcbase/include
    ${KERNEL_SRC_DIR}/login/private/include
    ${KERNEL_SRC_DIR}/mcjava/private/include
    ${KERNEL_SRC_DIR}/downloader/private/include
)

set(MANAGER_SRC
    src/cpp/main.cpp
    src/cpp/KernelBridge.h
    src/cpp/KernelBridge.cpp
    src/kernel/managers/VersionManager.h
    src/kernel/managers/VersionManager.cpp
    src/kernel/managers/DownloadManager.h
    src/kernel/managers/DownloadManager.cpp
    src/kernel/managers/DownloadWorker.h
    src/kernel/managers/DownloadWorker.cpp
    src/kernel/managers/JavaDownloadWorker.h
    src/kernel/managers/JavaDownloadWorker.cpp
    src/kernel/managers/AuthManager.h
    src/kernel/managers/AuthManager.cpp
    src/kernel/managers/JavaManager.h
    src/kernel/managers/JavaManager.cpp
    src/kernel/managers/ModManager.h
    src/kernel/managers/ModManager.cpp
    src/kernel/managers/ModpackManager.h
    src/kernel/managers/ModpackManager.cpp
    src/kernel/managers/InstallManager.h
    src/kernel/managers/InstallManager.cpp
    src/kernel/managers/LaunchManager.h
    src/kernel/managers/LaunchManager.cpp
    src/kernel/managers/InstanceManager.h
    src/kernel/managers/InstanceManager.cpp
    src/kernel/managers/SettingsManager.h
    src/kernel/managers/SettingsManager.cpp
    src/kernel/managers/SkinManager.h
    src/kernel/managers/SkinManager.cpp
    src/kernel/managers/I18nManager.h
    src/kernel/managers/I18nManager.cpp
    src/kernel/platform/WindowEffects.h
    src/kernel/platform/WindowEffects.cpp
    src/cpp/VersionGroupModel.h
    src/cpp/VersionGroupModel.cpp
    resources/qml.qrc
)

if(WIN32)
    list(APPEND MANAGER_SRC resources/app.rc)
endif()

add_library(libmckernel STATIC ${KERNEL_SRC})
target_include_directories(libmckernel PUBLIC ${KERNEL_INCLUDES})
target_link_libraries(libmckernel PUBLIC Qt6::Core Qt6::Network Qt6::Gui)
target_compile_definitions(libmckernel PRIVATE QT_DISABLE_DEPRECATED_BEFORE=0x060000)

if(WIN32)
    qt_add_executable(Beacon WIN32 ${MANAGER_SRC})
elseif(APPLE)
    qt_add_executable(Beacon MACOSX_BUNDLE ${MANAGER_SRC})
    set_target_properties(Beacon PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.beacon.launcher"
        MACOSX_BUNDLE_BUNDLE_NAME "Beacon"
        MACOSX_BUNDLE_BUNDLE_VERSION "1.0.0"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0.0")
else()
    qt_add_executable(Beacon ${MANAGER_SRC})
endif()
target_include_directories(Beacon PRIVATE
    ${KERNEL_INCLUDES}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/kernel/managers
    ${CMAKE_CURRENT_SOURCE_DIR}/src/kernel/platform
)
set_target_properties(Beacon PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
target_link_libraries(Beacon PRIVATE
    libmckernel
    Qt6::Quick
    Qt6::Qml
    Qt6::QuickControls2
    Qt6::QuickDialogs2
    Qt6::Svg
    Qt6::Widgets
)
if(ZLIB_FOUND)
    target_link_libraries(Beacon PRIVATE ZLIB::ZLIB)
else()
    # MinGW/macOS ship zlib, MSVC via vcpkg provides ZLIB_FOUND.
    target_link_libraries(Beacon PRIVATE z)
endif()

set(QML_SOURCES
    src/qml/main.qml
    src/qml/theme/Theme.qml
    src/qml/pages/LaunchPage.qml
    src/qml/pages/InstancesPage.qml
    src/qml/pages/DownloadPage.qml
    src/qml/pages/SettingsPage.qml
    src/qml/pages/ToolsPage.qml
    src/qml/pages/InstanceSettingsPage.qml
    src/qml/pages/LogViewerPage.qml
    src/qml/pages/FileManagerPage.qml
    src/qml/components/AppIcon.qml
    src/qml/components/DownloadCard.qml
    src/qml/components/DownloadDialog.qml
    src/qml/components/DownloadStatusPanel.qml
    src/qml/components/ModsSearchPage.qml
    src/qml/components/ModDetailPage.qml
    src/qml/components/ModDownloadDialog.qml
    src/qml/components/ModpackSearchPage.qml
    src/qml/components/ModpackDetailPage.qml
    src/qml/components/ModpackInstallDialog.qml
)

# Preserve the qrc:/qml/... resource layout so runtime URLs stay unchanged
foreach(_qml_src IN LISTS QML_SOURCES)
    file(RELATIVE_PATH _qml_alias
        "${CMAKE_CURRENT_SOURCE_DIR}/src/qml" "${CMAKE_CURRENT_SOURCE_DIR}/${_qml_src}")
    set_source_files_properties(${_qml_src} PROPERTIES QT_RESOURCE_ALIAS "${_qml_alias}")
endforeach()

# AOT-compile the QML into the executable to reduce QML engine runtime memory
qt_add_qml_module(Beacon
    URI "Beacon"
    VERSION 1.0
    RESOURCE_PREFIX "/qml"
    NO_RESOURCE_TARGET_PATH
    NO_LINT
    QML_FILES ${QML_SOURCES}
)

# Copy mirrors.json to build directory
add_custom_command(TARGET Beacon POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/minecraft-launcher-kernel/mirrors.json"
        "$<TARGET_FILE_DIR:Beacon>/mirrors.json"
    COMMENT "Copying mirrors.json to build directory"
)

# ---- Install rules (Linux/macOS packaging) ----
install(TARGETS Beacon
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/minecraft-launcher-kernel/mirrors.json"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/beacon
)

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/Untitled.svg"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
    RENAME io.github.fuqicn.beacon.svg
)
