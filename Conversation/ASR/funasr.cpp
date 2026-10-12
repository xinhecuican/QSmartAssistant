#include "funasr.h"
#include "../../Utils/config.h"
#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <limits>
#include <list>
#include <numeric>
#include <sstream>
#include <stdexcept>

using namespace std;

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// 小工具（原文件里都是全局符号，这里收进匿名 namespace，避免污染全局命名空间）
// ─────────────────────────────────────────────────────────────────────────────

void findMax(const float *din, int len, float &maxVal, int &maxIdx) {
    maxVal = -std::numeric_limits<float>::infinity();
    maxIdx = -1;
    for (int i = 0; i < len; i++) {
        if (din[i] > maxVal) {
            maxVal = din[i];
            maxIdx = i;
        }
    }
}

/// UTF-8 三字节字符 → unicode 码点（非三字节返回 0）
int utf8ToCodepoint(const string &ch) {
    if (ch.size() != 3) {
        return 0;
    }
    const char *c = ch.c_str();
    if (((c[0] & 0xf0) != 0xe0) || ((c[1] & 0xc0) != 0x80) ||
        ((c[2] & 0xc0) != 0x80)) {
        return 0;
    }
    return ((c[0] & 0x0f) << 12) | ((c[1] & 0x3f) << 6) | (c[2] & 0x3f);
}

bool isChinese(const string &ch) {
    const int unicode = utf8ToCodepoint(ch);
    return unicode >= 19968 && unicode <= 40959;
}

bool isChinese(const QChar &ch) {
    const int unicode = ch.unicode();
    return unicode >= 19968 && unicode <= 40959;
}

bool isChinese(const QString &ch) {
    if (ch.isEmpty()) {
        return false;
    }
    for (int i = 0; i < ch.size(); i++) {
        if (!isChinese(ch.at(i))) {
            return false;
        }
    }
    return true;
}

/// 抽出全部汉字字符
void keepChineseCharacterAndSplit(const QString &inputStr,
                                  QVector<QString> &chineseCharacters) {
    chineseCharacters.resize(0);
    for (int i = 0; i < inputStr.size(); i++) {
        if (isChinese(inputStr.at(i))) {
            chineseCharacters.push_back(inputStr.at(i));
        }
    }
}

/// ONNX 的输入 / 输出名（`GetXxxNameAllocated` 是新版 API，旧版裸指针已废弃）
void readNodeNames(Ort::Session *session, std::vector<std::string> &strNames,
                   std::vector<const char *> &names, bool input) {
    Ort::AllocatorWithDefaultOptions allocator;
    const size_t n = input ? session->GetInputCount() : session->GetOutputCount();
    strNames.resize(n);
    names.resize(n);
    for (size_t i = 0; i != n; ++i) {
        auto t = input ? session->GetInputNameAllocated(i, allocator)
                       : session->GetOutputNameAllocated(i, allocator);
        strNames[i] = t.get();
        names[i] = strNames[i].c_str();
    }
}

/// ONNX Runtime 的模型路径类型：Windows 上是 `wchar_t`，其它平台是 `char`
/// （`ORTCHAR_T`）。⚠ 旧代码直接传 `std::string::c_str()` —— 那在 Windows 上
/// **根本编不过**（说明 ASR_FUN 分支从来没在 Windows 上构建过）。
#ifdef _WIN32
using OrtPath = std::wstring;
inline OrtPath toOrtPath(const QString &p) { return p.toStdWString(); }
#else
using OrtPath = std::string;
inline OrtPath toOrtPath(const QString &p) { return p.toStdString(); }
#endif

/**
 * CIF peak → 每字时间戳（只在模型带 4 个输出、即热词 / 带时间戳导出时才会走到）。
 * 逻辑与 FunASR 官方 `TimestampOnnx` 一致；这里额外把 `num_frames` 夹到
 * `cif_peak.size()`，因为函数中段会把 cif_peak 重建成 `us_alphas.size()`，
 * 而 `num_frames` 还是旧值 —— 官方实现会越界读。
 */
