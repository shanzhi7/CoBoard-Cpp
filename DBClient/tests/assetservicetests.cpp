#include "assetserviceprotocol.h"
#include "servicecommandclient.h"
#include "servicecommandserver.h"
#include "imageassetmanager.h"

#include <functional>
#include <QBuffer>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QLocalSocket>
#include <QLockFile>
#include <QProcess>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtEndian>
#include <QtTest>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace
{
bool WaitUntil(const std::function<bool()>& predicate, int timeout_ms = 10000) // 在事件循环中等待集成条件。
{
    // 1. QTest 等待会处理事件，服务和 HTTP 测试端点仍可推进。
    QElapsedTimer elapsed; // 等待上限。
    elapsed.start();
    bool is_complete = predicate(); // 具有副作用的条件只求值一次。
    while (!is_complete && elapsed.elapsed() < timeout_ms)
    {
        QTest::qWait(10);
        is_complete = predicate();
    }
    return is_complete;
}

QByteArray ImageBytes(QRgb color) // 创建不依赖外部图片的真实 PNG。
{
    // 1. 所有图片走 Qt 格式插件及真实解码校验。
    QImage image(32, 24, QImage::Format_RGB32); // 测试像素。
    image.fill(color);
    QByteArray data; // PNG 编码。
    QBuffer buffer(&data); // 内存设备。
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return data;
}

class Peer : public QObject
{
public:
    Peer() // 测试控制协议，使用生产分帧器。
    {
        // 1. 每个测试客户端有独立缓冲与响应关联。
        _socket = new QLocalSocket;
        _channel = new AssetIpcChannel(_socket, this);
        connect(_channel, &AssetIpcChannel::sigFrameReady, this, [this](QJsonObject message, QByteArray binary) {
            _responses.insert(message.value("requestId").toString(), {message, binary});
        });
    }
    bool Connect() // 等待代理实际监听。
    {
        // 1. 重试未发现端点，不删除任何端点。
        return WaitUntil([this] {
            if (_socket->state() == QLocalSocket::ConnectedState)
            {
                return true;
            }
            if (_socket->state() == QLocalSocket::UnconnectedState)
            {
                _socket->connectToServer(AssetServiceProtocol::ServerName());
            }
            return false;
        });
    }
    QString Send(const QString& command, const QJsonObject& payload = {}, const QByteArray& binary = {}, int version = 1) // 返回测试请求 UUID。
    {
        // 1. 与客户端相同的控制字段。
        const QString id = AssetServiceProtocol::NewId(); // 请求关联。
        _channel->Send({{"version", version}, {"requestId", id}, {"command", command}, {"payload", payload}}, binary);
        return id;
    }
    QPair<QJsonObject, QByteArray> Take(const QString& id) // 返回并移除接收数据，避免测试缓冲增长。
    {
        // 1. 工作线程及网络回调需要持续事件处理。
        if (!WaitUntil([this, id] { return _responses.contains(id); }))
        {
            return {};
        }
        return _responses.take(id);
    }
    QJsonObject Request(const QString& command, const QJsonObject& payload = {}, const QByteArray& binary = {}) // 控制请求便捷入口。
    {
        // 1. 图片字节由需要的用例单独验证。
        return Take(Send(command, payload, binary)).first;
    }
    bool Hello() // 等待真实目录迁移完成。
    {
        // 1. Initializing 期间不发送业务指令。
        return WaitUntil([this] {
            const auto response = Request("Hello", {{"rootDigest", AssetServiceProtocol::RootDigest()}}); // 握手结果。
            return response.value("payload").toObject().value("state").toString() == "Ready";
        }, 60000);
    }
    void Close() // 模拟 UI 退出。
    {
        // 1. 主动断开使代理释放本连接资源。
        _socket->abort();
    }
    QLocalSocket* _socket = nullptr; // 当前测试连接。
    AssetIpcChannel* _channel = nullptr; // 生产通道实现。
    QHash<QString, QPair<QJsonObject, QByteArray>> _responses; // 响应关联。
};

class TestOss : public QObject
{
public:
    TestOss() // 本地 HTTP 端点，不需要真实 OSS 凭据。
    {
        // 1. 根据真实 HTTP 请求返回图片或接收上传正文。
        _server.listen(QHostAddress::LocalHost, 0);
        connect(&_server, &QTcpServer::newConnection, this, [this] {
            while (_server.hasPendingConnections())
            {
                auto* socket = _server.nextPendingConnection(); // HTTP 连接。
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                    QByteArray data = socket->property("requestData").toByteArray() + socket->readAll(); // 测试请求缓冲。
                    const int header_end = data.indexOf("\r\n\r\n"); // HTTP 头结束。
                    if (header_end < 0 || socket->property("responded").toBool())
                    {
                        socket->setProperty("requestData", data);
                        return;
                    }
                    if (data.startsWith("PUT "))
                    {
                        qint64 content_length = 0; // 上传正文长度。
                        for (const QByteArray& line : data.left(header_end).split('\n'))
                        {
                            if (line.trimmed().toLower().startsWith("content-length:"))
                            {
                                content_length = line.mid(line.indexOf(':') + 1).trimmed().toLongLong();
                            }
                        }
                        if (data.size() < header_end + 4 + content_length)
                        {
                            socket->setProperty("requestData", data);
                            return;
                        }
                        _uploaded_data = data.mid(header_end + 4, content_length);
                        ++_put_count;
                    } else
                    {
                        ++_get_count;
                    }
                    socket->setProperty("responded", true);
                    const bool is_put = data.startsWith("PUT "); // 回复类型。
                    QTimer::singleShot(_delay_ms, socket, [this, socket, is_put] {
                        const QByteArray body = is_put ? QByteArray() : _image_data; // 真实对象正文。
                        socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                        socket->disconnectFromHost();
                    });
                });
            }
        });
    }
    QString Url() const // 只供测试使用的对象 URL。
    {
        // 1. 服务仍执行 HTTP PUT/GET 完整流程。
        return QString("http://127.0.0.1:%1/image").arg(_server.serverPort());
    }
    QTcpServer _server; // 本地伪 OSS。
    QByteArray _image_data; // GET 正文。
    QByteArray _uploaded_data; // 实际 PUT 正文。
    int _get_count = 0; // 验证共享下载次数。
    int _put_count = 0; // 验证不重放上传。
    int _delay_ms = 200; // 让并发请求进入共享等待列表。
};

