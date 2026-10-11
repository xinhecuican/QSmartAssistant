#ifndef QSA_LLM_NLU_H
#define QSA_LLM_NLU_H

#include "nlumodel.h"
#include "../Utils/LLM/LLMBackend.h"
#include "../Utils/LLM/OpenAICompatibleBackend.h"
#include <QJsonObject>
#include <QMap>
#include <QNetworkProxy>
#include <QStringList>

/**
 * @brief 用小语言模型（默认 qwen3.5:2b，本地 ollama）替代 rasa 做 NLU。
 *
 * 设计目标是「输入输出都尽可能短」——语音链路对延迟极敏感，
 * 每多一个 token 都是端到端延迟。因此：
 *
 *   输入：user 消息就是 ASR 结果原文，不做任何复述。
 *        schema 通过 system prompt 的**前缀缓存**（ollama 自动复用），
 *        相同前缀不会重复 prefill，实际只 prefill 用户那句话。
 *
 *   输出：单行紧凑 DSL，而不是 JSON。
 *        例：`3|song=孤勇者,singer=陈慧珊`
 *        一次生成通常 5~20 token，而 JSON 方案要 40+。
 *
 *   意图：用**数字 id**代替意图名。`3|MUSICINFO` 变成 `3`，
 *        既省输出 token，也避免小模型拼错英文意图名。
 *
 * schema（意图表 / 实体表）与 rasa 的 domain.yml + nlu.yml 保持一一对应，
 * 由 tools/nlu_llm_finetune/ 下的流水线生成，两端共用同一份
 * nlu_schema.json，避免训练与推理不一致。
 *
 * 配置（config.json 的 "llm_nlu" 节）：
 *   "llm_nlu": {
 *     "enabled": true,
 *     "llm": "qwen-nlu",            // 对应 llm.backends 里的名字
 *     "conf": 0.6,                  // 低于此置信度按 FALLBACK 处理
 *     "timeout_ms": 1500,
 *     "temperature": 0,
 *     "max_tokens": 48,
 *     "schema": "nlu_schema.json"   // 相对 Data/；缺省用内置紧凑 schema
 *   }
 */
class LLMNLU : public NLUModel {
    Q_OBJECT
public:
    explicit LLMNLU(QObject *parent = nullptr);
    ~LLMNLU() override;

    ParsedIntent parseIntent(const QString &text) override;
    void stop() override;
    bool isStart() override { return started; }

    /**
     * @brief 加载意图/实体 schema。返回 false 表示用内置兜底 schema。
     */
    bool loadSchema();

    /**
     * @brief 生成 system prompt。schema 加载后才有意义。
     */
    QString systemPrompt() const;

    /**
     * @brief 解析模型输出的一行 DSL。public 便于单测与离线排障。
     *
     *        格式：`intentId|entity=value,entity=value`
     *        容错：允许前后空白/反引号、允许未知实体被丢弃、
     *        允许模型直接回意图名而非编号、无 | 分隔时按纯意图处理。
     */
    ParsedIntent parseOutput(const QString &output,
                             const QString &text = QString()) const;

    /**
     * @brief 注入 schema（public 便于单测，不依赖运行期 Data 目录）。
     * @return schema 里至少有一个意图时返回 true
     */
    bool loadSchemaFromJson(const QJsonObject &root);

private:
    /**
     * @brief 从 schema 生成 system prompt。
     */
    void buildPrompt(const QJsonObject &schemaRoot);

    /**
     * @brief 把 rasa 风格实体映射为插件使用的槽位
     *        （与 RasaNLU::entity2Slot 保持一致）。
     */
    Intent entity2Slot(const Intent &entityIntent) const;

    /**
     * @brief 从 schema 生成自检用信息：意图数/实体数，便于启动日志排障。
     */
    QString schemaSummary() const;

private:
    LLMBackend *backend = nullptr;
    QString schemaPath;
    QString systemPromptText;
    // 意图 id(字符串) -> 意图名
    QMap<QString, QString> idToIntent;
    QMap<QString, QString> idToEntity; // 实体 id -> 实体名
    // 某意图允许使用的实体（空表示不限）
    QMap<QString, QStringList> intentEntities;
    float conf = 0.6f;
    int timeoutMs = 1500;
    int maxTokens = 48;
    double temperature = 0;
    bool started = false;
};

#endif // QSA_LLM_NLU_H