void timestampOnnx(std::vector<float> &usAlphas, std::vector<float> usCifPeak,
                   std::vector<string> &charList, std::string &resStr,
                   std::vector<std::vector<float>> &timestampVec, float beginTime,
                   float totalOffset) {
    if (charList.empty()) {
        return;
    }

    const float START_END_THRESHOLD = 5.0;
    const float MAX_TOKEN_DURATION = 30.0;
    const float TIME_RATE = 10.0 * 6 / 1000 / 3;
    // cif_peak 是 3 倍上采样后拉平的 1 维数组
    std::vector<float> cifPeak = usCifPeak;
    int numFrames = static_cast<int>(cifPeak.size());
    if (charList.back() == "</s>") {
        charList.pop_back();
    }
    if (charList.empty()) {
        return;
    }
    vector<vector<float>> timestampList;
    vector<string> newCharList;
    vector<float> firePlace;
    // bicif 模型（大语料训练）的 cif2 其实在**字符开始**时触发，
    // 所以两个 peak 之间的帧算作前一个 token 的时长
    for (int i = 0; i < numFrames; i++) {
        if (cifPeak[i] > 1.0 - 1e-4) {
            firePlace.push_back(i + totalOffset);
        }
    }
    int numPeak = static_cast<int>(firePlace.size());
    if (numPeak != static_cast<int>(charList.size()) + 1) {
        float sum = std::accumulate(usAlphas.begin(), usAlphas.end(), 0.0f);
        float scale = sum / (static_cast<int>(charList.size()) + 1);
        if (scale == 0) {
            return;
        }
        cifPeak.clear();
        sum = 0.0;
        for (auto &alpha : usAlphas) {
            alpha = alpha / scale;
            sum += alpha;
            cifPeak.emplace_back(sum);
            if (sum >= 1.0 - 1e-4) {
                sum -= (1.0 - 1e-4);
            }
        }
        // 修正 sum > 1 的情况
        int cifIdx = static_cast<int>(cifPeak.size()) - 1;
        while (sum >= 1.0 - 1e-4 && cifIdx >= 0) {
            if (cifPeak[cifIdx] < 1.0 - 1e-4) {
                cifPeak[cifIdx] = sum;
                sum -= (1.0 - 1e-4);
            }
            cifIdx--;
        }

        numFrames = std::min(numFrames, static_cast<int>(cifPeak.size()));
        firePlace.clear();
        for (int i = 0; i < numFrames; i++) {
            if (cifPeak[i] > 1.0 - 1e-4) {
                firePlace.push_back(i + totalOffset);
            }
        }
    }

    numPeak = static_cast<int>(firePlace.size());
    if (firePlace.empty()) {
        return;
    }

    // 起始静音
    if (firePlace[0] > START_END_THRESHOLD) {
        newCharList.push_back("<sil>");
        timestampList.push_back({0.0, firePlace[0] * TIME_RATE});
    }

    // token 时间戳
    for (int i = 0; i < numPeak - 1; i++) {
        newCharList.push_back(charList[i]);
        if (i == numPeak - 2 || MAX_TOKEN_DURATION < 0 ||
            firePlace[i + 1] - firePlace[i] < MAX_TOKEN_DURATION) {
            timestampList.push_back({firePlace[i] * TIME_RATE,
                                     firePlace[i + 1] * TIME_RATE});
        } else {
            // 把超长 token 切成 token + 长静音
            float split = firePlace[i] + MAX_TOKEN_DURATION;
            timestampList.push_back({firePlace[i] * TIME_RATE, split * TIME_RATE});
            timestampList.push_back({split * TIME_RATE, firePlace[i + 1] * TIME_RATE});
            newCharList.push_back("<sil>");
        }
    }

    // 尾部 token 与结束静音
    if (timestampList.empty()) {
        qCritical() << "timestamp_list's size is 0!";
        return;
    }
    if (numFrames - firePlace.back() > START_END_THRESHOLD) {
        float end = (numFrames + firePlace.back()) / 2.0;
        timestampList.back()[1] = end * TIME_RATE;
        timestampList.push_back({end * TIME_RATE, numFrames * TIME_RATE});
        newCharList.push_back("<sil>");
    } else {
        timestampList.back()[1] = numFrames * TIME_RATE;
    }

    if (beginTime) {  // 带 vad 的模型需要加上起始偏移
        for (auto &timestamp : timestampList) {
            timestamp[0] += beginTime / 1000.0;
            timestamp[1] += beginTime / 1000.0;
        }
    }

    if (newCharList.size() != timestampList.size()) {
        qWarning() << "[funasr] 时间戳与字符数不一致，丢弃";
        return;
    }

    for (size_t i = 0; i < newCharList.size(); i++) {
        resStr += newCharList[i] + " " + to_string(timestampList[i][0]) + " " +
                  to_string(timestampList[i][1]) + ";";
    }
    for (size_t i = 0; i < newCharList.size(); i++) {
        if (newCharList[i] != "<sil>") {
            timestampVec.push_back(timestampList[i]);
        }
    }
}

