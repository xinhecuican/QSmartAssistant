#ifndef __FUNASR_H__
#define __FUNASR_H__
#include "ASRModel.h"
#include "kaldi-native-fbank/csrc/feature-fbank.h"
#include "kaldi-native-fbank/csrc/online-feature.h"
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QVector>
#include <memory>
#include <string>
#include <vector>
#include "onnxruntime_cxx_api.h"

/**
 * @brief FunASR / Paraformer 离线识别（onnxruntime 进程内推理，无 Python）
 *
 * ── 对接哪类模型 ───────────────────────────────────────────────────────────
 * 本实现对接 FunASR 官方 **ONNX 导出包**（ModelScope 上名字以 `-onnx` 结尾的
 * 仓库），例如：
 *
 *     iic/speech_paraformer-large_asr_nat-zh-cn-16k-common-vocab8404-onnx
 *
 * 该包只含 `model_quant.onnx`（就是通常说的 **Paraformer-q8**，int8 量化）+ 
 * `am.mvn` + `tokens.json` + `config.yaml`。**前端要自己做**（模型只吃算好的
 * 特征）：kaldi 80 维 mel-fbank → LFR(m=7,n=6) → CMVN，接口为
 *
 *     inputs : speech          float32 [batch, feats_length, 560]   (80*7)
 *              speech_lengths  int32   [batch]                      (= LFR 后的帧数)
 *     outputs: logits          float32 [batch, logits_length, 8404]
 *              token_num       int32   [batch]                      (= 真实 token 数)
 *
 * ⚠ 与改写前的两个关键差异（旧代码是从 FunASR runtime 抄来的 paraformer-seaco
 *   版本，直接拿来跑官方导出包会出错）：
 *   1. 第二个输出**官方导出是 int32**（`token_num`；seaco 版才是 int64），旧代码
 *      固定按 `int64_t*` 读 → n_len 变成「int32 值 + 紧邻内存」的天文数字 →
 *      按它循环等于越界读 logits。现在按张量真实 dtype 分派，并对 n_len 夹取。
 *   2. 旧代码写死 `exit(-1)`（少一个文件就直接杀进程），在插件 / 被守护的进程
 *      里不可接受 → 改为 `valid = false` + qCritical，`detect()` 空转返回。
 *
 * ── 配置（运行期 config 的 `funasr` 段）────────────────────────────────────
 *   model   主模型 onnx（必填，如 paraformer-q8/model_quant.onnx）
 *   cmvn    am.mvn（必填；LFR 之后的逐维 CMVN 参数，560 维）
 *   token   tokens.json（必填；JSON 字符串数组，下标即 token id）
 *   threads 推理线程数（可选，默认 2，与 sherpa 后端保持一致）
 *   language 解码语系（可选，默认 "zh-cn"；只有 "en-bpe" 会走 BPE 合词）
 *
 *   以下前端 / 网络超参可选，缺省即官方 paraformer-large 导出的取值（因此
 *   普通用法不需要写）：sample_rate / window / frame_length / frame_shift /
 *   n_mels / lfr_m / lfr_n / encoder_size
 *
 *   —— 下面三项**只有热词版（paraformer-seaco）**才需要；官方普通导出包没有
 *      第三个输入，配了也不会生效（会自动退化为普通模式）：
 *   hw_model 热词编码模型   segdict 分词词典   hotword 热词表
 *   ⚠ 热词分支本次**未做端到端验证**（本机没有 seaco 模型），按原逻辑保守保留，
 *     且任何一项缺失/加载失败都只会降级为普通模式，不影响主链路。
 */

/**
 * @brief tokens.json —— FunASR 导出包里的词表
 *
 * 文件就是一个 JSON 字符串数组，下标即 token id（首三个固定是 `<blank>` /
 * `<s>` / `</s>`）。词表（解码用）与音素表（热词编码用）用的是**同一个文件、
 * 同一种加载方式**，所以旧代码里的 `Vocab` / `PhoneSet` 两份重复实现合并成这一个。
 */
class FunasrTokens {
public:
    explicit FunasrTokens(const QString &filename);
    ~FunasrTokens();

    /// 是否加载成功且非空
    bool isValid() const { return !tokens_.isEmpty(); }
    int size() const { return tokens_.size(); }

    /// id → 字符串；**越界返回空串**（旧代码直接 `vocab[id]`，模型与词表不匹配时越界崩）
    QString id2str(int id) const;

    /// 字符串 → id；找不到返回 -1
    int str2id(const QString &token) const;

    /// id 序列 → 原始 token 串序列
    void toTokenStrings(const QVector<int> &ids, QVector<QString> &out) const;

    /// id 序列 → 最终文本（含中英混排 / `@@` 子词拼接，口径与 FunASR 官方一致）
    QString detokenize(const QVector<int> &ids,
                       const QString &language = QString()) const;

private:
    static QString wordFormat(const QString &word);
    void loadFromJson(const QString &filename);