void StartCandidate(QProcess& process) // 同一测试程序也实现后台模式。
{
    // 1. 目录覆盖只用于隔离测试缓存。
    process.start(QCoreApplication::applicationFilePath(), {"--asset-service", "--asset-root", AssetServiceProtocol::CacheRoot()});
}

void KillService(qint64 pid) // 仅终止测试创建的缓存服务。
{
    // 1. 崩溃恢复用例必须留下实际的过期文件锁。
#ifdef Q_OS_WIN
    HANDLE handle = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, static_cast<DWORD>(pid)); // 测试子进程句柄。
    if (handle)
    {
        TerminateProcess(handle, 9);
        WaitForSingleObject(handle, 5000);
        CloseHandle(handle);
    }
#else
    QProcess::execute("kill", {"-9", QString::number(pid)});
#endif
}
}

class AssetServiceTests : public QObject
{
    Q_OBJECT
private slots:
    void Framing(); // 半包、粘包和超长输入。
    void Initialization(); // 初始化延迟与未来数据库版本拒绝。
    void Integration(); // 生命周期、迁移、下载、上传、取消和水位。
    void ClientRecovery(); // 自启动、服务崩溃明确失败及重新握手。
};

void AssetServiceTests::Framing()
{
    // 1. 每个切割位置都必须保持半包，并正确消费粘包尾部。
    const QJsonObject request{{"requestId", AssetServiceProtocol::NewId()}, {"command", "ImportImageData"}}; // 控制正文。
    const QByteArray binary = ImageBytes(qRgb(20, 120, 80)); // 原始编码。
    const QByteArray frame = AssetServiceProtocol::EncodeFrame(request, binary); // 生产帧。
    for (int split = 0; split < frame.size(); ++split)
    {
        QByteArray buffer = frame.left(split); // 任意半包。
        QJsonObject result; // JSON 输出。
        QByteArray result_binary; // 二进制输出。
        QString error; // 格式错误。
        QVERIFY(!AssetServiceProtocol::TakeFrame(buffer, &result, &result_binary, &error));
        QVERIFY(error.isEmpty());
        QCOMPARE(buffer, frame.left(split));
        buffer += frame.mid(split) + frame;
        QVERIFY(AssetServiceProtocol::TakeFrame(buffer, &result, &result_binary, &error));
        QCOMPARE(result, request);
        QCOMPARE(result_binary, binary);
        QCOMPARE(buffer, frame);
    }
    // 2. 过大长度及非法 JSON 不能分配大正文或进入指令处理。
    QByteArray buffer(8, '\0'); // 超长长度头。
    qToBigEndian<quint32>(AssetServiceProtocol::MAX_JSON_BYTES + 1, buffer.data());
    QJsonObject result; // 接收对象。
    QByteArray result_binary; // 图片输出。
    QString error; // 校验原因。
    QVERIFY(!AssetServiceProtocol::TakeFrame(buffer, &result, &result_binary, &error));
    QVERIFY(!error.isEmpty());
    // 3. 安装包必须能通过 JPEG、WebP 及 PNG 的真实插件解码。
    for (const QByteArray& format : {QByteArray("JPEG"), QByteArray("WEBP"), QByteArray("PNG")})
    {
        QImage image(30, 20, QImage::Format_RGB32); // 真实像素。
        image.fill(qRgb(100, 30, 40));
        QByteArray encoded; // 格式编码。
        QBuffer output(&encoded); // 内存写入。
        output.open(QIODevice::WriteOnly);
        QVERIFY(image.save(&output, format.constData()));
        QCOMPARE(AssetServiceProtocol::ValidateImage(encoded)._status, "ok");
    }
    buffer = QByteArray(8, '\0') + "[]";
    qToBigEndian<quint32>(2, buffer.data());
    QVERIFY(!AssetServiceProtocol::TakeFrame(buffer, &result, &result_binary, &error));
    QVERIFY(!error.isEmpty());
}