/// 英文子词合并 / 空格规整（与 FunASR 官方 `PostProcess` 一致）
string postProcess(std::vector<string> &rawChar,
                   std::vector<std::vector<float>> &timestampList) {
    std::vector<std::vector<float>> timestampMerge;
    int i;
    list<string> words;
    int isPreEnglish = false;
    int preEnglishLen = 0;
    int isCombining = false;
    string combine = "";

    float begin = -1;
    for (i = 0; i < static_cast<int>(rawChar.size()); i++) {
        string word = rawChar[i];
        // step1 跳过特殊符号
        if (word == "<s>" || word == "</s>" || word == "<unk>" || word == "<blank>")
            continue;
        // step2 子词合成整词
        {
            int subWord = !(word.find("@@") == string::npos);
            if (subWord) {
                // badcase：lo@@ 后面直接接中文
                if (i == static_cast<int>(rawChar.size()) - 1 ||
                    (i < static_cast<int>(rawChar.size()) - 1 && isChinese(rawChar[i + 1]))) {
                    word = word.erase(word.length() - 2) + " ";
                    if (isCombining) {
                        combine += word;
                        isCombining = false;
                        word = combine;
                        combine = "";
                    }
                } else {
                    combine += word.erase(word.length() - 2);
                    if (!isCombining) {
                        begin = timestampList[i][0];
                    }
                    isCombining = true;
                    continue;
                }
            } else if (isCombining) {  // 词的结尾部分
                combine += word;
                isCombining = false;
                word = combine;
                combine = "";
            }
        }

        // step3 英文单词的空格处理
        {
            if (isChinese(word)) {
                words.push_back(word);
                timestampMerge.emplace_back(timestampList[i]);
                isPreEnglish = false;
            } else {
                if (!isPreEnglish) {
                    words.push_back(word);
                    begin = (begin == -1) ? timestampList[i][0] : begin;
                    std::vector<float> vec = {begin, timestampList[i][1]};
                    timestampMerge.emplace_back(vec);
                    begin = -1;
                    preEnglishLen = static_cast<int>(word.size());
                } else {
                    if (preEnglishLen > 1) {
                        words.push_back(" ");
                        words.push_back(word);
                        begin = (begin == -1) ? timestampList[i][0] : begin;
                        std::vector<float> vec = {begin, timestampList[i][1]};
                        timestampMerge.emplace_back(vec);
                        begin = -1;
                        preEnglishLen = static_cast<int>(word.size());
                    } else {
                        words.push_back(" ");
                        words.push_back(word);
                        begin = (begin == -1) ? timestampList[i][0] : begin;
                        std::vector<float> vec = {begin, timestampList[i][1]};
                        timestampMerge.emplace_back(vec);
                        begin = -1;
                        preEnglishLen = static_cast<int>(word.size());
                    }
                }
                isPreEnglish = true;
            }
        }
    }
    string stampStr = "";
    for (i = 0; i < static_cast<int>(timestampMerge.size()); i++) {
        stampStr += std::to_string(timestampMerge[i][0]);
        stampStr += ", ";
        stampStr += std::to_string(timestampMerge[i][1]);
        if (i != static_cast<int>(timestampMerge.size()) - 1) {
            stampStr += ",";
        }
    }

    stringstream ss;
    for (auto it = words.begin(); it != words.end(); it++) {
        ss << *it;
    }
    (void)stampStr;
    return ss.str();
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// FunasrTokens
// ─────────────────────────────────────────────────────────────────────────────

FunasrTokens::FunasrTokens(const QString &filename) { loadFromJson(filename); }

FunasrTokens::~FunasrTokens() {}

void FunasrTokens::loadFromJson(const QString &filename) {
    QFile file(filename);
    if (Q_UNLIKELY(!file.open(QIODevice::ReadOnly))) {
        qCritical() << "[funasr] 打不开词表:" << filename;
        return;
    }
    const QByteArray raw = file.readAll();
    file.close();

    const QJsonDocument doc = QJsonDocument::fromJson(raw);
    if (Q_UNLIKELY(!doc.isArray())) {
        qCritical() << "[funasr] 词表不是 JSON 数组:" << filename;
        return;
    }
    const QJsonArray data = doc.array();
    tokens_.reserve(data.size());
    int i = 0;
    for (const auto &ele : data) {
        const QString str = ele.toString();
        tokens_.push_back(str);
        if (!str2id_.contains(str)) {
            str2id_.insert(str, i);
        }
        i++;
    }
    qInfo() << "[funasr] 词表加载完成, token 数 =" << tokens_.size() << filename;
}

QString FunasrTokens::id2str(int id) const {
    // ⚠ 越界必须挡住：模型与词表不匹配（换个导出包忘了换 tokens.json）时，
    //   旧代码 `vocab[id]` 会直接越界读，表现为随机乱码甚至崩溃。
    if (id < 0 || id >= tokens_.size()) {
        return QString();
    }
    return tokens_.at(id);
}

int FunasrTokens::str2id(const QString &token) const {
    const auto it = str2id_.find(token);
    return it == str2id_.end() ? -1 : *it;
}

void FunasrTokens::toTokenStrings(const QVector<int> &ids,
                                  QVector<QString> &out) const {
    out.reserve(out.size() + ids.size());
    for (int i = 0; i < ids.size(); i++) {
        out.emplace_back(id2str(ids.at(i)));
    }
}

QString FunasrTokens::wordFormat(const QString &word) {
    if (word == "i") {
        return "I";
    } else if (word == "i'm") {
        return "I'm";
    } else if (word == "i've") {
        return "I've";
    } else if (word == "i'll") {
        return "I'll";
    }
    return word;
}

QString FunasrTokens::detokenize(const QVector<int> &ids,
                                 const QString &language) const {
    QList<QString> words;
    int isPreEnglish = false;
    int preEnglishLen = 0;
    int isCombining = false;
    QString combine = "";
    const QString unicodeChar = "▁";

    for (int i = 0; i < ids.size(); i++) {
        QString word = id2str(ids.at(i));
        // step1 跳过特殊 token。
        // ⚠ `<blank>` 是官方实现**漏掉**的一个：它是 id 0，一旦被 argmax 选中，
        //   官方 C++ runtime 会把字面量 "<blank>" 拼进最终文本；FunASR 的
        //   Python tokenizer 是把特殊 token 全部丢掉的，所以这里跟 Python 对齐。
        if (word == "<s>" || word == "</s>" || word == "<unk>" || word == "<blank>")
            continue;
        if (language == "en-bpe") {
            const qsizetype found = word.indexOf(unicodeChar);
            if (found != -1) {
                if (combine != "") {
                    combine = wordFormat(combine);
                    if (words.size() != 0) {
                        combine = " " + combine;
                    }
                    words.push_back(combine);
                }
                combine = word;
            } else {
                combine += word;
            }
            continue;
        }
        // step2 子词合成整词
        {
            const bool subWord = word.contains("@@");
            if (subWord) {
                if (i == ids.size() - 1 ||
                    (i < ids.size() - 1 && isChinese(id2str(ids.at(i + 1))))) {
                    // ⚠ 这里必须切掉**两个** '@'：官方 C++ runtime 用的是
                    //   std::string::erase(len - 2)。旧 Qt 移植版写成了
                    //   word.removeLast()，而 QString::removeLast() 在 Qt6 只删
                    //   **一个**字符（见 qstring.h）→ "and@@" 会变成 "and@ "，
                    //   英文子词拼接一直是错的。用 chop(2) 与官方对齐。
                    word.chop(2);
                    word += " ";
                    if (isCombining) {
                        combine += word;
                        isCombining = false;
                        word = combine;
                        combine = "";
                    }
                } else {
                    word.chop(2);
                    combine += word;
                    isCombining = true;
                    continue;
                }
            } else if (isCombining) {  // 词的结尾部分
                combine += word;
                isCombining = false;
                word = combine;
                combine = "";
            }
        }

        // step3 英文单词的空格处理
        {
            if (isChinese(word)) {
                words.push_back(word);
                isPreEnglish = false;
            } else {
                if (!isPreEnglish) {  // 前一个词是中文
                    words.push_back(word);
                    preEnglishLen = word.size();
                } else {  // 前一个词也是英文
                    if (preEnglishLen > 1) {
                        words.push_back(" ");
                        words.push_back(word);
                        preEnglishLen = word.size();
                    } else {
                        if (word.size() > 1) {
                            words.push_back(" ");
                        }
                        words.push_back(word);
                        preEnglishLen = word.size();
                    }
                }
                isPreEnglish = true;
            }
        }
    }

    if (language == "en-bpe" && combine != "") {
        combine = wordFormat(combine);
        if (words.size() != 0) {
            combine = " " + combine;
        }
        words.push_back(combine);
    }

    return words.join("");
}

// ─────────────────────────────────────────────────────────────────────────────
// FunasrSegDict
// ─────────────────────────────────────────────────────────────────────────────

FunasrSegDict::FunasrSegDict(const QString &filename) {
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly)) {
        qCritical() << "[funasr] 打不开分词词典:" << filename;
        return;
    }
    while (!file.atEnd()) {
        QByteArray line = file.readLine();
        if (line.endsWith('\n')) {
            line.chop(1);
        }
        if (line.endsWith('\r')) {
            line.chop(1);
        }
        const QList<QByteArray> lineItem = line.split('\t');
        if (lineItem.size() > 1) {
            const QString word = QString::fromUtf8(lineItem[0]);
            const QVector<QString> segs = QString::fromUtf8(lineItem[1]).split(' ');
            seg_dict_.insert(word, segs);
        }
    }
    file.close();
    qInfo() << "[funasr] 分词词典加载完成, 词条数 =" << seg_dict_.size();
}

