#ifndef QSA_LLM_BACKEND_H
#define QSA_LLM_BACKEND_H

#include "LLMTypes.h"
#include "../LPcommonGlobal.h"
#include <QNetworkProxy>
#include <QObject>
#include <QString>

/**
 * @brief LLM 后端抽象。
 *
 * 一次 chat 是「异步 + 回调」的：既支持流式增量（供 TTS 边生成边说），
 * 也支持整体返回（供 NLU 这类只要结论的场景）。
 * 相比旧实现（query 里嵌一个阻塞的 QEventLoop），本接口不阻塞调用方，
 * 因此可以安全地用于已在事件循环里的代码。
 */
class LPCOMMON_EXPORT LLMBackend : public QObject {
    Q_OBJECT
public:
    using ChunkCallback = std::function<void(const LLMChatChunk &)>;
    using DoneCallback = std::function<void(const QString &error)>;

    explicit LLMBackend(QObject *parent = nullptr) : QObject(parent) {}
    ~LLMBackend() override = default;

    QString name() const { return backendName; }
    int maxTokens() const { return defaultMaxTokens; }
    bool supportsTools() const { return toolCapable; }

    /**
     * @brief 发起一次对话
     * @param request 请求；request.requestId 用于后续 abort
     * @param onChunk 每个增量回调（流式下多次，最后一次 last=true）
     * @param onDone 结束时回调一次，error 为空表示成功
     */
    virtual void chat(const LLMChatRequest &request, const ChunkCallback &onChunk,
                      const DoneCallback &onDone) = 0;

    /**
     * @brief 中止指定请求；requestId 为空则中止该后端全部在途请求
     */
    virtual void abort(const QString &requestId) = 0;

protected:
    QString backendName;
    int defaultMaxTokens = 1024;
    bool toolCapable = true;
};

#endif // QSA_LLM_BACKEND_H