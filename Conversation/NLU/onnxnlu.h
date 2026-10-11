#ifndef QSA_ONNX_NLU_H
#define QSA_ONNX_NLU_H

#include "nlumodel.h"
#include <QHash>
#include <QPair>
#include <QStringList>
#include <QVector>
#include <memory>

namespace Ort {
struct Env;
struct Session;
struct SessionOptions;
}

/**
 * @brief 用**进程内 onnxruntime** 直接跑自训练的中文 NLU 模型，彻底取代 rasa。
 *
 * 为什么要有这条后端（对比 rasa 后端）：
 *   rasa 走的是 Python + TensorFlow + PyTorch 三条运行时叠在一起（DIET 是 TF、
 *   语言模型 featurizer 是 torch），实测一个 `rasa run` 常驻 **RSS ≈ 1.98 GB /
 *   commit ≈ 3.03 GB**，而模型文件本身只有 236 MB。本后端把整条链路换成
 *   一个 57 MB（int8）的 ONNX 图，由项目**已有的 onnxruntime**（唤醒词在用）
 *   进程内推理：无常驻 Python、无 TF、无 torch、无 HTTP 服务、无端口。
 *   实测 P50 1~4 ms（rasa 42 ms）。
 *
 * 模型结构（tools/onnx_nlu/ 训练导出）：
 *   rbtl3(BertModel) + 意图头(16 类) + 逐字符/token 槽位头(9 类: O + n t s g f l m c d)
 *   等价于「rasa DIETClassifier」，只不过槽位用单标签而非 BIO。
 *
 * 归一化分工（与原 rasa 管线一一对应，规则表由训练集统计生成、随模型发布）：
 *    f l s g m c → canon_map（ASR 同音错字/同义词 → 规范值，即 EntitySynonymMapper）
 *    t           → trend_span / trend_keyword（方向判定，含「太小→调大」语义反转）
 *    d           → 去掉「个」与尾部 后/之后/以后，再由 TimeNormalizer 归一化成秒偏移
 *    n           → 跨度原文
 *
 * 配置（config.json）：
 *   "nlu":      { "backend": "onnx" }
 *   "onnx_nlu": {
 *     "model":  "onnx_nlu/nlu_int8.onnx",   // 相对 Data/
 *     "vocab":  "onnx_nlu/vocab.txt",
 *     "rules":  "onnx_nlu/rules.json",
 *     "schema": "nlu_schema.json",          // id → 意图名
 *     "conf":   0.5,                        // 低于此置信度不产出意图
 *     "threads": 2                          // onnxruntime 线程数（0=默认）
 *   }
 */
class ONNXNLU : public NLUModel
{
    Q_OBJECT
public:
    /** 显式路径版配置：让本后端可以脱离 Config 单例被测试工程/嵌入式调用。 */
    struct Settings {
        QString model;
        QString vocab;
        QString rules;
        QString schema;
        float conf = 0.5f;
        int maxLen = 64;
        int threads = 0;
    };

    explicit ONNXNLU(QObject *parent = nullptr);
    ~ONNXNLU() override;

    /** 按显式路径加载（测试/嵌入式用）。返回 false 时 isValid() 为 false。 */
    bool load(const Settings &s);

    ParsedIntent parseIntent(const QString &text) override;
    void stop() override;
    bool isStart() override { return start; }

    /** 分词结果：token id 与它在**原文**里的 [start,end) 字符区间。
     *  用 int64_t 而非 qint64：onnxruntime 的 CreateTensor<T> 按类型查表，
     *  Linux 上 int64_t=long、qint64=long long，混用会编译不过。 */
    struct Tokenized {
        QVector<int64_t> ids;
        QVector<QPair<int, int>> offsets;
    };

    /** 原始解码结果：entity2Slot 映射**之前**的那一层，
     *  槽位用 DSL 实体码（n/t/s/g/f/l/m/c/d）。供一致性测试与 Python 对拍。 */
    struct RawResult {
        int intentId = -1;
        QString intentName;
        float conf = 0.f;
        // ⚠ 命名注意：不能叫 `slots` —— 那是 Qt 的关键字宏，moc 会把它误判成
        //   「嵌套类里的元对象特性」并报 "Meta object features not supported
        //   for nested classes"。同理别叫 signals/emit。
        QList<QPair<QString, QString> > slotList;   // (实体码, 归一化后的值)
    };

    /** 只做分词 + 推理 + 解码，不做 entity2Slot 映射。 */
    RawResult parseRaw(const QString &text);

    /** 暴露出来供单元测试/一致性校验（与 Python BertTokenizerFast 逐条对齐）。 */
    Tokenized tokenize(const QString &text) const;

private:
    bool loadVocab(const QString &path);
    bool loadRules(const QString &path);
    bool loadSchema(const QString &path);
    Intent entity2Slot(const Intent &entityIntent) const;

    QStringList vocab;
    QHash<QString, int> vocabIndex;
    QHash<QString, QString> canonMap;      // 表面写法 -> 规范值
    QHash<QString, QString> trendSpan;     // 整跨度 -> 大/小
    QHash<QString, QString> trendKeyword;  // 关键词 -> 大/小
    QHash<int, QString> intentNames;       // id -> 意图名
    QStringList entityCodes;               // 标签 id-1 -> 实体码

    std::unique_ptr<Ort::Env> env;
    std::unique_ptr<Ort::SessionOptions> sessionOptions;
    std::unique_ptr<Ort::Session> session;
    int maxLen = 64;
    float conf = 0.5f;
};

#endif // QSA_ONNX_NLU_H
