#pragma once

#include <json/json.h>
#include <cstddef>
#include <string>

// 图片资源签名服务只处理 OSS 元数据和访问授权，不代理图片二进制流。
class ImageAssetService
{
public:
    static bool BuildUploadResponse(const Json::Value& request, Json::Value& response); // 校验上传请求并生成短期 PUT 签名。
    static bool BuildDownloadResponse(const Json::Value& request, Json::Value& response); // 校验房间成员身份并生成短期 GET 签名。

private:
    static bool VerifyRoomMember(const Json::Value& request); // 通过 LogicServer Token 校验和 Redis 成员集合确认访问者身份。
    static bool ReadImageLimits(std::size_t& max_file_size, unsigned int& max_width, unsigned int& max_height); // 读取限制并在配置缺失时使用安全默认值。
    static bool IsAllowedSuffix(const std::string& suffix); // 判断扩展名是否属于首期允许的图片格式。
    static std::string NormalizeSuffix(const std::string& suffix); // 统一扩展名大小写并去除无关前缀。
    static std::string MimeTypeForSuffix(const std::string& suffix); // 将扩展名转换为签名使用的 MIME 类型。
    static std::string BuildObjectReference(const std::string& room_id, const std::string& asset_id, const std::string& suffix); // 生成不包含签名的稳定 OSS 对象引用。
    static bool ParseObjectReference(const std::string& room_id, const std::string& asset_id, const std::string& object_reference, std::string& object_name, std::string& mime_type); // 校验资源引用并提取对象名。
    static std::string BuildSignedUrl(const std::string& method, const std::string& object_name, const std::string& content_type, long long expire_time); // 按 OSS 规则生成短期签名地址。
    static std::string GenerateAssetId(); // 生成不依赖本地路径的随机资源 ID。
};
