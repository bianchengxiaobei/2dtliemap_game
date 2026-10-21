#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace zl::net {

// M13-T8：客户端持久玩家身份。
//
// 身份是服务器识别跨连接/跨重启玩家的唯一键；不是密码，也不参与战斗数值。
// 生成值为 32 个十六进制字符（128 bit），落盘后由客户端每次连接复用。
inline constexpr std::size_t kMaxPlayerIdentityBytes = 64;

bool IsValidPlayerIdentity(std::string_view identity);
std::string GeneratePlayerIdentity();

// 读取已有身份；文件不存在/为空/损坏时生成新身份并写回。
// 写盘失败时仍返回内存中新身份，error 记录原因；调用方应继续连接。
std::string LoadOrCreatePlayerIdentity(const std::filesystem::path& path,
                                       std::string* error = nullptr);

}  // namespace zl::net