void AssetServiceTests::Initialization()
{
    // 1. 用测试专属 SQL 写锁延迟初始化，监听必须仍能返回 Initializing。
    QTemporaryDir directory; // 独立目录。
    QVERIFY(directory.isValid());
    QCoreApplication::instance()->setProperty("assetServiceRoot", directory.path());
    {
        QSqlDatabase blocker = QSqlDatabase::addDatabase("QSQLITE", "asset_test_blocker"); // 仅测试故障注入，UI 产品代码无 SQL。
        blocker.setDatabaseName(QDir(directory.path()).filePath("canvas-assets.db"));
        QVERIFY(blocker.open());
        QSqlQuery query(blocker); // 人为延迟后台写入。
        QVERIFY(query.exec("PRAGMA journal_mode=WAL"));
        query.finish();
        QVERIFY(query.exec("BEGIN IMMEDIATE"));
        QProcess candidate; // 生产初始化逻辑。
        StartCandidate(candidate);
        Peer peer; // 服务事件循环不应被 SQL 写锁阻塞。
        QVERIFY(peer.Connect());
        const auto hello = peer.Request("Hello", {{"rootDigest", AssetServiceProtocol::RootDigest()}}); // 迁移前握手。
        QCOMPARE(hello.value("payload").toObject().value("state").toString(), "Initializing");
        QVERIFY(query.exec("ROLLBACK"));
        QVERIFY(peer.Hello());
        peer.Close();
        candidate.kill();
        QVERIFY(WaitUntil([&] { return candidate.state() == QProcess::NotRunning; }));
        // 2. 未来版本不能被旧代理改表或目录恢复流程覆盖。
        QVERIFY(query.exec("PRAGMA user_version=2"));
        query.finish();
        blocker.close();
    }
    QSqlDatabase::removeDatabase("asset_test_blocker");
    QProcess incompatible; // 崩溃锁及未来版本的启动处理。
    StartCandidate(incompatible);
    QVERIFY(WaitUntil([&] { return incompatible.state() == QProcess::NotRunning; }));
    QCOMPARE(incompatible.exitCode(), 2);
    QLockFile lock(QDir(directory.path()).filePath("asset-service.lock")); // 错误退出仍需释放锁。
    lock.setStaleLockTime(0);
    QVERIFY(lock.tryLock());
    lock.unlock();
}

