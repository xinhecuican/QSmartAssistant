#ifndef __SERVER_H__
#define __SERVER_H__
#include <QHttpServer>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTcpServer>
#include <QWebSocket>
#include <functional>

/**
 * @brief 面向网页 / 外部程序的对话传输层。
 *
 * 这一层只负责「搬运」，不认识 Conversation / NLU / 插件：
 *   网页 --WebSocket--> Server --QuestionHandler--> 对话逻辑（Conversation）
 *   对话逻辑 --pushResponse()--> Server --WebSocket--> 网页
 * 依赖由外部注入（见 robot.cpp 的接线），因此可以脱离 ASR/NLU/模型单独测试。
 *
 * 回复可能由插件异步产生（LLM、天气接口等），所以 **不通过请求返回值返回**，
 * 而是统一走 pushResponse() 推送，保证同步插件与异步插件的表现一致。
 *
 * 一次提问可能反过来变成「反问」：插件处理到一半需要用户再给一个输入（网易云
 * 的短信验证码、确认类问题……），这条链路同样要能从网页走：
 *   对话逻辑 --pushQuestion()--> Server --WebSocket--> 网页（{"op":"ask"}）
 *   网页 --WebSocket--> Server --AnswerHandler--> 对话逻辑
 * 问题同时会经语音念出来，因此网页作答与语音作答**先到先得**（见
 * Conversation::question()）。只有真正受理过提问的会话 id 才接受作答。
 *
 * WebSocket 协议（文本帧，JSON）：
 *   客户端 -> 服务端：{"op":"ping","timestamp":<ms>}
 *   服务端 -> 客户端：{"op":"pong","timestamp":<ms>}
 *   客户端 -> 服务端：{"op":"conversation","question":"今天天气怎么样"}
 *   服务端 -> 客户端：{"op":"accepted","id":<会话 id>}                        受理确认
 *   服务端 -> 客户端：{"op":"response","id":<会话 id>,"text":"...","source":"..."}
 *                    source 为产生回复的插件名，空串表示主程序
 *   服务端 -> 客户端：{"op":"ask","id":<会话 id>,"text":"...","source":"..."}
 *                    反问：请用 {"op":"answer"} 回答这一轮的问题。
 *                    仅当该会话来自可交互的 WebSocket 连接时才会发出。
 *   客户端 -> 服务端：{"op":"answer","id":<会话 id>,"text":"..."}
 *                    回答上一轮 ask。**一次性**：同一条 ask 只接受第一次作答，
 *                    且必须是发起该会话的那个连接。
 *
 * HTTP 接口：
 *   GET /             单页聊天界面
 *   GET /intent?text= 同步返回本次对话已产生的文本（仅对同步插件有效，保留兼容）
 *                    HTTP 是一次性的，没有回话通道，因此不会产生 ask。
 */
class Server : public QObject {
    Q_OBJECT
public:
    /// 把一个问题交给对话逻辑；id 为本次会话 id，回复必须带着它回来。
    /// 返回值是「同步回复」（立即开口的插件），异步回复走 pushResponse()。
    /// @param canAnswer 该请求方能否回答对话逻辑中途的反问（见 pushQuestion）。
    ///                  WebSocket 连接为 true；HTTP /intent 这类一次性请求为
    ///                  false —— 传错会让对话逻辑一直等一个永远不来的回答。
    using QuestionHandler =
        std::function<QList<QString>(const QString &text, int id, bool canAnswer)>;

    /// 收到某个会话对 pushQuestion() 的反问所作的回答。id 即那次会话的 id。
    using AnswerHandler = std::function<void(int id, const QString &text)>;

    /**
     * @param portOverride <0 用配置 `server.port`；>=0 用指定端口（0 = 由系统分配空闲端口）
     */
    explicit Server(QObject *parent = nullptr, int portOverride = -1);
    ~Server() override;

    void setQuestionHandler(QuestionHandler handler);
    void setAnswerHandler(AnswerHandler handler);
    /// 实际监听的端口；未监听成功时为 0
    int port() const;
    bool isListening() const;

public slots:
    /**
     * @brief 对话逻辑产生了一条回复，推送给发起该会话的客户端
     * @param source 产生回复的插件名，空串表示主程序
     */
    void pushResponse(const QString &text, int id, const QString &source);
    /**
     * @brief 对话逻辑在会话中途反问，请客户端作答
     *
     * 只有发起该会话的连接还在，并且该会话 id 仍然有效时才会推送；推送之后
     * 这条会话才进入「可作答」状态，`{"op":"answer"}` 会被转给 AnswerHandler。
     * @param source 提出该问题的插件名，空串表示主程序
     */
    void pushQuestion(const QString &text, int id, const QString &source);

private:
    void setupWebSocket();
    void setupRoutes();
    void onTextMessage(QWebSocket *socket, const QString &message);
    void onClosed(QWebSocket *socket);
    void sendJson(QWebSocket *socket, const QJsonObject &payload);
    void pushToConversation(uint32_t conversationId, const QJsonObject &payload);
    uint32_t allocateId();
    /// 取出下一条排队的提问交给对话逻辑（同一时刻只允许一条在跑，见 .cpp）
    void startNextConversation();

private:
    QHttpServer server;
    QTcpServer *tcpServer = nullptr;
    QuestionHandler questionHandler;
    AnswerHandler answerHandler;
    uint32_t nextId = 1;
    QByteArray htmlData;
    QHttpHeaders corsHeaders;
    QSet<QWebSocket *> sockets;                 // 在线客户端
    // 会话 id -> 归属客户端。用 QMap（按 id 有序）方便超限时淘汰最旧的一条
    QMap<uint32_t, QWebSocket *> socketByConv;
    // 当前正处在「反问待答」状态的会话 id。只有这里的 id 才接受 answer，
    // 防止随便一个客户端拿个数字就把文本塞进对话逻辑。
    QSet<uint32_t> awaitingAnswer;
    // 对话逻辑是同步且可能长时间阻塞的（插件反问要等用户作答），**不能并发
    // 调用**，所以这里排队 + 串行：handlingConversation 表示有一条正在跑。
    bool handlingConversation = false;
    QList<QPair<QString, uint32_t>> pendingConversations;
};

#endif // __SERVER_H__
