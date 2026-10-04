/***********************************************************************************
* @file         assetservicelogger.h
* @brief        图片后台代理的有界轮转 Qt 日志
* @author       shanzhi
* @date         2026/10/04
* @history
***********************************************************************************/
#pragma once

namespace AssetServiceLogger
{
void Install(); // 独占锁取得后安装三份两 MiB 日志。
void Uninstall(); // 应用停止前恢复原处理器。
}
