#include "CanvasServer/ImageOperationHandler.h"
#include "CanvasServer/CSession.h"
#include "CanvasServer/ConfigMgr.h"
#include "CanvasServer/Room.h"
#include "CanvasServer/const.h"
#include "CanvasServer/message.pb.h"
#include "Logger/Logger.h"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdint>

namespace
{
const std::size_t kMaxOperationIdLength = 128; // 限制操作 ID，避免把任意大字符串写入历史和日志
const std::size_t kMaxItemIdLength = 128; // 限制图元 ID，确保房间索引和客户端状态可控
const std::size_t kMaxAssetIdLength = 128; // 限制资源 ID，防止伪造超长资源引用
const std::size_t kMaxAssetRefLength = 1024; // 资源引用可能包含对象路径，但不能无限增长
const std::size_t kMaxMimeTypeLength = 64; // MIME 类型只用于图片格式白名单判断
const float kMaxTransformCoordinate = 1000000.0F; // 限制坐标，避免异常浮点值污染房间状态
const float kMaxScale = 100.0F; // 限制缩放倍数，避免客户端生成不可渲染的图元

std::string ReadObjectPrefix()
{
	// 图片资源路径必须由网关和 CanvasServer 使用同一前缀拼接，禁止请求方自行选择 OSS 根目录。
	std::string prefix = ConfigMgr::Inst()["ImageOperation"]["ObjectPrefix"];
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

std::uint32_t ReadMaxImageDimension()
{
	// 配置异常时回退到需求中的 4096，避免服务启动后意外放宽图片尺寸限制。
	try
	{
		const std::string value = ConfigMgr::Inst()["ImageOperation"]["MaxImageDimension"];
		const std::uint32_t configured = value.empty() ? 4096U : static_cast<std::uint32_t>(std::stoul(value));
		return configured == 0 ? 4096U : configured;
	}
	catch (const std::exception&)
	{
		return 4096U;
	}
}

std::size_t ReadMaxPayloadBytes()
{
	// TCP 包头已有 MAX_LENGTH 上限，配置值只允许进一步收紧，不能绕过统一封包限制。
	try
	{
		const std::string value = ConfigMgr::Inst()["ImageOperation"]["MaxPayloadBytes"];
		const std::size_t configured = value.empty() ? MAX_LENGTH : static_cast<std::size_t>(std::stoull(value));
		return configured == 0 ? MAX_LENGTH : std::min<std::size_t>(configured, MAX_LENGTH);
	}
	catch (const std::exception&)
	{
		return MAX_LENGTH;
	}
}

bool IsHexSha256(const std::string& sha256)
{
	// SHA-256 以固定 64 位十六进制文本传输，长度和字符集都必须严格限制。
	if (sha256.size() != 64)
	{
		return false;
	}

	for (const char value : sha256)
	{
		if (!std::isxdigit(static_cast<unsigned char>(value)))
		{
			return false;
		}
	}
	return true;
}

bool IsSupportedMimeType(const std::string& mime_type)
{
	// CanvasServer 不读取图片二进制，只允许客户端约定的三种可预览格式进入房间历史。
	return mime_type == "image/png" || mime_type == "image/jpeg" || mime_type == "image/webp";
}

bool IsHexAssetId(const std::string& asset_id)
{
	// 资源 ID 由 GateServer 生成 32 位十六进制随机值，TCP 层必须再次验证以阻止伪造路径。
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

bool IsCanonicalAssetReference(const std::string& room_id,
	const std::string& asset_id,
	const std::string& asset_ref,
	const std::string& mime_type)
{
	// 引用必须精确绑定当前房间、资源 ID 和格式，避免把任意 OSS 对象写入房间历史。
	if (!IsHexAssetId(asset_id) || asset_ref.find("..") != std::string::npos ||
		asset_ref.find('?') != std::string::npos || asset_ref.find("//") != std::string::npos)
	{
		return false;
	}
	const std::string prefix = ReadObjectPrefix() + "/" + room_id + "/" + asset_id + ".";
	if (asset_ref.compare(0, prefix.size(), prefix) != 0)
	{
		return false;
	}
	const std::string suffix = asset_ref.substr(prefix.size());
	if (suffix.find('/') != std::string::npos)
	{
		return false;
	}
	if ((suffix == "png" && mime_type != "image/png") ||
		((suffix == "jpg" || suffix == "jpeg") && mime_type != "image/jpeg") ||
		(suffix == "webp" && mime_type != "image/webp"))
	{
		return false;
	}
	return suffix == "png" || suffix == "jpg" || suffix == "jpeg" || suffix == "webp";
}

bool IsFiniteTransform(const message::ImageTransform& transform)
{
	// 浮点字段来自不可信客户端；先拒绝 NaN/Inf，再限制坐标和缩放范围，避免污染其他成员的画布状态。
	const float values[] = {
		transform.x(), transform.y(), transform.width(), transform.height(),
		transform.scale_x(), transform.scale_y(), transform.rotation()
	};

	for (const float value : values)
	{
		if (!std::isfinite(value) || std::fabs(value) > kMaxTransformCoordinate)
		{
			return false;
		}
	}

	return transform.width() > 0.0F && transform.height() > 0.0F
		&& transform.scale_x() > 0.0F && transform.scale_x() <= kMaxScale
		&& transform.scale_y() > 0.0F && transform.scale_y() <= kMaxScale;
}

bool ValidateImageItem(const message::ImageItem& item, bool require_asset_metadata, bool require_item_id = true)
{
	// 更新操作可以只提交 target_item_id，因此 item_id 是否必填由调用方按操作类型决定。
	if ((require_item_id && item.item_id().empty()) || item.item_id().size() > kMaxItemIdLength)
	{
		return false;
	}

	if (!item.has_transform() || !IsFiniteTransform(item.transform()))
	{
		return false;
	}

	if (!require_asset_metadata)
	{
		return true;
	}

	return !item.asset_id().empty() && item.asset_id().size() <= kMaxAssetIdLength
		&& !item.asset_ref().empty() && item.asset_ref().size() <= kMaxAssetRefLength
		&& IsHexSha256(item.asset_sha256())
		&& item.mime_type().size() <= kMaxMimeTypeLength
		&& IsSupportedMimeType(item.mime_type())
		&& item.original_width() > 0 && item.original_width() <= ReadMaxImageDimension()
		&& item.original_height() > 0 && item.original_height() <= ReadMaxImageDimension();
}
}

void ImageOperationHandler::Handle(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data)
{
	(void)msg_id; // 回调接口保留消息 ID，图片处理只需要正文并统一广播响应 ID。
	if (!session || session->IsClosed())
	{
		return;
	}

	message::ImageOperation operation;
	if (msg_data.size() > ReadMaxPayloadBytes() || !operation.ParseFromString(msg_data))
	{
		LOG_WARN_CTX("ImageOperationHandler::Handle", "图片操作解析失败或超过最大包长");
		return;
	}

	const int session_uid = session->GetUserId();
	if (session_uid == 0)
	{
		LOG_WARN_CTX("ImageOperationHandler::Handle", "未登录会话尝试操作图片");
		return;
	}

	// UID 必须以 TCP 会话绑定值为准，不能相信客户端随包提交的身份字段。
	if (operation.uid() != session_uid)
	{
		LOG_WARN_CTX("ImageOperationHandler::Handle", "图片操作 UID 不匹配 session_uid=" << session_uid
			<< " request_uid=" << operation.uid());
		session->Close();
		return;
	}

	auto room = session->GetRoomLocked();
	if (!room)
	{
		LOG_WARN_CTX("ImageOperationHandler::Handle", "图片操作会话不在房间 uid=" << session_uid);
		return;
	}

	// room_id 是防串房间字段；即使当前 Session 已绑定房间，也必须校验请求中的值。
	if (operation.room_id().empty() || operation.room_id() != room->GetRoomId())
	{
		LOG_WARN_CTX("ImageOperationHandler::Handle", "图片操作房间不匹配 uid=" << session_uid
			<< " room_id=" << operation.room_id());
		return;
	}

	if (!room->CanEdit(session_uid))
	{
		LOG_WARN_CTX("ImageOperationHandler::Handle", "图片操作无编辑权限 uid=" << session_uid
			<< " room_id=" << room->GetRoomId());
		return;
	}

	if (operation.operation_id().empty() || operation.operation_id().size() > kMaxOperationIdLength)
	{
		LOG_WARN_CTX("ImageOperationHandler::Handle", "图片操作 ID 无效 uid=" << session_uid);
		return;
	}

	// 创建必须携带完整资源元数据；更新只允许携带变换，删除只需要目标 item_id。
	if (operation.operation_type() == message::IMAGE_CREATE)
	{
		if (!operation.target_item_id().empty()
			|| !operation.has_item() || !ValidateImageItem(operation.item(), true)
			|| !IsCanonicalAssetReference(room->GetRoomId(), operation.item().asset_id(),
				operation.item().asset_ref(), operation.item().mime_type()))
		{
			LOG_WARN_CTX("ImageOperationHandler::Handle", "创建图片字段无效 uid=" << session_uid);
			return;
		}
	}
	else if (operation.operation_type() == message::IMAGE_UPDATE_TRANSFORM)
	{
		if (!operation.has_item() || !ValidateImageItem(operation.item(), false, false)
			|| (operation.target_item_id().empty() && operation.item().item_id().empty())
			|| operation.target_item_id().size() > kMaxItemIdLength
			|| (!operation.target_item_id().empty() && !operation.item().item_id().empty()
				&& operation.target_item_id() != operation.item().item_id()))
		{
			LOG_WARN_CTX("ImageOperationHandler::Handle", "更新图片变换字段无效 uid=" << session_uid);
			return;
		}
	}
	else if (operation.operation_type() == message::IMAGE_DELETE)
	{
		if (operation.target_item_id().empty() || operation.target_item_id().size() > kMaxItemIdLength)
		{
			LOG_WARN_CTX("ImageOperationHandler::Handle", "删除图片目标无效 uid=" << session_uid);
			return;
		}
		if (operation.has_item() && !operation.item().item_id().empty()
			&& operation.item().item_id() != operation.target_item_id())
		{
			LOG_WARN_CTX("ImageOperationHandler::Handle", "删除图片 ID 字段不一致 uid=" << session_uid);
			return;
		}
	}
	else
	{
		LOG_WARN_CTX("ImageOperationHandler::Handle", "未知图片操作类型 uid=" << session_uid);
		return;
	}

	// Room 在同一把锁内完成重复 item 检查、状态变更、序号分配和正文序列化，避免先校验后写入的竞态。
	std::string normalized_body;
	if (!room->ApplyImageOperation(operation, &normalized_body))
	{
		LOG_WARN_CTX("ImageOperationHandler::Handle", "图片操作与当前房间状态冲突或序列化失败 uid=" << session_uid
			<< " room_id=" << room->GetRoomId());
		return;
	}

	// Room 已在状态锁内完成序列化，并且按操作类型自行维护快照式图片历史
	// （变换更新原位改写创建条目，不追加移动记录）；这里只负责在线广播。
	room->Broadcast(normalized_body, ID_IMAGE_OPERATION_RSP, session_uid);
}
