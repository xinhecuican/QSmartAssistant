#ifndef __FUNASR_H__
#define __FUNASR_H__
#include "ASRModel.h"
#include "kaldi-native-fbank/csrc/feature-fbank.h"
#include "kaldi-native-fbank/csrc/online-feature.h"
#include <QMap>
#include <QVector>
#include <QHash>
#include "onnxruntime_run_options_config_keys.h"
#include "onnxruntime_cxx_api.h"
using namespace std;

class Vocab {
public:
    Vocab(const QString& filename);
    ~Vocab();
    int Size() const;
    void Vector2String(QVector<int> in, QVector<QString> &preds);
    QString Vector2StringV2(QVector<int> in, QString language="");
    QString Id2String(int id) const;
    QString WordFormat(QString word);
    int GetIdByToken(const QString &token) const;
    QString Word2Lex(const QString &word) const;

private:
    QVector<QString> vocab;
    QMap<QString, int> token_id;
    QMap<QString, QString> lex_map;
    void LoadVocabFromJson(const QString& filename);
};

class PhoneSet {
  public:
    PhoneSet(const QString& filename);
    int Size() const;
    int String2Id(QString str) const;
    QString Id2String(int id) const;
    bool Find(QString str) const;
    int GetBegSilPhnId() const;
    int GetEndSilPhnId() const;
    int GetBlkPhnId() const;

  private:
    QVector<QString> phone_;
    QHash<QString, int> phn2Id_;
    void LoadPhoneSetFromJson(const QString& filename);
};

class SegDict {
  private:
    QMap<QString, QVector<QString>> seg_dict;

  public:
    SegDict(const QString& filename);
    ~SegDict();
    QVector<QString> GetTokensByWord(const QString &word);
};

class Funasr : public ASRModel {
    Q_OBJECT
public:
    Funasr(QObject* parent=nullptr);
    ~Funasr();
    bool isStream() override;
    void detect(const QByteArray& data, bool isLast=false, int id=0) override;

private:
    void LoadCmvn(const char *filename);
    void InitHwCompiler(const std::string &hw_model, int thread_num);
    void InitSegDict(const QString &seg_dict_model);
    string GreedySearch( float* in, int n_len, int64_t token_nums,
                            bool is_stamp=false, std::vector<float> us_alphas={0}, std::vector<float> us_cif_peak={0});
    void FbankKaldi(float sample_rate, const float* waves, int len, std::vector<std::vector<float>> &asr_feats);
    void Forward(float* din, int len, bool input_finished);
    std::vector<std::vector<float>> CompileHotwordEmbedding(QString &hotwords);
    void LfrCmvn(std::vector<std::vector<float>> &asr_feats);

private:
    std::shared_ptr<Ort::Session> m_session_ = nullptr;
    Ort::Env env_;
    Ort::SessionOptions session_options_;
    vector<string> m_strInputNames, m_strOutputNames;
    vector<const char*> m_szInputNames;
    vector<const char*> m_szOutputNames;
    QString language="zh-cn";
    std::shared_ptr<Ort::Session> hw_m_session = nullptr;
    Ort::Env hw_env_;
    Ort::SessionOptions hw_session_options;
    knf::FbankOptions fbank_opts_;
    vector<string> hw_m_strInputNames, hw_m_strOutputNames;
    vector<const char*> hw_m_szInputNames;
    vector<const char*> hw_m_szOutputNames;
    bool use_hotword = false;

    Vocab* vocab = nullptr;
    PhoneSet* phone_set_ = nullptr;
    SegDict* seg_dict = nullptr;
    vector<float> means_list_;
    vector<float> vars_list_;

    float* samples = nullptr;
    int sampleLen;
    std::vector<std::vector<float>> hw_emb;
    QString result;
    int lfr_m = 7;
    int lfr_n = 6;
    string window_type = "hamming";
    int frame_length = 25;
    int frame_shift = 10;
    int n_mels = 80;
    int encoder_size = 512;
    int fsmn_layers = 16;
    int fsmn_lorder = 10;
    int fsmn_dims = 512;
    float cif_threshold = 1.0;
    float tail_alphas = 0.45;
    int asr_sample_rate = 16000;
    int batch_size_ = 1;
    const float scale = 1.0;
};

#endif
