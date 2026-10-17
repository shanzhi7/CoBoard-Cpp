/***********************************************************************************
* @file         canvasitemserializer.h
* @brief        画布图元与版本化 JSON 描述之间的转换
* @author       shanzhi
* @date         2026/10/08
* @history
***********************************************************************************/
#pragma once

#include "canvasdocument.h"

class CanvasItem;

class CanvasItemSerializer
{
public:
    static bool ValidateItem(const QJsonObject& item_data, const QMap<QString, CanvasAssetData>& asset_dataMap, QString* error_message); // 后台校验几何、文字排版及资源引用，不创建 GUI 对象。
    static bool SerializeItem(CanvasItem* item, QJsonObject* item_data, CanvasDocument* document, QString* error_message); // 保存原始路径、编辑映射、父图元附属文字和图片资源引用。
    static CanvasItem* DeserializeItem(const QJsonObject& item_data, const QMap<QString, CanvasAssetData>& asset_dataMap, QString* error_message); // 兼容旧图元描述并在局部排版后恢复公共变换。
};
