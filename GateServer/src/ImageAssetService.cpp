#include "GateServer/ImageAssetService.h"

#include "GateServer/ConfigMgr.h"
#include "GateServer/LogicGrpcClient.h"
#include "GateServer/RedisMgr.h"
#include "GateServer/const.h"
#include "GateServer/message.pb.h"

#include <boost/beast/core/detail/base64.hpp>
#include <openssl/hmac.h>
#include <openssl/evp.h>

#include <algorithm>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <random>
#include <sstream>

namespace
{
std::string HmacSha1(const std::string& key, const std::string& data)
{
    // OSS 使用 HMAC-SHA1 对固定格式的 StringToSign 签名；密钥只在本函数内使用，不进入日志和响应。
    unsigned char digest[EVP_MAX_MD_SIZE] = {};
    unsigned int digest_length = 0;
    HMAC(EVP_sha1(),
         key.data(), static_cast<int>(key.size()),
         reinterpret_cast<const unsigned char*>(data.data()), data.size(),
         digest, &digest_length);
    return std::string(reinterpret_cast<const char*>(digest), digest_length);
}

std::string Base64Encode(const std::string& input)
{
    // OSS 签名需要 Base64 文本；输出只用于后续 URL 编码，不会写入日志。
    std::string output;
    output.resize(boost::beast::detail::base64::encoded_size(input.size()));
    const auto result = boost::beast::detail::base64::encode(&output[0], input.data(), input.size());
    output.resize(result);
    return output;
}

std::string UrlEncode(const std::string& input)
{
    // 资源引用中的路径只允许安全字符，签名参数中的特殊字符必须进行百分号编码。
    static const char hex[] = "0123456789ABCDEF";
    std::ostringstream output;
    for (unsigned char value : input)
    {
        if (std::isalnum(value) || value == '-' || value == '_' || value == '.' || value == '~')
        {
            output << static_cast<char>(value);
        }
        else
        {
            output << '%' << hex[(value >> 4) & 0x0F] << hex[value & 0x0F];
        }
    }
    return output.str();
}

bool IsDigits(const std::string& value)
{
    return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char character) {
        return std::isdigit(character) != 0;
    });
}

bool IsHexAssetId(const std::string& asset_id)
{
    // 网关生成的资源 ID 固定为 128 位随机数的 32 位十六进制文本，下载请求必须保持同一格式。
    if (asset_id.size() != 32)
    {
        return false;
    }
    for (const char value : asset_id)
    {
        if (!std::isxdigit(static_cast<unsigned char>(value)))
        {
            return false;
        }
    }
    return true;
}

std::string ReadObjectPrefix()
{
    // 上传和下载必须共用配置中的单级对象前缀，防止不同服务生成不一致的 OSS 路径。
    std::string prefix = ConfigMgr::Inst()["ImageAsset"]["ObjectPrefix"];
    if (prefix.empty())
    {
        prefix = "canvas-assets";
    }
    while (!prefix.empty() && prefix.front() == '/')
    {
        prefix.erase(prefix.begin());
    }
    while (!prefix.empty() && prefix.back() == '/')
    {
        prefix.pop_back();
    }
    if (prefix.empty() || prefix.find("..") != std::string::npos || prefix.find('/') != std::string::npos)
    {
        return "canvas-assets";
    }
    return prefix;
}

long long ReadExpireSeconds(const std::string& key, long long fallback)
{
    // 有效期只能在合理的正数范围内配置，错误配置回退到需求约定值而不生成永久签名。
    try
    {
        const std::string configured = ConfigMgr::Inst()["ImageAsset"][key];
        const long long seconds = configured.empty() ? fallback : std::stoll(configured);
        return seconds > 0 ? seconds : fallback;
    }
    catch (const std::exception&)
    {
        return fallback;
    }
}
}

std::string ImageAssetService::NormalizeSuffix(const std::string& suffix)
{
    std::string normalized = suffix;
    if (!normalized.empty() && normalized.front() == '.')
    {
        normalized.erase(normalized.begin());
    }
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return normalized;
}

bool ImageAssetService::IsAllowedSuffix(const std::string& suffix)
{
    const std::string normalized = NormalizeSuffix(suffix);
    return normalized == "png" || normalized == "jpg" || normalized == "jpeg" || normalized == "webp";
}

std::string ImageAssetService::MimeTypeForSuffix(const std::string& suffix)
{
    const std::string normalized = NormalizeSuffix(suffix);
    if (normalized == "png")
    {
        return "image/png";
    }
    if (normalized == "webp")
    {
        return "image/webp";
    }
    return "image/jpeg";
}