FunasrSegDict::~FunasrSegDict() {}

QVector<QString> FunasrSegDict::tokensOfWord(const QString &word) const {
    const auto it = seg_dict_.find(word);
    if (it != seg_dict_.end()) {
        return *it;
    }
    return QVector<QString>();
}

// ─────────────────────────────────────────────────────────────────────────────
// Funasr
// ─────────────────────────────────────────────────────────────────────────────

Funasr::Funasr(QObject *parent)
    : ASRModel(parent), env_(ORT_LOGGING_LEVEL_ERROR, "paraformer"),
      session_options_{}, hw_env_(ORT_LOGGING_LEVEL_ERROR, "paraformer_hw"),
      hw_session_options_{} {
    // 任一环节失败都只置 valid=false 并打日志，**不再 exit(-1)**：
    // 这个类跑在 Conversation 的 asrThread 里，杀进程等于把整个程序带走。
    if (!initMain()) {
        valid = false;
        return;
    }
    const QJsonObject config = Config::instance()->getConfig("funasr");
    initHotword(config);
    valid = true;
}

bool Funasr::initMain() {
    const QJsonObject config = Config::instance()->getConfig("funasr");
    if (config.isEmpty()) {
        qCritical() << "[funasr] 配置里没有 funasr 段，请检查 Data/config.yaml";
        return false;
    }

    const QString modelRel = config.value(QStringLiteral("model")).toString();
    const QString cmvnRel = config.value(QStringLiteral("cmvn")).toString();
    const QString tokenRel = config.value(QStringLiteral("token")).toString();
    if (modelRel.isEmpty() || cmvnRel.isEmpty() || tokenRel.isEmpty()) {
        qCritical() << "[funasr] 配置缺项，model/cmvn/token 都是必填:"
                    << "model=" << modelRel << "cmvn=" << cmvnRel
                    << "token=" << tokenRel;
        return false;
    }

    loadParams(config);

    const QString modelPath = Config::getDataPath(modelRel);
    const QString cmvnPath = Config::getDataPath(cmvnRel);
    const QString tokenPath = Config::getDataPath(tokenRel);

    if (!QFile::exists(modelPath)) {
        qCritical() << "[funasr] 模型文件不存在:" << modelPath
                    << "（见 scripts/setup.py --only funasr-asr）";
        return false;
    }

    if (!loadCmvn(cmvnPath)) {
        return false;
    }

    vocab_ = std::make_unique<FunasrTokens>(tokenPath);
    if (!vocab_->isValid()) {
        qCritical() << "[funasr] 词表为空或解析失败:" << tokenPath;
        vocab_.reset();
        return false;
    }

    // ---- 前端：kaldi 80 维 mel-fbank，口径与 FunASR 官方 ONNX runtime 一致 ----
    fbank_opts_.frame_opts.dither = 0;
    fbank_opts_.mel_opts.num_bins = n_mels_;
    fbank_opts_.frame_opts.samp_freq = asr_sample_rate_;
    fbank_opts_.frame_opts.window_type = window_type_;
    fbank_opts_.frame_opts.frame_shift_ms = frame_shift_;
    fbank_opts_.frame_opts.frame_length_ms = frame_length_;
    fbank_opts_.energy_floor = 0;
    fbank_opts_.mel_opts.debug_mel = false;

    session_options_.SetIntraOpNumThreads(threads_);
    session_options_.SetGraphOptimizationLevel(ORT_ENABLE_ALL);
    // DisableCpuMemArena 对单次大 batch 推理更省内存（官方 runtime 的选择）
    session_options_.DisableCpuMemArena();

    const OrtPath modelOrt = toOrtPath(modelPath);
    try {
        session_ = std::make_unique<Ort::Session>(env_, modelOrt.c_str(),
                                                  session_options_);
    } catch (const std::exception &e) {
        qCritical() << "[funasr] 加载主模型失败:" << modelPath << e.what();
        session_.reset();
        return false;
    }

    readNodeNames(session_.get(), input_names_str_, input_names_, true);
    readNodeNames(session_.get(), output_names_str_, output_names_, false);

    QStringList inNames;
    for (const auto &n : input_names_str_)
        inNames << QString::fromStdString(n);
    QStringList outNames;
    for (const auto &n : output_names_str_)
        outNames << QString::fromStdString(n);
    qInfo() << "[funasr] 模型加载完成:" << modelPath;
    qInfo() << "[funasr]   输入 =" << inNames << " 输出 =" << outNames;

    // 位置约定：官方导出固定为 [speech, speech_lengths]（热词版追加第三个输入）
    if (input_names_str_.size() < 2 || output_names_str_.size() < 2) {
        qCritical() << "[funasr] 模型接口不符合预期（至少需要 2 输入 / 2 输出）:"
                    << inNames << outNames;
        return false;
    }
    if (!input_names_str_[0].empty() &&
        input_names_str_[0].find("speech") == std::string::npos) {
        qWarning() << "[funasr] 第 1 个输入不是 speech(特征), 实际为"
                   << QString::fromStdString(input_names_str_[0])
                   << "——不是官方 Paraformer 导出包？";
    }

    // speech_lengths 的 dtype 可能是 int32（官方导出）也可能是 int64（部分转换脚本），
    // 这里读一次真实类型缓存下来，别假设。
    length_input_type_ =
        session_->GetInputTypeInfo(1).GetTensorTypeAndShapeInfo().GetElementType();
    if (length_input_type_ != ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32 &&
        length_input_type_ != ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64) {
        qCritical() << "[funasr] speech_lengths 既不是 int32 也不是 int64，无法推理";
        return false;
    }

    // 特征维度必须与 lfr_m*n_mels 一致，否则送进去形状就错了
    const int featDim = lfr_m_ * n_mels_;
    qInfo() << "[funasr]   前端: fs=" << asr_sample_rate_ << "window="
            << QString::fromStdString(window_type_) << "frame=" << frame_length_
            << "/" << frame_shift_ << "ms n_mels=" << n_mels_
            << "lfr=" << lfr_m_ << "/" << lfr_n_ << "→ feat_dim=" << featDim
            << " threads=" << threads_;
    return true;
}

