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
    static bool ValidateItem(const QJsonObject& item_data, const QMap<QString, CanvasAssetData>& asset_dataMap, QString* error_message); // 后台校验图元几何、枚举和资源引用。
    static bool SerializeItem(CanvasItem* item, QJsonObject* item_data, CanvasDocument* document, QString* error_message); // 序列化图元及其图片资源引用。
    static CanvasItem* DeserializeItem(const QJsonObject& item_data, const QMap<QString, CanvasAssetData>& asset_dataMap, QString* error_message); // 从已校验数据创建独立图元。
};
