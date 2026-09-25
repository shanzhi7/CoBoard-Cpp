cmake_minimum_required(VERSION 3.19)


project(DBClient LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Network)
find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(Qt6 REQUIRED COMPONENTS Widgets)

# 2. 查找 Protobuf 和 gRPC (由 vcpkg 提供)
find_package(protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)

# LiveKit C++ SDK (Windows x64, prebuilt Release package)
set(LIVEKIT_SDK_ROOT "D:/cppsoft/livekit-sdk-windows-x64-1.10.0")
find_package(LiveKit CONFIG REQUIRED
    PATHS "${LIVEKIT_SDK_ROOT}/lib/cmake/LiveKit"
    NO_DEFAULT_PATH
)
set_property(TARGET LiveKit::livekit PROPERTY MAP_IMPORTED_CONFIG_DEBUG Release)


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
    voicemanager.h voicemanager.cpp
    tipwidget.h tipwidget.cpp
    config.ini
    userdata.h
    userdata.cpp
    usermgr.h usermgr.cpp
    resetwidget.h resetwidget.cpp resetwidget.ui
    canvas.h canvas.cpp canvas.ui
    canvasgraphicsview.h canvasgraphicsview.cpp canvasgraphicsview.ui
    testmode.h testmode.cpp
    tcpmgr.h tcpmgr.cpp
    newroomdialog.h newroomdialog.cpp newroomdialog.ui
    paintscene.h paintscene.cpp
    drawtool.h drawtool.cpp
    message.grpc.pb.cc message.grpc.pb.h message.pb.cc message.pb.h
    message.proto
    lobbywidget.h lobbywidget.cpp lobbywidget.ui
    joinroomdialog.h joinroomdialog.cpp joinroomdialog.ui
    userlisttree.h userlisttree.cpp
    widthpopup.h widthpopup.cpp

)

# ---> 请添加这段代码 <---
target_include_directories(DBClient PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(DBClient PRIVATE cxx_std_17)

target_link_libraries(DBClient
    PRIVATE
        Qt::Core
        Qt::Widgets
        Qt::Network

        #链接 Google 的库
        protobuf::libprotobuf
        gRPC::grpc++
        LiveKit::livekit

        # LiveKit Windows system dependencies
        ntdll
        userenv
        winmm
        iphlpapi
        msdmo
        dmoguids
        wmcodecdspuuid
        ws2_32
        secur32
        bcrypt
        crypt32
)
target_link_libraries(DBClient PRIVATE Qt6::Widgets)
target_link_libraries(DBClient PRIVATE Qt6::Widgets)
target_link_libraries(DBClient PRIVATE Qt6::Core)
target_link_libraries(DBClient PRIVATE Qt6::Widgets)
target_link_libraries(DBClient PRIVATE Qt6::Core)
target_link_libraries(DBClient PRIVATE Qt6::Core)
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

# 确保 config.ini 始终复制到 .exe 所在的目录 (Debug/Release 均自动适配)
add_custom_command(TARGET DBClient POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/config.ini"
    "$<TARGET_FILE_DIR:DBClient>/config.ini"
)

# LiveKit SDK 的运行时 DLL 必须与 DBClient.exe 位于同一目录。
add_custom_command(TARGET DBClient POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${LIVEKIT_SDK_ROOT}/bin/livekit.dll"
        "$<TARGET_FILE_DIR:DBClient>/livekit.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${LIVEKIT_SDK_ROOT}/bin/livekit_ffi.dll"
        "$<TARGET_FILE_DIR:DBClient>/livekit_ffi.dll"
)