void Funasr::loadParams(const QJsonObject &config) {
    const auto intOf = [&config](const char *key, int def) {
        const QJsonValue v = config.value(QLatin1String(key));
        return v.isDouble() ? v.toInt(def) : def;
    };
    lfr_m_ = std::max(1, intOf("lfr_m", lfr_m_));
    lfr_n_ = std::max(1, intOf("lfr_n", lfr_n_));
    frame_length_ = std::max(1, intOf("frame_length", frame_length_));
    frame_shift_ = std::max(1, intOf("frame_shift", frame_shift_));
    n_mels_ = std::max(1, intOf("n_mels", n_mels_));
    encoder_size_ = std::max(1, intOf("encoder_size", encoder_size_));
    asr_sample_rate_ = std::max(1, intOf("sample_rate", asr_sample_rate_));
    threads_ = std::max(1, intOf("threads", threads_));

    const QJsonValue win = config.value(QStringLiteral("window"));
    if (win.isString() && !win.toString().isEmpty()) {
        window_type_ = win.toString().toStdString();
    }
    const QJsonValue lang = config.value(QStringLiteral("language"));
    if (lang.isString() && !lang.toString().isEmpty()) {
        language_ = lang.toString();
    }
}

bool Funasr::loadCmvn(const QString &filename) {
    // ⚠ 用 QFile 而不是 ifstream：Windows 上非 ASCII 路径（中文用户名 / 安装目录）
    //   走 std::ifstream 会直接打开失败。
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCritical() << "[funasr] 打不开 am.mvn:" << filename;
        return false;
    }
    const std::string raw = file.readAll().toStdString();
    file.close();

    means_list_.clear();
    vars_list_.clear();
    istringstream cmvnStream(raw);
    string line;
    while (getline(cmvnStream, line)) {
        istringstream iss(line);
        vector<string> lineItem{istream_iterator<string>{iss},
                                istream_iterator<string>{}};
        // ⚠ 空行会让 line_item 为空，旧代码 line_item[0] 直接越界
        if (lineItem.empty()) {
            continue;
        }
        if (lineItem[0] == "<AddShift>") {
            if (!getline(cmvnStream, line)) {
                break;
            }
            istringstream meansStream(line);
            vector<string> meansLines{istream_iterator<string>{meansStream},
                                      istream_iterator<string>{}};
            if (!meansLines.empty() && meansLines[0] == "<LearnRateCoef>") {
                for (size_t j = 3; j + 1 < meansLines.size(); j++) {
                    means_list_.push_back(stof(meansLines[j]));
                }
            }
        } else if (lineItem[0] == "<Rescale>") {
            if (!getline(cmvnStream, line)) {
                break;
            }
            istringstream varsStream(line);
            vector<string> varsLines{istream_iterator<string>{varsStream},
                                     istream_iterator<string>{}};
            if (!varsLines.empty() && varsLines[0] == "<LearnRateCoef>") {
                for (size_t j = 3; j + 1 < varsLines.size(); j++) {
                    vars_list_.push_back(stof(varsLines[j]) * scale_);
                }
            }
        }
    }

    if (means_list_.empty() || means_list_.size() != vars_list_.size()) {
        qCritical() << "[funasr] am.mvn 解析异常: AddShift" << means_list_.size()
                    << "项 / Rescale" << vars_list_.size() << "项";
        return false;
    }
    const int expect = lfr_m_ * n_mels_;
    if (static_cast<int>(means_list_.size()) != expect) {
        qWarning() << "[funasr] am.mvn 维度" << means_list_.size()
                   << "与 lfr_m*n_mels =" << expect
                   << "不一致，CMVN 将按较小者生效";
    }
    qInfo() << "[funasr] am.mvn 加载完成, 维度 =" << means_list_.size();
    return true;
}

void Funasr::initHotword(const QJsonObject &config) {
    // 热词（paraformer-seaco）三项必须齐全，缺一即静默降级为普通模型 ——
    // 官方导出包只有 2 个输入，走不到这里。
    const QString hwRel = config.value(QStringLiteral("hw_model")).toString();
    const QString segRel = config.value(QStringLiteral("segdict")).toString();
    const QString hotwordRel = config.value(QStringLiteral("hotword")).toString();
    if (hwRel.isEmpty() || segRel.isEmpty() || hotwordRel.isEmpty()) {
        return;
    }

    const std::string hwModel = Config::getDataPath(hwRel).toStdString();
    const OrtPath hwModelOrt = toOrtPath(Config::getDataPath(hwRel));
    hw_session_options_.SetIntraOpNumThreads(1);
    hw_session_options_.SetGraphOptimizationLevel(ORT_ENABLE_ALL);
    hw_session_options_.DisableCpuMemArena();
    try {
        hw_session_ = std::make_unique<Ort::Session>(hw_env_, hwModelOrt.c_str(),
                                                     hw_session_options_);
    } catch (const std::exception &e) {
        // 不退出：热词只是增强，缺了照样能识别
        qWarning() << "[funasr] 热词编码模型加载失败，退化为普通模式:"
                   << QString::fromStdString(hwModel) << e.what();
        hw_session_.reset();
        return;
    }
    readNodeNames(hw_session_.get(), hw_input_names_str_, hw_input_names_, true);
    readNodeNames(hw_session_.get(), hw_output_names_str_, hw_output_names_, false);

    seg_dict_ = std::make_unique<FunasrSegDict>(Config::getDataPath(segRel));
    if (!seg_dict_->isValid()) {
        qWarning() << "[funasr] 分词词典为空，退化为普通模式";
        seg_dict_.reset();
        hw_session_.reset();
        return;
    }

    QFile hotwordFile(Config::getDataPath(hotwordRel));
    if (!hotwordFile.open(QIODevice::ReadOnly)) {
        qWarning() << "[funasr] 热词表打不开，退化为普通模式:" << hotwordRel;
        seg_dict_.reset();
        hw_session_.reset();
        return;
    }
    QString hotwords;
    QVector<QString> hotLines;
    while (!hotwordFile.atEnd()) {
        QString line = QString::fromUtf8(hotwordFile.readLine()).trimmed();
        if (!line.isEmpty()) {
            hotLines.push_back(line);
        }
    }
    hotwordFile.close();
    hotwords = hotLines.join(' ');

    // 先把这个会话标成"可用"，CompileHotwordEmbedding 才能编译出 embedding
    use_hotword_ = true;
    hw_emb_ = compileHotwordEmbedding(hotwords);
    if (hw_emb_.empty() || hw_emb_.front().empty()) {
        qWarning() << "[funasr] 热词 embedding 生成失败，退化为普通模式";
        use_hotword_ = false;
        hw_emb_.clear();
        hw_session_.reset();
        seg_dict_.reset();
        return;
    }
    if (static_cast<int>(hw_emb_.front().size()) != encoder_size_) {
        qWarning() << "[funasr] 热词 embedding 维度" << hw_emb_.front().size()
                   << "与配置 encoder_size" << encoder_size_
                   << "不一致（以模型输出为准）";
    }
    qInfo() << "[funasr] 热词模式已启用, 热词条数 =" << hotLines.size()
            << " embedding 形状 =" << hw_emb_.size() << "x" << hw_emb_[0].size();
}

