#pragma once

#include <sstream>
#include <string>

// 统一输出服务日志，同时保留控制台输出和可选的文件输出。
class Logger
{
public:
    // 日志级别按严重程度递增，Error 会额外写入标准错误流。
    enum class Level
    {
        Debug,
        Info,
        Warn,
        Error
    };

    // 一条日志的 RAII 构造器：析构时提交 Stream 中累积的内容。
    class LogLine
    {
    public:
        LogLine(Level level, const char* function, const char* file, int line);
        ~LogLine();

        std::ostringstream& Stream();

    private:
        Level _level;
        const char* _function;
        const char* _file;
        int _line;
        std::ostringstream _stream;
    };

    // 初始化服务名和日志文件；相对路径以可执行文件目录为基准。
    static bool Init(const std::string& service_name, const std::string& file_path);
    // 刷新并关闭日志文件，允许服务在退出前显式释放日志资源。
    static void Shutdown();
    // 写入一条已经组装完成的日志，并附加时间、线程和源代码位置。
    static void Write(Level level, const std::string& message,
        const char* function, const char* file, int line);

private:
    // 将级别转换为稳定的文本，避免调用方依赖枚举底层数值。
    static std::string LevelName(Level level);
    // 生成带毫秒精度的本地时间。
    static std::string Timestamp();
    // 返回当前线程标识，用于定位并发日志来源。
    static std::string ThreadId();
};

// 临时 LogLine 对象会在完整表达式结束时析构，因此支持流式拼接消息。
#define LOG_DEBUG(message) \
    Logger::LogLine(Logger::Level::Debug, __func__, __FILE__, __LINE__).Stream() << message
#define LOG_INFO(message) \
    Logger::LogLine(Logger::Level::Info, __func__, __FILE__, __LINE__).Stream() << message
#define LOG_WARN(message) \
    Logger::LogLine(Logger::Level::Warn, __func__, __FILE__, __LINE__).Stream() << message
#define LOG_ERROR(message) \
    Logger::LogLine(Logger::Level::Error, __func__, __FILE__, __LINE__).Stream() << message

#define LOG_DEBUG_CTX(context, message) \
    Logger::LogLine(Logger::Level::Debug, context, __FILE__, __LINE__).Stream() << message
#define LOG_INFO_CTX(context, message) \
    Logger::LogLine(Logger::Level::Info, context, __FILE__, __LINE__).Stream() << message
#define LOG_WARN_CTX(context, message) \
    Logger::LogLine(Logger::Level::Warn, context, __FILE__, __LINE__).Stream() << message
#define LOG_ERROR_CTX(context, message) \
    Logger::LogLine(Logger::Level::Error, context, __FILE__, __LINE__).Stream() << message
