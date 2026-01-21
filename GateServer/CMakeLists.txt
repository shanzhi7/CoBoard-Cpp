cmake_minimum_required (VERSION 3.20)

# 0. 策略设置
if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif()

# 1. 工具链设置
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "D:/cppsoft/vcpkg-new/scripts/buildsystems/vcpkg.cmake")
endif()
set(VCPKG_TARGET_TRIPLET "x64-windows-static")

project ("GateServer")

# 2. 静态运行时
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 3. 宏定义 (MySQL必需)
add_definitions(-DSTATIC_CONCPP)
add_definitions(-DNOMINMAX)

# 4. 找库
find_package(hiredis CONFIG REQUIRED)
find_package(redis++ CONFIG REQUIRED)
find_package(jsoncpp CONFIG REQUIRED)
find_package(Boost REQUIRED)
find_package(Boost REQUIRED COMPONENTS filesystem system)
find_package(unofficial-mysql-connector-cpp CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_package(Protobuf CONFIG REQUIRED)
find_package(absl CONFIG REQUIRED)
# MySQL Connector 的依赖（静态链接必需）
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(lz4 CONFIG REQUIRED)
find_package(zstd CONFIG REQUIRED)

# 5. 生成
add_executable (GateServer "src/GateServer.cpp" 
"include/GateServer/Singleton.h"
"include/GateServer/RedisMgr.h" 
"src/RedisMgr.cpp" 
"include/GateServer/CServer.h"
"src/CServer.cpp" 
"include/GateServer/HttpConnection.h"
"src/HttpConnection.cpp"
"include/GateServer/AsioIOServicePool.h"
"src/AsioIOServicePool.cpp"
"include/GateServer/LogicSystem.h"
"src/LogicSystem.cpp"
"include/GateServer/const.h" 
"include/GateServer/ConfigMgr.h"
"src/ConfigMgr.cpp"
"include/GateServer/VerifyGrpcClient.h"
"src/VerifyGrpcClient.cpp"

"src/message.pb.cc"
"src/message.grpc.pb.cc"
"include/GateServer/message.pb.h"
"include/GateServer/message.grpc.pb.h"
)

# 5.1 头文件路径
target_include_directories(GateServer PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

# 6. 链接
target_link_libraries(GateServer PRIVATE
    # Redis
    hiredis::hiredis
    redis++::redis++_static

    # JSON
    JsonCpp::JsonCpp

    # Boost
    Boost::boost
    Boost::filesystem
    Boost::system

    # MySQL Connector/C++ (JDBC API + 静态链接依赖)
    unofficial::mysql-connector-cpp::connector-jdbc
    unofficial::mysql-connector-cpp::connector
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

    # Windows 系统库（静态链接必需）
    ws2_32
    dnsapi
    secur32
    crypt32
    bcrypt
)

# ---------------------------------------------------------
# 自动复制配置文件到构建目录
# ---------------------------------------------------------

# 1. 确保构建目标存在 (假设你的可执行文件叫 GateServer)
if(TARGET GateServer)
    add_custom_command(TARGET GateServer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/config.ini"      # 源文件路径
            "$<TARGET_FILE_DIR:GateServer>/config.ini"    # 目标路径(自动定位到exe所在目录)
    )
endif()