Funasr::~Funasr() { stop(); }

bool Funasr::isStream() { return false; }

void Funasr::stop() {
    valid = false;
    // 幂等：析构 / 退出流程都会调，逐句柄判空释放
    hw_emb_.clear();
    seg_dict_.reset();
    vocab_.reset();
    hw_session_.reset();
    session_.reset();
}

void Funasr::clear() { result_.clear(); }

void Funasr::detect(const QByteArray &data, bool isLast, int id) {
    Q_UNUSED(isLast);
    result_.clear();
    // 模型没起来也必须回一个空结果：调用方在等 recognized()
    if (!valid || !session_) {
        emit recognized(QString(), id);
        return;
    }

    const int dataLength = data.length() / 2;  // 16bit PCM
    if (dataLength <= 0) {
        emit recognized(QString(), id);
        return;
    }

    const int16_t *intData = reinterpret_cast<const int16_t *>(data.constData());
    samples_.resize(static_cast<size_t>(dataLength));
    for (int i = 0; i < dataLength; i++) {
        samples_[i] = intData[i] / 32768.0f;
    }

    forward(samples_.data(), dataLength, true);
    emit recognized(result_, id);
    result_.clear();
}

void Funasr::computeFbank(const float *waves, int len,
                          std::vector<std::vector<float>> &feats) const {
    // kaldi 的 fbank 想要 int16 量级的输入，所以这里乘回 32768
    knf::OnlineFbank fbank(fbank_opts_);
    std::vector<float> buf(static_cast<size_t>(len));
    for (int i = 0; i != len; ++i) {
        buf[i] = waves[i] * 32768.0f;
    }
    fbank.AcceptWaveform(asr_sample_rate_, buf.data(),
                         static_cast<int32_t>(buf.size()));

    const int32_t frames = fbank.NumFramesReady();
    feats.reserve(static_cast<size_t>(frames));
    for (int32_t i = 0; i != frames; ++i) {
        const float *frame = fbank.GetFrame(i);
        feats.emplace_back(frame, frame + fbank_opts_.mel_opts.num_bins);
    }
}

void Funasr::applyLfrCmvn(std::vector<std::vector<float>> &feats) const {
    if (feats.empty()) {
        return;
    }
    const int t0 = static_cast<int>(feats.size());
    // ⚠ T_lrf 用**补齐前**的帧数算，这是 FunASR `apply_lfr` 的口径，
    //   改成补齐后的帧数会多出一帧，识别结果会变。
    const int tLrf = static_cast<int>(std::ceil(1.0 * t0 / lfr_n_));
    const int left = (lfr_m_ - 1) / 2;

    // 左侧复制首帧补 left 帧。
    // ⚠ 先把首帧拷成独立副本再 insert：直接 insert(begin(), feats[0]) 是把
    //   "指向容器自身的引用"交给 insert，语义上依赖 LWG 526 的实现兜底
    //  （官方 FunASR runtime 就是这么写的，能跑但很脆）。
    if (left > 0) {
        const std::vector<float> first = feats.front();
        feats.insert(feats.begin(), static_cast<size_t>(left), first);
    }
    const int t = t0 + left;

    std::vector<std::vector<float>> outFeats;
    outFeats.reserve(static_cast<size_t>(tLrf));
    std::vector<float> p;
    for (int i = 0; i < tLrf; i++) {
        p.clear();
        if (lfr_m_ <= t - i * lfr_n_) {
            for (int j = 0; j < lfr_m_; j++) {
                p.insert(p.end(), feats[i * lfr_n_ + j].begin(),
                         feats[i * lfr_n_ + j].end());
            }
        } else {
            // 最后一个窗口凑不满 lfr_m 帧：先接上剩余的，再用末帧补齐
            const int numPadding = lfr_m_ - (t - i * lfr_n_);
            const int remain = t - i * lfr_n_;
            for (int j = 0; j < remain; j++) {
                p.insert(p.end(), feats[i * lfr_n_ + j].begin(),
                         feats[i * lfr_n_ + j].end());
            }
            const std::vector<float> &last = feats.back();
            for (int j = 0; j < numPadding; j++) {
                p.insert(p.end(), last.begin(), last.end());
            }
        }
        outFeats.emplace_back(p);
    }

    // CMVN：(x + shift) * scale，逐维
    const size_t n = std::min(means_list_.size(), vars_list_.size());
    for (auto &outFeat : outFeats) {
        const size_t m = std::min(n, outFeat.size());
        for (size_t j = 0; j < m; j++) {
            outFeat[j] = (outFeat[j] + means_list_[j]) * vars_list_[j];
        }
    }
    feats.swap(outFeats);
}

