@ECHO OFF
setlocal

:: 设置工具路径 (根据你刚才找到的路径)
SET PROTOC="D:\cppsoft\vcpkg-new\installed\x64-windows-static\tools\protobuf\protoc.exe"
SET PLUGIN="D:\cppsoft\vcpkg-new\installed\x64-windows\tools\grpc\grpc_cpp_plugin.exe"

ECHO 正在生成 Protobuf 消息代码 (.pb.h, .pb.cc)...
%PROTOC% --cpp_out=. "message.proto"

ECHO 正在生成 gRPC 服务代码 (.grpc.pb.h, .grpc.pb.cc)...
%PROTOC% --grpc_out=. --plugin=protoc-gen-grpc=%PLUGIN% "message.proto"

ECHO.
ECHO 所有文件生成完毕！请将生成的4个文件(cc/h)覆盖到你的源码目录中。
PAUSE