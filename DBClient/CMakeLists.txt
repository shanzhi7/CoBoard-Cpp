cmake_minimum_required(VERSION 3.19)
project(DBClient LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Network)
find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(Qt6 REQUIRED COMPONENTS Widgets)

# 强制开启资源自动编译
set(CMAKE_AUTORCC ON)

# 显式定义资源文件
set(APP_ICON_RESOURCE "app_icon.rc")

qt_standard_project_setup()

qt_add_executable(DBClient
    WIN32 MACOSX_BUNDLE
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    global.h
    global.cpp
    welcomewidget.h welcomewidget.cpp welcomewidget.ui
    res.qrc
    ${APP_ICON_RESOURCE} #logo
    hoverwidget.h hoverwidget.cpp
    loginwidget.h loginwidget.cpp loginwidget.ui
    registerwidget.h registerwidget.cpp registerwidget.ui
    singleton.h
    httpmgr.h httpmgr.cpp
    tipwidget.h tipwidget.cpp
    config.ini
)

# ---> 请添加这段代码 <---
target_include_directories(DBClient PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(DBClient
    PRIVATE
        Qt::Core
        Qt::Widgets
        Qt::Network
)
target_link_libraries(DBClient PRIVATE Qt6::Widgets)
target_link_libraries(DBClient PRIVATE Qt6::Widgets)
target_link_libraries(DBClient PRIVATE Qt6::Core)
target_link_libraries(DBClient PRIVATE Qt6::Widgets)

include(GNUInstallDirs)

install(TARGETS DBClient
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET DBClient
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})

# 确保 config.ini 存在于源码目录中
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/config.ini"
    "${CMAKE_CURRENT_BINARY_DIR}/config.ini"
    COPYONLY
)