void Funasr::forward(const float *din, int len, bool inputFinished) {
    Q_UNUSED(inputFinished);

    std::vector<std::vector<float>> asrFeats;
    computeFbank(din, len, asrFeats);
    if (asrFeats.empty()) {
        qWarning() << "[funasr] 音频太短，fbank 一帧都没算出来（len =" << len << "）";
        return;
    }
    applyLfrCmvn(asrFeats);

    // ⚠ T_lrf 只有 1 帧时，模型的 CIF predictor 会算出 0 个 token，图里的 Reshape
    //   直接失败（onnxruntime 报 "input tensor cannot be reshaped"，实测 <100ms 的
    //   输入就会命中）。异常虽然会被下面的 catch 接住不至于崩，但会往 stderr 刷
    //   一大段 ORT 报错；这里提前挡掉——这种长度的输入本来就是 VAD 误触发。
    if (asrFeats.size() < 2) {
        qWarning() << "[funasr] 音频过短（LFR 后只有" << asrFeats.size()
                   << "帧），跳过识别";
        return;
    }

    const int32_t featDim = lfr_m_ * fbank_opts_.mel_opts.num_bins;
    const int32_t numFrames = static_cast<int32_t>(asrFeats.size());
    std::vector<float> wavFeats;
    wavFeats.reserve(static_cast<size_t>(featDim) * numFrames);
    for (const auto &frameFeat : asrFeats) {
        wavFeats.insert(wavFeats.end(), frameFeat.begin(), frameFeat.end());
    }
    if (static_cast<int32_t>(wavFeats.size()) != featDim * numFrames) {
        qCritical() << "[funasr] 特征维度不齐:" << wavFeats.size() << "!="
                    << featDim * numFrames;
        return;
    }

    // ⚠⚠ Ort::Value::CreateTensor 只**记住裸指针**，不接管 buffer 的所有权。
    //   所以承载张量数据的量必须活到 session_->Run() 返回为止 —— 一律放在函数
    //   作用域（旧代码把 heatword embedding 放在内层 {} 里，出块即悬垂）。
    int32_t speechLen32 = numFrames;
    int64_t speechLen64 = numFrames;
    std::vector<float> hwEmbedding;
    if (use_hotword_) {
        if (hw_emb_.empty() || hw_emb_.front().empty()) {
            qCritical() << "[funasr] hw_emb 为空，跳过本次识别";
            return;
        }
        hwEmbedding.reserve(hw_emb_.size() * hw_emb_.front().size());
        for (const auto &item : hw_emb_) {
            hwEmbedding.insert(hwEmbedding.end(), item.begin(), item.end());
        }
    }

    Ort::MemoryInfo memInfo =
        Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    std::vector<Ort::Value> inputOnnx;
    const int64_t speechShape[3] = {1, numFrames, featDim};
    inputOnnx.emplace_back(Ort::Value::CreateTensor<float>(
        memInfo, wavFeats.data(), wavFeats.size(), speechShape, 3));
    // speech_lengths 按模型真实 dtype 构造（官方导出是 int32）
    const int64_t lenShape[1] = {1};
    if (length_input_type_ == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64) {
        inputOnnx.emplace_back(Ort::Value::CreateTensor<int64_t>(
            memInfo, &speechLen64, 1, lenShape, 1));
    } else {
        inputOnnx.emplace_back(Ort::Value::CreateTensor<int32_t>(
            memInfo, &speechLen32, 1, lenShape, 1));
    }
    if (use_hotword_) {
        const int64_t hotwordShape[3] = {
            1, static_cast<int64_t>(hw_emb_.size()),
            static_cast<int64_t>(hw_emb_.front().size())};
        inputOnnx.emplace_back(Ort::Value::CreateTensor<float>(
            memInfo, hwEmbedding.data(), hwEmbedding.size(), hotwordShape, 3));
    }

    if (inputOnnx.size() != input_names_.size()) {
        qCritical() << "[funasr] 输入个数与模型不符: 构造" << inputOnnx.size()
                    << "个 / 模型需要" << input_names_.size() << "个";
        return;
    }

    try {
        auto outputs = session_->Run(Ort::RunOptions{nullptr}, input_names_.data(),
                                     inputOnnx.data(), inputOnnx.size(),
                                     output_names_.data(), output_names_.size());
        if (outputs.size() < 2) {
            qCritical() << "[funasr] 输出个数不足 2";
            return;
        }
        const std::vector<int64_t> logitsShape =
            outputs[0].GetTensorTypeAndShapeInfo().GetShape();
        if (logitsShape.size() != 3) {
            qCritical() << "[funasr] logits 形状不是 3 维";
            return;
        }
        const int64_t tokenNums = logitsShape[2];

        // ⚠⚠ 第二个输出（官方叫 token_num / seaco 版叫 encoder_out_lens）
        //   **dtype 随导出脚本而变**：官方导出是 int32，旧代码固定按 int64_t*
        //   读 → 得到 "int32 值 + 紧邻 4 字节" 的天文数字 → 解码时越界读 logits。
        int64_t nLen = 0;
        {
            const auto info = outputs[1].GetTensorTypeAndShapeInfo();
            const auto dtype = info.GetElementType();
            if (dtype == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64) {
                nLen = *outputs[1].GetTensorMutableData<int64_t>();
            } else if (dtype == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32) {
                nLen = *outputs[1].GetTensorMutableData<int32_t>();
            } else {
                qCritical() << "[funasr] 第 2 个输出 dtype 不是 int32/int64，无法解码";
                return;
            }
        }
        if (nLen <= 0) {
            qWarning() << "[funasr] token_num =" << nLen << "，本句无输出";
            return;
        }
        if (nLen > logitsShape[1]) {
            qWarning() << "[funasr] token_num" << nLen << "超出 logits 长度"
                       << logitsShape[1] << "，按 logits 长度夹取";
            nLen = logitsShape[1];
        }
        if (tokenNums > vocab_->size()) {
            qWarning() << "[funasr] 模型词表" << tokenNums << "大于 tokens.json"
                       << vocab_->size() << "，请确认两者配套";
        }

        float *logits = outputs[0].GetTensorMutableData<float>();
        if (outputs.size() == 4) {
            // 带时间戳的导出：额外接 us_alphas / us_cif_peak
            const std::vector<int64_t> alphaShape =
                outputs[2].GetTensorTypeAndShapeInfo().GetShape();
            const float *alphaData = outputs[2].GetTensorMutableData<float>();
            std::vector<float> usAlphas(alphaData,
                                        alphaData + alphaShape.back());
            const std::vector<int64_t> peakShape =
                outputs[3].GetTensorTypeAndShapeInfo().GetShape();
            const float *peakData = outputs[3].GetTensorMutableData<float>();
            std::vector<float> usPeaks(peakData, peakData + peakShape.back());
            result_ += QString::fromStdString(greedySearch(
                logits, static_cast<int>(nLen), tokenNums, true, usAlphas, usPeaks));
        } else {
            result_ += QString::fromStdString(
                greedySearch(logits, static_cast<int>(nLen), tokenNums, false));
        }
    } catch (const std::exception &e) {
        qCritical() << "[funasr] 推理失败:" << e.what();
    }
}

