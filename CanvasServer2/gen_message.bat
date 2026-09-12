@ECHO OFF
setlocal

:: ================= 1. 自动识别项目名 =================
for %%I in ("%~dp0.") do set "PROJECT_NAME=%%~nxI"

:: ================= 2. 配置区域 =================
:: 设置工具路径
SET PROTOC="D:\cppsoft\vcpkg-new\installed\x64-windows-static\tools\protobuf\protoc.exe"
SET PLUGIN="D:\cppsoft\vcpkg-new\installed\x64-windows\tools\grpc\grpc_cpp_plugin.exe"

:: 设置目标目录
SET DEST_INC=include\%PROJECT_NAME%
SET DEST_SRC=src
:: ===============================================

ECHO -----------------------------------------------
ECHO 检测到当前项目名: %PROJECT_NAME%
ECHO -----------------------------------------------

ECHO [1/3] 正在生成 Protobuf ^& gRPC 代码...
:: 生成消息代码
%PROTOC% --cpp_out=. "message.proto"
:: 生成 gRPC 服务代码
%PROTOC% --grpc_out=. --plugin=protoc-gen-grpc=%PLUGIN% "message.proto"

:: 检查编译是否出错，如果出错就不移动文件了
IF %ERRORLEVEL% NEQ 0 (
    ECHO.
    ECHO [错误] Protobuf 编译失败，请检查 message.proto 文件内容！
    PAUSE
    EXIT /B
)

ECHO.
ECHO [2/3] 正在移动头文件 (.h) 到 %DEST_INC% ...
if not exist "%DEST_INC%" mkdir "%DEST_INC%"
:: 只有文件存在才移动，避免报错
if exist *.pb.h MOVE /Y *.pb.h "%DEST_INC%\" >NUL

ECHO [3/3] 正在移动源文件 (.cc) 到 %DEST_SRC% ...
if not exist "%DEST_SRC%" mkdir "%DEST_SRC%"
if exist *.pb.cc MOVE /Y *.pb.cc "%DEST_SRC%\" >NUL

ECHO.
ECHO [成功] 所有文件已自动归位到 %PROJECT_NAME% 相应目录下。
PAUSE