    QVector<QString> tokens_;
    QMap<QString, int> str2id_;
};

/**
 * @brief seg_dict —— 热词分词词典（词 → 子词序列）
 * @note 只有热词模型（paraformer-seaco）用得到，普通 Paraformer 不会构造它。
 */
class FunasrSegDict {
public:
    explicit FunasrSegDict(const QString &filename);
    ~FunasrSegDict();

    bool isValid() const { return !seg_dict_.isEmpty(); }
    QVector<QString> tokensOfWord(const QString &word) const;

private:
    QMap<QString, QVector<QString>> seg_dict_;
};

/**
 * @brief Paraformer 离线识别模型（ASRModel 实现）
 *
 * 离线（非流式）：`isStream()` 返回 false，调用方（Conversation）会把整段音频
 * 缓存好，最后一次性 `detect(cache, true, id)` 喂进来。
 */
class Funasr : public ASRModel {
    Q_OBJECT
public:
    explicit Funasr(QObject *parent = nullptr);
    ~Funasr() override;

    bool isStream() override;
    void detect(const QByteArray &data, bool isLast = false, int id = 0) override;

    /// 释放 onnxruntime 会话（幂等，可重复调用）
    void stop() override;
    /// 丢弃当前累积的文本
    void clear() override;

private:
    // ---- 初始化 ----
    /// 读配置、建会话，成功返回 true；失败置 valid=false
    bool initMain();
    /// 可选热词分支：三项都齐且可用才真正打开，否则降级为普通模式
    void initHotword(const QJsonObject &config);
    bool loadCmvn(const QString &filename);
    /// 按配置覆盖前端 / 网络超参（键不存在就用官方缺省值）
    void loadParams(const QJsonObject &config);

    // ---- 前端 ----
    /// kaldi 80 维 mel-fbank（dither=0，与 FunASR 官方 ONNX runtime 一致）
    void computeFbank(const float *waves, int len,
                      std::vector<std::vector<float>> &feats) const;
    /// LFR(m/n) 拼帧 + CMVN；口径与 FunASR `apply_lfr` / `apply_cmvn` 逐位对应
    void applyLfrCmvn(std::vector<std::vector<float>> &feats) const;

    // ---- 推理 ----
    void forward(const float *din, int len, bool inputFinished);
    std::string greedySearch(float *in, int nLen, int64_t tokenNums, bool isStamp,
                             const std::vector<float> &usAlphas = {},
                             const std::vector<float> &usCifPeak = {});
    std::vector<std::vector<float>> compileHotwordEmbedding(const QString &hotwords) const;

private:
    // ⚠ 成员声明顺序 = 构造顺序，析构顺序相反：env 必须先于 session 构造、
    //   后于 session 析构，所以 env 放最前面。
    Ort::Env env_;
    Ort::SessionOptions session_options_;
    std::unique_ptr<Ort::Session> session_;   // 官方导出包：speech + speech_lengths
    std::vector<std::string> input_names_str_;
    std::vector<const char *> input_names_;
    std::vector<std::string> output_names_str_;
    std::vector<const char *> output_names_;
    /// speech_lengths 的真实 dtype（官方导出 int32，部分转换脚本给 int64）
    ONNXTensorElementDataType length_input_type_ = ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32;

    // ---- 热词（paraformer-seaco）可选分支，未启用时全是空的 ----
    bool use_hotword_ = false;
    Ort::Env hw_env_;
    Ort::SessionOptions hw_session_options_;
    std::unique_ptr<Ort::Session> hw_session_;
    std::vector<std::string> hw_input_names_str_;
    std::vector<const char *> hw_input_names_;
    std::vector<std::string> hw_output_names_str_;
    std::vector<const char *> hw_output_names_;
    std::vector<std::vector<float>> hw_emb_;

    std::unique_ptr<FunasrTokens> vocab_;
    std::unique_ptr<FunasrSegDict> seg_dict_;

    std::vector<float> means_list_;   // am.mvn <AddShift>，560 维
    std::vector<float> vars_list_;    // am.mvn <Rescale>，560 维

    knf::FbankOptions fbank_opts_;
    std::vector<float> samples_;
    QString result_;
    QString language_ = QStringLiteral("zh-cn");

    // ---- 前端 / 网络超参（缺省 = 官方 paraformer-large 导出值）----
    int lfr_m_ = 7;
    int lfr_n_ = 6;
    std::string window_type_ = "hamming";
    int frame_length_ = 25;      // ms
    int frame_shift_ = 10;       // ms
    int n_mels_ = 80;
    int encoder_size_ = 512;     // 热词 embedding 维度 = encoder 输出维度
    int asr_sample_rate_ = 16000;
    int threads_ = 2;
    const float scale_ = 1.0f;
};

#endif