void AssetServiceTests::Integration()
{
    // 1. 被锁占用时不得创建数据库；并发启动只能有一个掌控者。
    QTemporaryDir directory; // 测试根目录。
    QVERIFY(directory.isValid());
    QCoreApplication::instance()->setProperty("assetServiceRoot", directory.path());
    QLockFile external_lock(QDir(directory.path()).filePath("asset-service.lock")); // 模拟存活掌控者的锁。
    external_lock.setStaleLockTime(0);
    QVERIFY(external_lock.tryLock());
    QProcess locked_candidate; // 竞争失败者。
    StartCandidate(locked_candidate);
    QVERIFY(WaitUntil([&] { return locked_candidate.state() == QProcess::NotRunning; }));
    QCOMPARE(locked_candidate.exitCode(), 0);
    QVERIFY(!QFileInfo::exists(QDir(directory.path()).filePath("canvas-assets.db")));
    external_lock.unlock();
    const QByteArray imported_data = ImageBytes(qRgb(30, 90, 220)); // 旧目录图片。
    const QString imported_sha = AssetServiceProtocol::ValidateImage(imported_data)._record._asset_sha256; // 内容寻址名称。
    QDir().mkpath(QDir(directory.path()).filePath("canvas-assets"));
    QFile legacy(QDir(directory.path()).filePath("canvas-assets/old_resource_id")); // 旧 ID 文件。
    QVERIFY(legacy.open(QIODevice::WriteOnly));
    legacy.write(imported_data);
    legacy.close();
    QProcess first_candidate; // 同时启动候选者。
    QProcess second_candidate; // 同时启动候选者。
    StartCandidate(first_candidate);
    StartCandidate(second_candidate);
    Peer first; // 首个 UI。
    Peer second; // 后续 UI。
    QVERIFY(first.Connect());
    QVERIFY(first.Hello());
    QVERIFY(second.Connect());
    QVERIFY(second.Hello());
    QVERIFY(WaitUntil([&] { return (first_candidate.state() == QProcess::NotRunning) != (second_candidate.state() == QProcess::NotRunning); }));
    QVERIFY(!QFileInfo::exists(legacy.fileName()));
    const auto migrated = first.Take(first.Send("LoadAsset", {{"sha256", imported_sha}})); // 验证迁移真实字节。
    QCOMPARE(migrated.first.value("status").toString(), "ok");
    QCOMPARE(migrated.second, imported_data);
    const auto incompatible = first.Take(first.Send("Hello", {{"rootDigest", AssetServiceProtocol::RootDigest()}}, {}, 99)).first; // 错版本拒绝。
    QCOMPARE(incompatible.value("errorCode").toString(), "IncompatibleVersion");
    const auto prepared = first.Request("ImportImageData", {}, imported_data); // 重复导入应去重。
    const QString handle = prepared.value("payload").toObject().value("assetHandle").toString(); // 绑定客户端的句柄。
    QVERIFY(!handle.isEmpty());
    QCOMPARE(first.Request("GetCacheStats").value("payload").toObject().value("assetCount").toInt(), 1);

    // 2. 两客户端共享下载，取消一个等待者不影响另一请求。
    TestOss oss; // 真 HTTP 测试端点。
    oss._image_data = ImageBytes(qRgb(120, 180, 50));
    const QString download_sha = AssetServiceProtocol::ValidateImage(oss._image_data)._record._asset_sha256; // 下载摘要。
    const QJsonObject download{{"sha256", download_sha}, {"url", oss.Url()}}; // 临时 GET 描述。
    const QString first_download = first.Send("DownloadAsset", download); // 一个等待者。
    const QString second_download = second.Send("DownloadAsset", download); // 共享等待者。
    QVERIFY(WaitUntil([&] { return oss._get_count == 1; }));
    first.Request("CancelRequest", {{"requestId", first_download}});
    QCOMPARE(first.Take(first_download).first.value("errorCode").toString(), "Canceled");
    QCOMPARE(second.Take(second_download).second, oss._image_data);
    QCOMPARE(oss._get_count, 1);
    QCOMPARE(second.Request("DownloadAsset", download).value("status").toString(), "ok");
    QCOMPARE(oss._get_count, 1);
    // 单连接请求上限必须允许取消指令继续执行。
    oss._image_data = ImageBytes(qRgb(120, 10, 240));
    oss._delay_ms = 1000;
    const QString queued_sha = AssetServiceProtocol::ValidateImage(oss._image_data)._record._asset_sha256; // 新内容避免缓存命中。
    QStringList queued_requests; // 填满十六个业务请求。
    for (int index = 0; index < 16; ++index)
    {
        queued_requests.append(second.Send("DownloadAsset", {{"sha256", queued_sha}, {"url", oss.Url()}}));
    }
    QCOMPARE(second.Request("DownloadAsset", {{"sha256", queued_sha}, {"url", oss.Url()}}).value("errorCode").toString(), "Busy");
    second.Request("CancelRequest", {{"requestId", queued_requests.takeLast()}});
    for (const QString& id : queued_requests)
    {
        QCOMPARE(second.Take(id).first.value("status").toString(), "ok");
    }
    QCOMPARE(oss._get_count, 2);
    oss._delay_ms = 200;
    // 同一个 requestId 在不同连接上仍独立关联。
    const QString shared_id = AssetServiceProtocol::NewId(); // 故意复用 ID 验证连接隔离。
    first._channel->Send({{"version", 1}, {"requestId", shared_id}, {"command", "LoadAsset"}, {"payload", QJsonObject{{"sha256", imported_sha}}}});
    second._channel->Send({{"version", 1}, {"requestId", shared_id}, {"command", "LoadAsset"}, {"payload", QJsonObject{{"sha256", QString(64, '0')}}}});
    QCOMPARE(first.Take(shared_id).first.value("status").toString(), "ok");
    QCOMPARE(second.Take(shared_id).first.value("status").toString(), "miss");
    Peer malformed; // 协议错误仅关闭错误连接。
    QVERIFY(malformed.Connect());
    QVERIFY(malformed.Hello());
    QByteArray invalid_header(8, '\0'); // 超长二进制声明。
    qToBigEndian<quint32>(10, invalid_header.data());
    qToBigEndian<quint32>(AssetServiceProtocol::MAX_IMAGE_BYTES + 1, invalid_header.data() + 4);
    malformed._socket->write(invalid_header);
    QVERIFY(WaitUntil([&] { return malformed._socket->state() == QLocalSocket::UnconnectedState; }));
    QCOMPARE(first.Request("GetCacheStats").value("status").toString(), "ok");
    QJsonObject upload{{"assetHandle", handle}, {"url", oss.Url()}, {"sha256", imported_sha}, {"mimeType", "image/png"}, {"byteSize", imported_data.size()}}; // PUT 描述。
    QCOMPARE(second.Request("UploadAsset", upload).value("errorCode").toString(), "InvalidHandle");
    const QString upload_request = first.Send("UploadAsset", upload); // 实际上传另持文件引用。
    QVERIFY(WaitUntil([&] { return oss._put_count == 1; }));
    first.Request("ReleaseAsset", {{"assetHandle", handle}});
    QCOMPARE(first.Take(upload_request).first.value("status").toString(), "ok");
    QCOMPARE(oss._uploaded_data, imported_data);
    first.Request("ReleaseAsset", {{"assetHandle", handle}});
    // 已发送导入结果再取消仍能回收迟到的句柄。
    const QString late_import = first.Send("ImportImageData", {}, imported_data); // 模拟取消与响应竞争。
    const auto late_response = first.Take(late_import).first; // 已成功持有句柄。
    first.Request("CancelRequest", {{"requestId", late_import}});
    upload.insert("assetHandle", late_response.value("payload").toObject().value("assetHandle"));
    QCOMPARE(first.Request("UploadAsset", upload).value("errorCode").toString(), "InvalidHandle");
    first.Close();
    QCOMPARE(second.Request("GetCacheStats").value("status").toString(), "ok");

    // 3. 真实容量越过 300 MiB；所有引用保护时允许暂时超限。
    QStringList handles; // 水位期间保护所有资源。
    QStringList hashes; // 验证 LRU 与引用保护。
    for (int index = 0; index < 40; ++index)
    {
        QByteArray large_image = ImageBytes(qRgb(index, 100, 200)); // PNG 允许尾部填充，编码大小真实参与统计。
        large_image.append(QByteArray(8 * 1024 * 1024, static_cast<char>(index)));
        const auto result = second.Request("ImportImageData", {}, large_image); // 每次内容摘要不同。
        QCOMPARE(result.value("status").toString(), "ok");
        handles.append(result.value("payload").toObject().value("assetHandle").toString());
        hashes.append(result.value("payload").toObject().value("sha256").toString());
    }
    const auto protected_stats = second.Request("GetCacheStats").value("payload").toObject(); // 高水位容量。
    QVERIFY(protected_stats.value("totalBytes").toInteger() > 300LL * 1024 * 1024);
    for (int index = 1; index < handles.size(); ++index)
    {
        QCOMPARE(second.Request("ReleaseAsset", {{"assetHandle", handles[index]}}).value("status").toString(), "ok");
    }
    QVERIFY(WaitUntil([&] {
        const auto stats = second.Request("GetCacheStats").value("payload").toObject(); // 周期清理推进。
        return stats.value("totalBytes").toInteger() < 100LL * 1024 * 1024 && !stats.value("needsCleanup").toBool();
    }, 20000));
    QCOMPARE(second.Request("LoadAsset", {{"sha256", hashes.first()}}).value("status").toString(), "ok");
    QCOMPARE(second.Request("LoadAsset", {{"sha256", hashes.last()}}).value("status").toString(), "ok");
    QCOMPARE(second.Request("LoadAsset", {{"sha256", hashes[1]}}).value("status").toString(), "miss");
    // 4. 首个 UI 退出不影响第二个；最后退出等待三十秒再结束代理。
    second.Close();
    QVERIFY(WaitUntil([&] { return first_candidate.state() == QProcess::NotRunning && second_candidate.state() == QProcess::NotRunning; }, 40000));
    QCOMPARE(first_candidate.exitCode(), 0);
    QCOMPARE(second_candidate.exitCode(), 0);
    QVERIFY(external_lock.tryLock());
    external_lock.unlock();
}