std::string Funasr::greedySearch(float *in, int nLen, int64_t tokenNums,
                                 bool isStamp,
                                 const std::vector<float> &usAlphas,
                                 const std::vector<float> &usCifPeak) {
    QVector<int> hyps;
    hyps.reserve(nLen);
    for (int i = 0; i < nLen; i++) {
        float maxVal = 0;
        int maxIdx = -1;
        findMax(in + static_cast<size_t>(i) * tokenNums,
                static_cast<int>(tokenNums), maxVal, maxIdx);
        hyps.push_back(maxIdx < 0 ? 0 : maxIdx);
    }

    if (!isStamp) {
        return vocab_->detokenize(hyps, language_).toStdString();
    }

    QVector<QString> charList;
    std::vector<std::vector<float>> timestampList;
    std::string resStr;
    std::vector<string> charListStd;
    vocab_->toTokenStrings(hyps, charList);
    charListStd.reserve(charList.size());
    for (int i = 0; i < charList.size(); i++) {
        charListStd.push_back(charList.at(i).toStdString());
    }
    std::vector<string> rawChar(charListStd);
    std::vector<float> alphas = usAlphas;
    timestampOnnx(alphas, usCifPeak, charListStd, resStr, timestampList, 0, -1.5);
    return postProcess(rawChar, timestampList);
}

std::vector<std::vector<float>> Funasr::compileHotwordEmbedding(
    const QString &hotwords) const {
    const int maxHotwordLen = 10;
    std::vector<std::vector<float>> result;
    if (!use_hotword_ || !hw_session_) {
        return result;
    }

    int hotwordSize = 1;
    int realHwSize = 0;
    std::vector<int32_t> hotwordMatrix;
    std::vector<int32_t> lengths;
    if (!hotwords.isEmpty()) {
        const QVector<QString> hotwordArray = hotwords.split(' ');
        hotwordSize = hotwordArray.size() + 1;
        hotwordMatrix.reserve(static_cast<size_t>(hotwordSize) * maxHotwordLen);
        for (const QString &hotword : hotwordArray) {
            QVector<QString> chars;
            if (isChinese(hotword)) {
                keepChineseCharacterAndSplit(hotword, chars);
            } else {
                // 英文：走分词词典拆成子词
                const QVector<QString> words = hotword.split(' ');
                for (const QString &word : words) {
                    chars += seg_dict_ ? seg_dict_->tokensOfWord(word)
                                       : QVector<QString>();
                }
            }
            if (chars.isEmpty()) {
                continue;
            }
            std::vector<int32_t> hwVector(maxHotwordLen, 0);
            const int vectorLen = std::min(maxHotwordLen, static_cast<int>(chars.size()));
            bool oov = false;
            for (int i = 0; i < vectorLen; i++) {
                hwVector[i] = vocab_->str2id(chars[i]);
                if (hwVector[i] == -1) {
                    oov = true;
                    break;
                }
            }
            if (oov) {
                qInfo() << "[funasr] 热词含 OOV，已跳过:" << hotword;
                continue;
            }
            lengths.push_back(vectorLen);
            realHwSize += 1;
            hotwordMatrix.insert(hotwordMatrix.end(), hwVector.begin(),
                                 hwVector.end());
        }
        hotwordSize = realHwSize + 1;
    }
    std::vector<int32_t> blankVec(maxHotwordLen, 0);
    blankVec[0] = 1;
    hotwordMatrix.insert(hotwordMatrix.end(), blankVec.begin(), blankVec.end());
    lengths.push_back(1);

    Ort::MemoryInfo memInfo =
        Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    const int64_t inputShape[2] = {hotwordSize, maxHotwordLen};
    std::vector<Ort::Value> inputOnnx;
    inputOnnx.emplace_back(Ort::Value::CreateTensor<int32_t>(
        memInfo, hotwordMatrix.data(), hotwordMatrix.size(), inputShape, 2));

    try {
        auto outputs = hw_session_->Run(
            Ort::RunOptions{nullptr}, hw_input_names_.data(), inputOnnx.data(),
            inputOnnx.size(), hw_output_names_.data(), hw_output_names_.size());
        const std::vector<int64_t> outputShape =
            outputs[0].GetTensorTypeAndShapeInfo().GetShape();
        if (outputShape.size() != 3 || outputShape[0] != maxHotwordLen ||
            outputShape[1] != hotwordSize) {
            qWarning() << "[funasr] 热词编码模型输出形状异常，放弃热词";
            return result;
        }
        const int embeddingDim = static_cast<int>(outputShape[2]);
        const float *floatData = outputs[0].GetTensorMutableData<float>();
        for (int j = 0; j < hotwordSize; j++) {
            const int startPos =
                hotwordSize * (lengths[j] - 1) * embeddingDim + j * embeddingDim;
            result.emplace_back(floatData + startPos,
                                floatData + startPos + embeddingDim);
        }
    } catch (const std::exception &e) {
        qCritical() << "[funasr] 热词编码失败:" << e.what();
    }
    return result;
}
