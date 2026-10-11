#ifndef QSA_LLM_OPENAI_BACKEND_H
#define QSA_LLM_OPENAI_BACKEND_H

#include "LLMBackend.h"
#include <QHash>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

/**
 * @brief OpenAI 兼容 /chat/completions 后端。
 *
 * 覆盖 OpenAI、DeepSeek、智谱、以及本地 Ollama 的 /v1 端点，
 * 因此 NLU 与 Chat 可以共用同一份实现。
 *
 * 配置（config.json 的 "llm" 节）：
 *   "qwen-nlu": {
 *     "modelName": "qwen3.5:2b",
 *     "api_base": "http://127.0.0.1:11434/v1/chat/completions",
 *     "key": "",                      // Ollama 可留空，此时不发 Authorization
 *     "maxTokens": 256,
 *     "temperature": 0,
 *     "supports_tools": false
 *   }
 */
class LPCOMMON_EXPORT OpenAICompatibleBackend : public LLMBackend {
    Q_OBJECT
public:
    OpenAICompatibleBackend(const QString &name, const QJsonObject &config,
                            QNetworkProxy proxy, QObject *parent = nullptr);
    ~OpenAICompatibleBackend() override;

    void chat(const LLMChatRequest &request, const ChunkCallback &onChunk,
              const DoneCallback &onDone) override;
    void abort(const QString &requestId) override;

    /**
     * @brief 同步单次补全，供 NLU 这类必须立刻拿到结论的接口使用。
     *        内部跑局部事件循环并自带超时；返回空串表示失败。
     */
    QString complete(const QString &prompt, const QString &systemPrompt,
                     double temperature, int maxTokens, int timeoutMs,
                     const QString &requestId = QString());

private:
    /**
     * @brief 一次在途请求的全部状态。挂在 reply 上，随 reply 一起销毁。
     */
    struct StreamState {
        QString requestId;
        QByteArray buffer;  // SSE 跨 readyRead 累积
        QString accumulated;
        QMap<qint64, LLMToolCall> partialCalls; // 按 index 合并流式分片
        ChunkCallback onChunk;
        DoneCallback onDone;
        bool finished = false;
    };

    void handleStreamData(QNetworkReply *reply, StreamState *state);
    void finishStream(QNetworkReply *reply, StreamState *state, const QString &error);
    QJsonObject buildBody(const LLMChatRequest &request) const;
    static QJsonObject parseNonStream(const QByteArray &data, LLMChatChunk *chunk,
                                      QString *error);

private:
    QNetworkAccessManager manager;
    QNetworkRequest request;
    QNetworkProxy proxy;
    QJsonObject baseConfig;
    QHash<QNetworkReply *, StreamState *> states; // 在途请求
};

#endif // QSA_LLM_OPENAI_BACKEND_H