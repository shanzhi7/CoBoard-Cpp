#include "Logger.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <thread>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace
{
// 日志状态由同一把锁保护，保证多线程写控制台和文件时不会交错。
std::mutex g_mutex;
std::ofstream g_file;
std::string g_service_name = "SyncCanvas";
bool g_initialized = false;

// 只保留源文件名，避免日志因绝对路径过长而难以阅读。
std::string FileName(const char* file)
{
    const std::string path = file == nullptr ? "unknown" : file;
    const std::string::size_type separator = path.find_last_of("/\\");
    return separator == std::string::npos ? path : path.substr(separator + 1);
}

void EnsureLogDirectory(const std::string& file_path)
{
    const std::string::size_type separator = file_path.find_last_of("/\\");
    if (separator == std::string::npos)
    {
        return;
    }

    const std::string directory = file_path.substr(0, separator);
    // 日志文件的父目录可能尚未存在；创建失败时仍允许输出到控制台。
#ifdef _WIN32
    _mkdir(directory.c_str());
#else
    mkdir(directory.c_str(), 0755);
#endif
}

bool IsAbsolutePath(const std::string& path)
{
#ifdef _WIN32
    return path.size() > 2 && ((path[0] >= 'A' && path[0] <= 'Z') ||
        (path[0] >= 'a' && path[0] <= 'z')) && path[1] == ':';
#else
    return !path.empty() && path[0] == '/';
#endif
}

std::string ExecutableDirectory()
{
#ifdef _WIN32
    char buffer[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
    {
        return std::string();
    }
    const std::string path(buffer, length);
#else
    char buffer[4096] = {};
    const ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (length <= 0)
    {
        return std::string();
    }
    const std::string path(buffer, static_cast<size_t>(length));
#endif
    const std::string::size_type separator = path.find_last_of("/\\");
    return separator == std::string::npos ? std::string() : path.substr(0, separator);
}

std::string ResolveLogPath(const std::string& file_path)
{
    // 配置绝对路径时原样使用；相对路径固定到程序目录，避免受当前工作目录影响。
    if (IsAbsolutePath(file_path))
    {
        return file_path;
    }

    const std::string executable_directory = ExecutableDirectory();
    if (executable_directory.empty())
    {
        return file_path;
    }

    return executable_directory + "/" + file_path;
}
}

Logger::LogLine::LogLine(Level level, const char* function, const char* file, int line)
    : _level(level), _function(function), _file(file), _line(line)
{
}

Logger::LogLine::~LogLine()
{
    // 统一在析构点写出，调用方只需使用 LOG_* 宏即可自然完成一条日志。
    Logger::Write(_level, _stream.str(), _function, _file, _line);
}

std::ostringstream& Logger::LogLine::Stream()
{
    return _stream;
}

bool Logger::Init(const std::string& service_name, const std::string& file_path)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_service_name = service_name;
    const std::string resolved_path = ResolveLogPath(file_path);
    EnsureLogDirectory(resolved_path);
    // 追加模式保留服务重启前的日志，二进制模式避免平台换行转换影响原始文本。
    g_file.open(resolved_path.c_str(), std::ios::out | std::ios::app | std::ios::binary);
    g_initialized = g_file.is_open();
    return g_initialized;
}

void Logger::Shutdown()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file.is_open())
    {
        g_file.flush();
        g_file.close();
    }
    g_initialized = false;
}

void Logger::Write(Level level, const std::string& message,
    const char* function, const char* file, int line)
{
    std::ostringstream output;
    output << "[" << Timestamp() << "]"
        << " [" << LevelName(level) << "]"
        << " [" << g_service_name << "]"
        << " [" << function << "]"
        << " [file=" << FileName(file) << ":" << line << "]"
        << " [thread=" << ThreadId() << "] " << message << '\n';

    const std::string text = output.str();
    // 组装文本不需要持锁，只有最终输出共享流时才锁定，缩短临界区。
    std::lock_guard<std::mutex> lock(g_mutex);
    std::ostream& console = level == Level::Error ? std::cerr : std::cout;
    console << text;
    console.flush();
    if (g_initialized && g_file.is_open())
    {
        g_file << text;
        g_file.flush();
    }
}

std::string Logger::LevelName(Level level)
{
    switch (level)
    {
    case Level::Debug: return "DEBUG";
    case Level::Info: return "INFO";
    case Level::Warn: return "WARN";
    case Level::Error: return "ERROR";
    }
    return "UNKNOWN";
}

std::string Logger::Timestamp()
{
    const auto now = std::chrono::system_clock::now();
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm local_time;
#ifdef _WIN32
    localtime_s(&local_time, &time);
#else
    localtime_r(&time, &local_time);
#endif
    std::ostringstream result;
    result << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S")
        << '.' << std::setfill('0') << std::setw(3) << milliseconds.count();
    return result.str();
}

std::string Logger::ThreadId()
{
    std::ostringstream result;
    result << std::this_thread::get_id();
    return result.str();
}
