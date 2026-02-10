# Diff Details

Date : 2026-02-06 20:59:42

Directory e:\\qt_project\\SyncCanvas

Total : 54 files,  875 codes, 147 comments, 218 blanks, all 1240 lines

[Summary](results.md) / [Details](details.md) / [Diff Summary](diff.md) / Diff Details

## Files
| filename | language | code | comment | blank | total |
| :--- | :--- | ---: | ---: | ---: | ---: |
| [CanvasServer2/config.ini](/CanvasServer2/config.ini) | Ini | -30 | 0 | 0 | -30 |
| [CanvasServer2/include/CanvasServer/CSession.h](/CanvasServer2/include/CanvasServer/CSession.h) | C++ | 7 | 0 | 0 | 7 |
| [CanvasServer2/include/CanvasServer/Room.h](/CanvasServer2/include/CanvasServer/Room.h) | C++ | 4 | 2 | 2 | 8 |
| [CanvasServer2/include/CanvasServer/const.h](/CanvasServer2/include/CanvasServer/const.h) | C++ | 5 | 0 | 0 | 5 |
| [CanvasServer2/src/CSession.cpp](/CanvasServer2/src/CSession.cpp) | C++ | 24 | -3 | 5 | 26 |
| [CanvasServer2/src/LogicSystem.cpp](/CanvasServer2/src/LogicSystem.cpp) | C++ | 14 | 4 | 5 | 23 |
| [CanvasServer2/src/Room.cpp](/CanvasServer2/src/Room.cpp) | C++ | 63 | 13 | 16 | 92 |
| [CanvasServer/CMakeLists.txt](/CanvasServer/CMakeLists.txt) | CMake | -1 | 0 | 1 | 0 |
| [CanvasServer/config.ini](/CanvasServer/config.ini) | Ini | -30 | 0 | 0 | -30 |
| [CanvasServer/include/CanvasServer/CSession.h](/CanvasServer/include/CanvasServer/CSession.h) | C++ | 7 | 0 | 0 | 7 |
| [CanvasServer/include/CanvasServer/Room.h](/CanvasServer/include/CanvasServer/Room.h) | C++ | 4 | 2 | 2 | 8 |
| [CanvasServer/include/CanvasServer/const.h](/CanvasServer/include/CanvasServer/const.h) | C++ | 5 | 0 | 0 | 5 |
| [CanvasServer/src/CSession.cpp](/CanvasServer/src/CSession.cpp) | C++ | 22 | -3 | 5 | 24 |
| [CanvasServer/src/LogicSystem.cpp](/CanvasServer/src/LogicSystem.cpp) | C++ | 14 | 4 | 5 | 23 |
| [CanvasServer/src/Room.cpp](/CanvasServer/src/Room.cpp) | C++ | 63 | 13 | 16 | 92 |
| [DBClient/CMakeLists.txt](/DBClient/CMakeLists.txt) | CMake | 3 | 0 | 0 | 3 |
| [DBClient/canvas.cpp](/DBClient/canvas.cpp) | C++ | 80 | -7 | 3 | 76 |
| [DBClient/canvas.h](/DBClient/canvas.h) | C++ | 7 | 0 | -1 | 6 |
| [DBClient/config.ini](/DBClient/config.ini) | Ini | -4 | 0 | 0 | -4 |
| [DBClient/global.h](/DBClient/global.h) | C++ | 8 | 0 | 1 | 9 |
| [DBClient/hoverwidget.cpp](/DBClient/hoverwidget.cpp) | C++ | 37 | 18 | 17 | 72 |
| [DBClient/hoverwidget.h](/DBClient/hoverwidget.h) | C++ | 7 | 6 | 2 | 15 |
| [DBClient/httpmgr.cpp](/DBClient/httpmgr.cpp) | C++ | 41 | 7 | 6 | 54 |
| [DBClient/httpmgr.h](/DBClient/httpmgr.h) | C++ | 2 | 1 | 0 | 3 |
| [DBClient/lobbywidget.cpp](/DBClient/lobbywidget.cpp) | C++ | 135 | 30 | 34 | 199 |
| [DBClient/lobbywidget.h](/DBClient/lobbywidget.h) | C++ | 15 | 0 | 5 | 20 |
| [DBClient/loginwidget.cpp](/DBClient/loginwidget.cpp) | C++ | 2 | 0 | 0 | 2 |
| [DBClient/mainwindow.cpp](/DBClient/mainwindow.cpp) | C++ | 7 | 3 | 2 | 12 |
| [DBClient/mainwindow.h](/DBClient/mainwindow.h) | C++ | 1 | 0 | 0 | 1 |
| [DBClient/tcpmgr.cpp](/DBClient/tcpmgr.cpp) | C++ | 37 | 6 | 11 | 54 |
| [DBClient/tcpmgr.h](/DBClient/tcpmgr.h) | C++ | 2 | 0 | 1 | 3 |
| [DBClient/userdata.cpp](/DBClient/userdata.cpp) | C++ | 3 | 0 | 2 | 5 |
| [DBClient/userdata.h](/DBClient/userdata.h) | C++ | 1 | 0 | 0 | 1 |
| [DBClient/userlisttree.cpp](/DBClient/userlisttree.cpp) | C++ | 15 | 0 | 3 | 18 |
| [DBClient/userlisttree.h](/DBClient/userlisttree.h) | C++ | 13 | 0 | 4 | 17 |
| [DBClient/usermgr.cpp](/DBClient/usermgr.cpp) | C++ | 104 | 11 | 19 | 134 |
| [DBClient/usermgr.h](/DBClient/usermgr.h) | C++ | 12 | 0 | 1 | 13 |
| [DBClient/welcomewidget.cpp](/DBClient/welcomewidget.cpp) | C++ | 14 | 4 | 7 | 25 |
| [DBClient/welcomewidget.h](/DBClient/welcomewidget.h) | C++ | 1 | 0 | 1 | 2 |
| [GateServer/config.ini](/GateServer/config.ini) | Ini | -8 | 0 | 0 | -8 |
| [GateServer/include/GateServer/HttpConnection.h](/GateServer/include/GateServer/HttpConnection.h) | C++ | 54 | 10 | 4 | 68 |
| [GateServer/include/GateServer/LogicGrpcClient.h](/GateServer/include/GateServer/LogicGrpcClient.h) | C++ | 3 | 0 | 0 | 3 |
| [GateServer/include/GateServer/VerifyGrpcClient.h](/GateServer/include/GateServer/VerifyGrpcClient.h) | C++ | 0 | 1 | 1 | 2 |
| [GateServer/src/HttpConnection.cpp](/GateServer/src/HttpConnection.cpp) | C++ | -54 | -10 | -4 | -68 |
| [GateServer/src/LogicGrpcClient.cpp](/GateServer/src/LogicGrpcClient.cpp) | C++ | 17 | 3 | 2 | 22 |
| [GateServer/src/LogicSystem.cpp](/GateServer/src/LogicSystem.cpp) | C++ | 111 | 25 | 29 | 165 |
| [LogicServer/config.ini](/LogicServer/config.ini) | Ini | -30 | 0 | 0 | -30 |
| [LogicServer/include/LogicServer/LogicServiceImpl.h](/LogicServer/include/LogicServer/LogicServiceImpl.h) | C++ | 3 | 1 | 1 | 5 |
| [LogicServer/include/LogicServer/MysqlDao.h](/LogicServer/include/LogicServer/MysqlDao.h) | C++ | 1 | 1 | 1 | 3 |
| [LogicServer/include/LogicServer/MysqlMgr.h](/LogicServer/include/LogicServer/MysqlMgr.h) | C++ | 1 | 1 | 1 | 3 |
| [LogicServer/src/LogicServiceImpl.cpp](/LogicServer/src/LogicServiceImpl.cpp) | C++ | 17 | 0 | 3 | 20 |
| [LogicServer/src/MysqlDao.cpp](/LogicServer/src/MysqlDao.cpp) | C++ | 40 | 4 | 4 | 48 |
| [LogicServer/src/MysqlMgr.cpp](/LogicServer/src/MysqlMgr.cpp) | C++ | 4 | 0 | 1 | 5 |
| [README.md](/README.md) | Markdown | 2 | 0 | 0 | 2 |

[Summary](results.md) / [Details](details.md) / [Diff Summary](diff.md) / Diff Details