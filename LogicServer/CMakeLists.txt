# CMakeList.txt: LogicServer 的 CMake 项目，在此处包括源代码并定义
# 项目特定的逻辑。
#
cmake_minimum_required (VERSION 3.20)

# 0. 策略设置
if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif()

# 1. 工具链设置
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND CMAKE_HOST_WIN32)
    set(CMAKE_TOOLCHAIN_FILE "D:/cppsoft/vcpkg-new/scripts/buildsystems/vcpkg.cmake")
endif()
if(NOT DEFINED VCPKG_TARGET_TRIPLET)
    if(CMAKE_HOST_WIN32)
        set(VCPKG_TARGET_TRIPLET "x64-windows-static")
    else()
        set(VCPKG_TARGET_TRIPLET "x64-linux")
    endif()
endif()
set(BUILD_SHARED_LIBS OFF)
if(UNIX AND NOT APPLE)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
endif()

project ("LogicServer")

# 2. 静态运行时
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 3. 宏定义 (MySQL必需)
add_definitions(-DSTATIC_CONCPP)
if(WIN32)
    add_definitions(-DNOMINMAX)
endif()

# 4. 找库
find_package(hiredis CONFIG REQUIRED)
find_package(redis++ CONFIG REQUIRED)
find_package(jsoncpp CONFIG REQUIRED)
find_package(Boost REQUIRED)
find_package(Boost REQUIRED COMPONENTS filesystem system)
find_package(unofficial-mysql-connector-cpp CONFIG REQUIRED)

# 【新增】显式查找 MySQL 原生 C 库
find_package(unofficial-libmysql CONFIG REQUIRED)

find_package(gRPC CONFIG REQUIRED)
find_package(Protobuf CONFIG REQUIRED)
find_package(absl CONFIG REQUIRED)
find_package(Threads REQUIRED)
# MySQL Connector 的依赖（静态链接必需）
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(lz4 CONFIG REQUIRED)
find_package(zstd CONFIG REQUIRED)

# 5. 生成
add_executable (LogicServer "src/LogicServer.cpp"
"include/LogicServer/LogicServer.h"
 "include/LogicServer/LogicServiceImpl.h"
 "src/LogicServiceImpl.cpp"
 "include/LogicServer/const.h"
 "include/LogicServer/RedisMgr.h"
 "src/RedisMgr.cpp"
 "include/LogicServer/MysqlDao.h"
 "src/MysqlDao.cpp"
 "include/LogicServer/MysqlMgr.h"
 "src/MysqlMgr.cpp"
 "include/LogicServer/MysqlPool.h"
 "src/MysqlPool.cpp"
 "include/LogicServer/ConfigMgr.h"
 "src/ConfigMgr.cpp"
 "include/LogicServer/Singleton.h"

 "src/message.pb.cc"              # Protobuf 序列化代码
 "src/message.grpc.pb.cc"         # gRPC 服务代码
   "include/LogicServer/message.grpc.pb.h"
   "include/LogicServer/message.pb.h"
   "include/LogicServer/data.h")

# 5.1 头文件路径
target_include_directories(LogicServer PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

# 6. 链接
target_link_libraries(LogicServer PRIVATE
    # Redis
    hiredis::hiredis
    redis++::redis++_static

    # JSON
    JsonCpp::JsonCpp

    # Boost
    Boost::boost
    Boost::filesystem
    Boost::system

    # MySQL Connector/C++ JDBC API + static libmysql
    unofficial::mysql-connector-cpp::connector-jdbc

    # 【新增】显式链接 MySQL C 库 (解决 LNK2019 mysql_client_find_plugin)
    unofficial::libmysql::libmysql

    #依赖库
    OpenSSL::SSL
    OpenSSL::Crypto
    ZLIB::ZLIB
    lz4::lz4
    $<IF:$<TARGET_EXISTS:zstd::libzstd_static>,zstd::libzstd_static,zstd::libzstd_shared>

    # gRPC + 依赖
    gRPC::gpr
    gRPC::grpc
    gRPC::grpc++
    gRPC::grpc++_alts
    protobuf::libprotobuf
    absl::base
    absl::synchronization
    absl::strings
    Threads::Threads
)

if(WIN32)
    target_link_libraries(LogicServer PRIVATE
    # Windows 系统库（静态链接必需）
    ws2_32
    dnsapi
    secur32
    crypt32
    bcrypt
    )
elseif(UNIX AND NOT APPLE)
    target_link_options(LogicServer PRIVATE -static -static-libgcc -static-libstdc++)
    target_link_libraries(LogicServer PRIVATE ${CMAKE_DL_LIBS} resolv)
endif()

# ---------------------------------------------------------
# 自动复制配置文件到构建目录
# ---------------------------------------------------------

# 1. 确保构建目标存在 (假设你的可执行文件叫 GateServer)
if(TARGET LogicServer AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/config.ini")
    add_custom_command(TARGET LogicServer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/config.ini"      # 源文件路径
            "$<TARGET_FILE_DIR:LogicServer>/config.ini"    # 目标路径(自动定位到exe所在目录)
    )
endif()

