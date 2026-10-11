#include "server.h"
#include "../Utils/config.h"
#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMimeDatabase>
#include <QMimeType>
#include <QtConcurrent/QtConcurrent>

namespace {
// 静态资源挂在 Qt 资源系统里的前缀（qt_add_resources PREFIX "/" + Web/ 目录）
constexpr auto kWebPrefix = ":/Web";
// 会话 id 用 int 传递（Conversation 的 id 是 int），留出安全余量后回绕
constexpr uint32_t kMaxConversationId = 0x7FFFFFFF;
// 最多同时记录的会话数，超出后淘汰最旧的一条，避免长跑下无限增长
constexpr int kMaxTrackedConversations = 4096;
} // namespace

Server::Server(QObject *parent, int portOverride) : QObject(parent) {
    // 端口 0 交给系统分配空闲端口（单测用），-1 才去读配置
    const int listenPort =
        portOverride >= 0
            ? portOverride
            : Config::instance()->getConfig("server").value("port").toInt(20214);
    tcpServer = new QTcpServer(this);
    if (!tcpServer->listen(QHostAddress::Any, static_cast<quint16>(listenPort))) {
        qWarning() << "tcp server create fail, port" << listenPort
                   << tcpServer->errorString();
        tcpServer->deleteLater();
        tcpServer = nullptr;
        return;
    }
    if (!server.bind(tcpServer)) {
        qWarning() << "bind http server fail, port" << listenPort;
        tcpServer->deleteLater();
        tcpServer = nullptr;
        return;
    }

    corsHeaders.append("Access-Control-Allow-Origin", "*");
    corsHeaders.append("Access-Control-Allow-Methods", "POST,GET,PUT,DELETE");
    corsHeaders.append("Access-Control-Max-Age", "3600");
    corsHeaders.append("Access-Control-Allow-Headers", "*");
    corsHeaders.append("Access-Control-Allow-Credentials", "true");
    server.addAfterRequestHandler(
        &server, [this](const QHttpServerRequest &, QHttpServerResponse &response) {
            response.setHeaders(corsHeaders);
        });

    QFile htmlFile(QString("%1/index.html").arg(kWebPrefix));
    if (htmlFile.open(QIODevice::ReadOnly)) {
        htmlData = htmlFile.readAll();
        htmlFile.close();
    } else {
        qWarning() << "web index.html missing in resources";
    }

    setupWebSocket();
    setupRoutes();
}

void Server::setQuestionHandler(QuestionHandler handler) {
    questionHandler = std::move(handler);
}

void Server::setAnswerHandler(AnswerHandler handler) {
    answerHandler = std::move(handler);
}

int Server::port() const {
    return tcpServer != nullptr ? static_cast<int>(tcpServer->serverPort()) : 0;
}

bool Server::isListening() const {
    return tcpServer != nullptr && tcpServer->isListening();
}

void Server::pushResponse(const QString &text, int id, const QString &source) {
    if (id <= 0) {
        return; // 0 表示本地麦克风对话，与网页无关
    }
    // 回复一到，这一轮的反问就结束了（可能是被语音答掉的，也可能是超时之后
    // 插件自己往下走了）。此时再蹦出来一个迟到的网页作答只会污染下一轮，
    // 所以顺手把「可作答」状态收掉。
    awaitingAnswer.remove(static_cast<uint32_t>(id));
    QJsonObject payload{
        {"op", QStringLiteral("response")},
        {"id", static_cast<double>(id)},
        {"text", text},
        {"source", source},
    };
    pushToConversation(static_cast<uint32_t>(id), payload);
}

void Server::pushQuestion(const QString &text, int id, const QString &source) {
    if (id <= 0 || text.isEmpty()) {
        return;
    }
    const uint32_t conversationId = static_cast<uint32_t>(id);
    if (!socketByConv.contains(conversationId)) {
        // 会话已经结束了（网页关了 / 被淘汰）：问题无处可去。
        // 对话逻辑那边还有语音通道，所以这里只是不推，不算错误。
        qInfo() << "pushQuestion: 会话不存在" << id;
        return;
    }
    awaitingAnswer.insert(conversationId);
    pushToConversation(conversationId,
                       QJsonObject{
                           {"op", QStringLiteral("ask")},
                           {"id", static_cast<double>(id)},
                           {"text", text},
                           {"source", source},
                       });
}

Server::~Server() {
    // 迭代副本：close() 会同步触发 disconnected -> onClosed，进而修改 sockets
    const QSet<QWebSocket *> alive = sockets;
    for (QWebSocket *socket : alive) {
        socket->close(QWebSocketProtocol::CloseCodeNormal, QString());
    }
}