bool ImageAssetService::ReadImageLimits(std::size_t& max_file_size,
                                        unsigned int& max_width,
                                        unsigned int& max_height)
{
    // 缺少配置时使用计划中约定的保守值，确保新接口不会因为配置遗漏而无限制接受资源。
    auto& config = ConfigMgr::Inst();
    const std::string configured_size = config["ImageAsset"]["MaxFileSizeBytes"];
    const std::string configured_width = config["ImageAsset"]["MaxWidth"];
    const std::string configured_height = config["ImageAsset"]["MaxHeight"];
    try
    {
        max_file_size = configured_size.empty() ? 10U * 1024U * 1024U : static_cast<std::size_t>(std::stoull(configured_size));
        max_width = configured_width.empty() ? 4096U : static_cast<unsigned int>(std::stoul(configured_width));
        max_height = configured_height.empty() ? 4096U : static_cast<unsigned int>(std::stoul(configured_height));
    }
    catch (const std::exception&)
    {
        max_file_size = 10U * 1024U * 1024U;
        max_width = 4096U;
        max_height = 4096U;
        return false;
    }
    return max_file_size > 0 && max_width > 0 && max_height > 0;
}

bool ImageAssetService::VerifyRoomMember(const Json::Value& request)
{
    // 先通过 LogicServer 验证 Token 与 UID 的绑定，再读取 Redis 成员集合，防止伪造 uid 访问任意房间资源。
    if (!request.isMember("uid") || !request.isMember("token") || !request.isMember("room_id"))
    {
        return false;
    }
    const int uid = request["uid"].asInt();
    const std::string token = request["token"].asString();
    const std::string room_id = request["room_id"].asString();
    if (uid <= 0 || token.empty() || !IsDigits(room_id) || room_id.size() > 32)
    {
        return false;
    }

    message::VerifyTokenReq verify_request;
    verify_request.set_uid(uid);
    verify_request.set_token(token);
    const message::VerifyTokenRsp verify_response = LogicGrpcClient::getInstance()->VerifyToken(verify_request);
    if (verify_response.error() != message::ErrorCodes::SUCCESS || verify_response.uid() != uid)
    {
        return false;
    }

    const std::string member_key = std::string(ROOM_USERS_PREFIX) + room_id;
    return RedisMgr::getInstance()->SIsMember(member_key, std::to_string(uid));
}

std::string ImageAssetService::BuildObjectReference(const std::string& room_id,
                                                    const std::string& asset_id,
                                                    const std::string& suffix)
{
    // 对象引用固定带房间前缀，下载时可验证引用不能跨房间指向其他资源。
    return ReadObjectPrefix() + "/" + room_id + "/" + asset_id + "." + NormalizeSuffix(suffix);
}

bool ImageAssetService::ParseObjectReference(const std::string& room_id,
                                             const std::string& asset_id,
                                             const std::string& object_reference,
                                             std::string& object_name,
                                             std::string& mime_type)
{
    // 客户端只能回传服务端生成的稳定引用；拒绝 ..、查询串和绝对 URL，避免签名任意 OSS 对象。
    if (object_reference.empty() || object_reference.find("..") != std::string::npos ||
        object_reference.find('?') != std::string::npos || object_reference.find("//") != std::string::npos)
    {
        return false;
    }
    if (!IsHexAssetId(asset_id))
    {
        return false;
    }
    const std::string prefix = ReadObjectPrefix() + "/" + room_id + "/" + asset_id + ".";
    if (object_reference.compare(0, prefix.size(), prefix) != 0)
    {
        return false;
    }
    const std::string suffix = object_reference.substr(prefix.size());
    if (!IsAllowedSuffix(suffix) || suffix.find('/') != std::string::npos)
    {
        return false;
    }
    object_name = object_reference;
    mime_type = MimeTypeForSuffix(suffix);
    return true;
}

std::string ImageAssetService::BuildSignedUrl(const std::string& method,
                                              const std::string& object_name,
                                              const std::string& content_type,
                                              long long expire_time)
{
    // 统一从配置读取 OSS 参数，并使用对象名参与签名，防止 URL 被替换成其他资源。
    auto& config = ConfigMgr::Inst();
    const std::string access_id = config["AliyunOSS"]["AccessKeyId"];
    const std::string access_secret = config["AliyunOSS"]["AccessKeySecret"];
    const std::string bucket = config["AliyunOSS"]["BucketName"];
    const std::string host = config["AliyunOSS"]["Host"];
    if (access_id.empty() || access_secret.empty() || bucket.empty() || host.empty())
    {
        return {};
    }

    const std::string string_to_sign = method + "\n\n" + content_type + "\n" +
                                       std::to_string(expire_time) + "\n/" + bucket + "/" + object_name;
    const std::string signature = UrlEncode(Base64Encode(HmacSha1(access_secret, string_to_sign)));
    std::ostringstream signed_url;
    signed_url << host << "/" << object_name
               << "?OSSAccessKeyId=" << UrlEncode(access_id)
               << "&Expires=" << expire_time
               << "&Signature=" << signature;
    return signed_url.str();
}