void AssetServiceTests::ClientRecovery()
{
    // 1. 生产客户端自启动代理，未完成任务在服务崩溃后得到明确失败。
    QTemporaryDir directory; // 独立测试根目录。
    QVERIFY(directory.isValid());
    QCoreApplication::instance()->setProperty("assetServiceRoot", directory.path());
    auto client = ServiceCommandClient::getInstance(); // 仓库单例入口。
    QHash<QString, QJsonObject> responses; // 异步业务结果。
    QString instance_id; // 当前代理实例。
    connect(client.get(), &ServiceCommandClient::sigResponseReady, this, [&](QString id, QJsonObject response, QByteArray) { responses.insert(id, response); });
    connect(client.get(), &ServiceCommandClient::sigServiceReady, this, [&](QString id) { instance_id = id; });
    const QString stats_id = client->SendCommand("GetCacheStats"); // 不显式启动后台进程。
    QVERIFY(WaitUntil([&] { return responses.contains(stats_id); }));
    QCOMPARE(responses.take(stats_id).value("status").toString(), "ok");
    // 2. 实际 UI 门面后台编码、IPC 导入、主线程 QPixmap 与缓存命中。
    {
        ImageAssetManager assets; // 不读取缓存文件的 UI 门面。
        ImageAssetInfo imported_info; // 导入元数据和句柄。
        bool is_import_ready = false; // 异步导入完成。
        bool is_cache_ready = false; // 异步缓存完成。
        bool has_failed = false; // 明确验证失败信号。
        connect(&assets, &ImageAssetManager::sigLocalAssetReady, this, [&](QString id, QString, ImageAssetInfo info, QPixmap pixmap) {
            is_import_ready = id == "clipboard-request" && !pixmap.isNull();
            imported_info = info;
        });
        connect(&assets, &ImageAssetManager::sigAssetReady, this, [&](QString id, QPixmap pixmap, QString sha, QSize size, QString) {
            is_cache_ready = id == "room-asset" && !pixmap.isNull() && sha == imported_info.asset_sha256 && size == QSize(32, 24);
        });
        connect(&assets, &ImageAssetManager::sigLocalAssetFailed, this, [&](QString, QString, QString) { has_failed = true; });
        connect(&assets, &ImageAssetManager::sigAssetFailed, this, [&](QString, QString) { has_failed = true; });
        assets.PrepareImageDataAsync(QImage::fromData(ImageBytes(qRgb(160, 30, 30))), "clipboard-request");
        QVERIFY(WaitUntil([&] { return is_import_ready || has_failed; }));
        QVERIFY(!has_failed);
        QVERIFY(!imported_info._asset_handle.isEmpty());
        assets.ReleaseAsset(imported_info._asset_handle);
        assets.LoadAssetAsync("room-asset", imported_info.asset_sha256, imported_info.mime_type);
        QVERIFY(WaitUntil([&] { return is_cache_ready || has_failed; }));
        QVERIFY(!has_failed);
        assets.CancelAll();
        // 本地文件入口同样经过代理，UI 不打开缓存文件。
        const QString source_path = QDir(directory.path()).filePath("source.png"); // 测试原始图片。
        QFile source_file(source_path); // 只在测试中创建用户选择的文件。
        QVERIFY(source_file.open(QIODevice::WriteOnly));
        source_file.write(ImageBytes(qRgb(5, 10, 20)));
        source_file.close();
        bool is_file_ready = false; // 路径关联检查。
        connect(&assets, &ImageAssetManager::sigLocalAssetReady, this, [&](QString id, QString path, ImageAssetInfo info, QPixmap pixmap) {
            if (id == "file-request")
            {
                is_file_ready = path == source_path && !pixmap.isNull();
                assets.ReleaseAsset(info._asset_handle);
            }
        });
        assets.prepareLocalAssetAsync(source_path, "file-request");
        QVERIFY(WaitUntil([&] { return is_file_ready || has_failed; }));
        QVERIFY(is_file_ready);
        // 删除未广播图元必须中止真实 PUT，不能只释放句柄后继续传输。
        bool is_upload_import_ready = false; // 上传导入关联。
        bool has_upload_result = false; // 删除后不再向图元交付成功或失败。
        ImageAssetInfo upload_info; // 未释放的上传句柄。
        connect(&assets, &ImageAssetManager::sigLocalAssetReady, this, [&](QString id, QString, ImageAssetInfo info, QPixmap) {
            if (id == "upload-request")
            {
                upload_info = info;
                is_upload_import_ready = true;
            }
        });
        connect(&assets, &ImageAssetManager::sigAssetUploaded, this, [&](QString, QString, QString) { has_upload_result = true; });
        connect(&assets, &ImageAssetManager::sigAssetUploadFailed, this, [&](QString, QString) { has_upload_result = true; });
        assets.prepareLocalAssetAsync(source_path, "upload-request");
        QVERIFY(WaitUntil([&] { return is_upload_import_ready || has_failed; }));
        QVERIFY(is_upload_import_ready);
        TestOss cancel_oss; // 模拟尚未收到 OSS 上传完成回复。
        cancel_oss._delay_ms = 5000;
        assets.uploadAsset("temporary-image", upload_info._asset_handle, QUrl(cancel_oss.Url()),
            upload_info.mime_type, upload_info.asset_sha256, upload_info.byte_size);
        QVERIFY(WaitUntil([&] { return cancel_oss._put_count == 1; }));
        assets.CancelAssetUpload(upload_info._asset_handle);
        QVERIFY(WaitUntil([&] { return cancel_oss._server.findChildren<QTcpSocket*>().isEmpty(); }, 2000));
        QVERIFY(!has_upload_result);
    }
    const QString previous_instance = instance_id; // 重启前身份。
    Peer peer; // 用握手取得测试进程 PID。
    QVERIFY(peer.Connect());
    QVERIFY(peer.Hello());
    const auto hello = peer.Request("Hello", {{"rootDigest", AssetServiceProtocol::RootDigest()}}).value("payload").toObject(); // 身份描述。
    TestOss oss; // 延迟 HTTP 回复，使请求在 kill 时尚未完成。
    oss._delay_ms = 5000;
    oss._image_data = ImageBytes(qRgb(80, 15, 75));
    const QString sha256 = AssetServiceProtocol::ValidateImage(oss._image_data)._record._asset_sha256; // 下载摘要。
    const QString download_id = client->SendCommand("DownloadAsset", {{"sha256", sha256}, {"url", oss.Url()}}); // 未完成任务。
    QVERIFY(WaitUntil([&] { return oss._get_count == 1; }));
    KillService(hello.value("pid").toInteger());
    peer.Close();
    QVERIFY(WaitUntil([&] { return responses.contains(download_id); }));
    QCOMPARE(responses.take(download_id).value("errorCode").toString(), "ServiceDisconnected");
    QVERIFY(WaitUntil([&] { return !instance_id.isEmpty() && instance_id != previous_instance; }));
    const QString next_stats = client->SendCommand("GetCacheStats"); // 新连接仍可使用。
    QVERIFY(WaitUntil([&] { return responses.contains(next_stats); }));
    QCOMPARE(responses.take(next_stats).value("status").toString(), "ok");
    QCOMPARE(oss._get_count, 1);
    client->disconnect(this);
    client->Disconnect();
    client.reset();
    ServiceCommandClient::getInstance().reset();
    // 2. detached 服务没有 UI 父进程依赖，按自身空闲规则释放独占锁。
    QLockFile lock(QDir(directory.path()).filePath("asset-service.lock")); // 验证后台已完整退出。
    lock.setStaleLockTime(0);
    QVERIFY(WaitUntil([&] { return lock.tryLock(); }, 40000));
    lock.unlock();
}

int main(int argc, char** argv)
{
    // 1. 测试 executable 与客户端采用相同服务模式，避免依赖 LiveKit。
    bool is_service = false; // 应用构造前检测后台模式。
    for (int index = 1; index < argc; ++index)
    {
        is_service = is_service || QByteArray(argv[index]) == "--asset-service";
    }
    if (is_service)
    {
        QCoreApplication app(argc, argv); // 服务不需要 Widgets 初始化。
        const QStringList arguments = app.arguments(); // 测试后台参数。
        const int index = arguments.indexOf("--asset-root"); // 独立缓存路径。
        if (index >= 0 && index + 1 < arguments.size())
        {
            app.setProperty("assetServiceRoot", arguments[index + 1]);
        }
        ServiceCommandServer server; // 生产服务实现。
        const auto result = server.Start(); // 生产唯一性逻辑。
        return result == ServiceStartResult::Started ? app.exec() : (result == ServiceStartResult::AlreadyRunning ? 0 : 2);
    }
    // 2. 前台运行实际集成测试。
    QApplication app(argc, argv); // UI 门面需要创建真实 QPixmap。
    AssetServiceTests tests; // 测试对象。
    return QTest::qExec(&tests, argc, argv);
}

#include "assetservicetests.moc"