void Server::setupWebSocket() {
    server.addWebSocketUpgradeVerifier(
        &server, [](const QHttpServerRequest &) {
            return QHttpServerWebSocketUpgradeResponse::accept();
        });

    connect(&server, &QHttpServer::newWebSocketConnection, this, [this]() {
        while (server.hasPendingWebSocketConnections()) {
            std::unique_ptr<QWebSocket> pending =
                server.nextPendingWebSocketConnection();
            QWebSocket *socket = pending.release();
            if (socket == nullptr) {
                continue;
            }
            socket->setParent(this);
            sockets.insert(socket);
            connect(socket, &QWebSocket::textMessageReceived, this,
                    [this, socket](const QString &message) {
                        onTextMessage(socket, message);
                    });
            connect(socket, &QWebSocket::disconnected, this,
                    [this, socket]() { onClosed(socket); });
            connect(socket, &QWebSocket::errorOccurred, this,
                    [this, socket](QAbstractSocket::SocketError error) {
                        qInfo() << "web socket error" << error;
                        onClosed(socket);
                    });
        }
    });
}

void Server::setupRoutes() {
    server.route("/", [this]() {
        return QHttpServerResponse(QByteArray("text/html"), htmlData);
    });

    // 保留的同步接口：只对「立即说话」的插件（天气、系统信息等）有效，
    // 走 LLM 的插件是异步的，这里拿不到结果 —— 网页端请用 WebSocket。
    server.route("/intent", QHttpServerRequest::Method::Get,
                 [this](const QHttpServerRequest &request) {
                     if (!request.query().hasQueryItem("text")) {
                         return QtConcurrent::run([]() {
                             return QHttpServerResponse(
                                 QHttpServerResponder::StatusCode::BadRequest);
                         });
                     }
                     const QString text =
                         request.query().queryItemValue("text");
                     const uint32_t conversationId = allocateId();
                     Server *self = this;
                     return QtConcurrent::run([self, text, conversationId]() {
                         QList<QString> texts;
                         // 对话逻辑（NLU / 插件 / 播放器）活在主线程，必须切回去
                         // 执行，否则非线程安全的 NLU 会被并发调用
                         QMetaObject::invokeMethod(
                             self,
                             [&texts, self, text, conversationId]() {
                                 if (self->questionHandler) {
                                     // HTTP 是一次性的：没有回话通道，明确告诉
                                     // 对话逻辑「别等反问的回答」。
                                     texts = self->questionHandler(
                                         text,
                                         static_cast<int>(conversationId), false);
                                 }
                             },
                             Qt::BlockingQueuedConnection);
                         QJsonArray array;
                         for (const QString &data : texts) {
                             array.append(data);
                         }
                         return QHttpServerResponse(array);
                     });
                 });

    // 静态资源：index.html 里引用的 css / js / 图片都从这里出去
    server.setMissingHandler(
        &server,
        [](const QHttpServerRequest &request, QHttpServerResponder &responder) {
            QString path = request.url().path();
            while (path.contains(QStringLiteral(".."))) {
                path.remove(QStringLiteral(".."));
            }
            if (path.startsWith('/')) {
                path.remove(0, 1);
            }
            const QString resource =
                QString("%1/%2").arg(kWebPrefix, path);
            QFile file(resource);
            if (path.isEmpty() || !file.open(QIODevice::ReadOnly)) {
                responder.write(QHttpServerResponse::StatusCode::NotFound);
                return;
            }
            const QByteArray data = file.readAll();
            file.close();
            QMimeDatabase mimeDatabase;
            const QMimeType mimeType =
                mimeDatabase.mimeTypeForFileNameAndData(resource, data);
            responder.write(data, mimeType.name().toUtf8());
        });
}