std::string ImageAssetService::GenerateAssetId()
{
    // 使用随机 128 位十六进制 ID，不把 uid、时间戳或本地文件名暴露为资源标识。
    static std::random_device random_device;
    static std::mt19937_64 generator(random_device());
    std::uniform_int_distribution<unsigned long long> distribution;
    std::ostringstream asset_id;
    asset_id << std::hex << std::setfill('0') << std::setw(16) << distribution(generator)
             << std::setw(16) << distribution(generator);
    return asset_id.str();
}

bool ImageAssetService::BuildUploadResponse(const Json::Value& request, Json::Value& response)
{
    // 先完成身份和房间成员校验，再检查资源声明，避免未授权用户消耗签名额度。
    if (!VerifyRoomMember(request))
    {
        response["error"] = message::ErrorCodes::TokenInvalid;
        return false;
    }

    std::size_t max_file_size = 0;
    unsigned int max_width = 0;
    unsigned int max_height = 0;
    ReadImageLimits(max_file_size, max_width, max_height);
    const std::string suffix = NormalizeSuffix(request.get("suffix", "").asString());
    const std::string mime_type = request.get("mime_type", "").asString();
    const Json::UInt64 file_size = request.get("file_size", 0).asUInt64();
    const unsigned int width = request.get("width", 0).asUInt();
    const unsigned int height = request.get("height", 0).asUInt();
    if (!IsAllowedSuffix(suffix) || mime_type != MimeTypeForSuffix(suffix) ||
        file_size == 0 || file_size > max_file_size || width == 0 || width > max_width ||
        height == 0 || height > max_height)
    {
        response["error"] = message::ErrorCodes::Error_Json;
        return false;
    }

    const std::string asset_id = GenerateAssetId();
    const std::string room_id = request["room_id"].asString();
    const std::string object_reference = BuildObjectReference(room_id, asset_id, suffix);
    const long long upload_expire_seconds = ReadExpireSeconds("UploadExpireSeconds", 600);
    const long long expire_time = static_cast<long long>(std::time(nullptr)) + upload_expire_seconds;
    const std::string upload_url = BuildSignedUrl("PUT", object_reference, mime_type, expire_time);
    if (upload_url.empty())
    {
        response["error"] = message::ErrorCodes::ServerInternalErr;
        return false;
    }

    response["error"] = message::ErrorCodes::SUCCESS;
    response["asset_id"] = asset_id;
    response["asset_ref"] = object_reference;
    response["url"] = upload_url;
    response["mime_type"] = mime_type;
    response["expires_in"] = static_cast<Json::Int64>(upload_expire_seconds);
    return true;
}

bool ImageAssetService::BuildDownloadResponse(const Json::Value& request, Json::Value& response)
{
    // 下载同样要求房间成员资格；即使知道 asset_id，也不能跨房间请求签名。
    if (!VerifyRoomMember(request))
    {
        response["error"] = message::ErrorCodes::TokenInvalid;
        return false;
    }

    const std::string asset_id = request.get("asset_id", "").asString();
    const std::string object_reference = request.get("asset_ref", "").asString();
    const std::string room_id = request["room_id"].asString();
    if (!IsHexAssetId(asset_id))
    {
        response["error"] = message::ErrorCodes::Error_Json;
        return false;
    }

    std::string object_name;
    std::string mime_type;
    if (!ParseObjectReference(room_id, asset_id, object_reference, object_name, mime_type))
    {
        response["error"] = message::ErrorCodes::Error_Json;
        return false;
    }

    const long long download_expire_seconds = ReadExpireSeconds("DownloadExpireSeconds", 300);
    const long long expire_time = static_cast<long long>(std::time(nullptr)) + download_expire_seconds;
    const std::string download_url = BuildSignedUrl("GET", object_name, "", expire_time);
    if (download_url.empty())
    {
        response["error"] = message::ErrorCodes::ServerInternalErr;
        return false;
    }

    response["error"] = message::ErrorCodes::SUCCESS;
    response["url"] = download_url;
    response["mime_type"] = mime_type;
    response["expires_in"] = static_cast<Json::Int64>(download_expire_seconds);
    return true;
}