void Server::onTextMessage(QWebSocket *socket, const QString &message) {
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        qInfo() << "socket parse error" << error.errorString();
        return;
    }
    const QJsonObject root = doc.object();
    const QString op = root.value("op").toString();

    if (op == QStringLiteral("ping")) {
        sendJson(socket, QJsonObject{{"op", QStringLiteral("pong")},
                                     {"timestamp", root.value("timestamp")}});
        return;
    }

    if (op == QStringLiteral("answer")) {
        // 回答某一轮的 {"op":"ask"}。三重校验，任何一条不满足都直接丢掉：
        //   1. id 确实是被反问过的会话（awaitingAnswer）；
        //   2. 该会话还归这个连接所有（不能替别人作答）；
        //   3. 有接收方（AnswerHandler 已接线）。
        // ⚠ 归属校验必须在「消费」之前：否则随便一个客户端就能拿一个数字把
        //   别人正在等的那一轮反问作废。
        const uint32_t conversationId =
            static_cast<uint32_t>(root.value("id").toInt());
        if (!awaitingAnswer.contains(conversationId)) {
            qInfo() << "socket answer without ask" << conversationId;
            return;
        }
        if (socketByConv.value(conversationId, nullptr) != socket) {
            qInfo() << "socket answer from wrong client" << conversationId;
            return;
        }
        awaitingAnswer.remove(conversationId); // 一次性
        if (answerHandler) {
            answerHandler(static_cast<int>(conversationId),
                          root.value("text").toString());
        }
        return;
    }

    if (op != QStringLiteral("conversation")) {
        qInfo() << "socket unknown op" << op;
        return;
    }

    const QString question = root.value("question").toString().trimmed();
    if (question.isEmpty()) {
        return;
    }
    // 每次提问分配一个新的会话 id：插件异步产生的回复会带着它回来，
    // 网页据此把回复对应到具体某一轮提问
    const uint32_t conversationId = allocateId();
    if (socketByConv.size() >= kMaxTrackedConversations) {
        awaitingAnswer.remove(socketByConv.begin().key());
        socketByConv.erase(socketByConv.begin()); // id 递增，第一个就是最旧的
    }
    socketByConv.insert(conversationId, socket);
    sendJson(socket, QJsonObject{{"op", QStringLiteral("accepted")},
                                 {"id", static_cast<double>(conversationId)}});
    if (questionHandler) {
        // ⚠⚠ 必须排到事件队列里再调，绝不能在这个栈上直接调。
        //
        // 这段代码跑在「socket 可读」的通知回调里（Qt 在派发 socket 通知时
        // 调过来）。对话逻辑是**可能长时间阻塞**的：插件中途反问时
        // Conversation::question() 会跑嵌套事件循环，一直等到用户作答
        // （见 Conversation::askExternal）。而从这个栈上进入嵌套循环之后，
        // 后续到达的 WebSocket 帧就不再被派发了 —— 实测（.buildtmp/
        // ws_timeline.py）：反问期间网页发来的 ping 与 answer 全部积压，
        // 直到这一次提问结束才一次性涌出，网页因此**根本没法作答**，ping
        // 保活也失效。主线程心跳正常、定时器照常触发，说明卡的是 socket
        // 通知这一路，而不是整个事件循环。
        //
        // 排到队列里，让这次 socket 回调先干净地返回；等到真正处理时已经在
        // 普通事件派发中，嵌套循环就能照常收报文。
        pendingConversations.append({question, conversationId});
        startNextConversation();
    }
}

void Server::startNextConversation() {
    // 串行：对话逻辑是同步的，而且可能长时间阻塞（反问要等用户作答）。
    // 真让它并发，第二条提问会在第一条的嵌套事件循环里被处理 —— 那时两条
    // 链路会共用同一份「正在等回答」的状态，互相把对方的 eventLoop 解掉。
    if (handlingConversation || pendingConversations.isEmpty() ||
        !questionHandler) {
        return;
    }
    handlingConversation = true;
    const QPair<QString, uint32_t> job = pendingConversations.takeFirst();
    QMetaObject::invokeMethod(
        this,
        [this, job]() {
            if (questionHandler) {
                questionHandler(job.first, static_cast<int>(job.second), true);
            }
            handlingConversation = false;
            startNextConversation(); // 轮到下一条排队的提问
        },
        Qt::QueuedConnection);
}

void Server::onClosed(QWebSocket *socket) {
    if (!sockets.remove(socket)) {
        return; // 已经处理过（disconnected 与 errorOccurred 可能都触发）
    }
    for (auto it = socketByConv.begin(); it != socketByConv.end();) {
        if (it.value() == socket) {
            awaitingAnswer.remove(it.key());
            it = socketByConv.erase(it);
        } else {
            ++it;
        }
    }
    socket->deleteLater();
}

void Server::sendJson(QWebSocket *socket, const QJsonObject &payload) {
    if (socket == nullptr) {
        return;
    }
    socket->sendTextMessage(
        QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)));
}

void Server::pushToConversation(uint32_t conversationId,
                                const QJsonObject &payload) {
    sendJson(socketByConv.value(conversationId, nullptr), payload);
}

uint32_t Server::allocateId() {
    const uint32_t id = nextId;
    nextId = (nextId >= kMaxConversationId) ? 1 : nextId + 1;
    return id;
}
