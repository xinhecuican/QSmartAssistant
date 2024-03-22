#ifndef DUILITEVAD_H
#define DUILITEVAD_H
#include "VadModel.h"
#include "duilite.h"
#include <QLibrary>

class DuiliteVad : public VadModel {
public:
    DuiliteVad(QObject *parent = nullptr);
    ~DuiliteVad();
    bool detectVoice(const QByteArray &data) override;
    void detect(const QByteArray &data) override;
    void stop() override;
    int getChunkSize() override;
    void startDetect(bool isResponse = false) override;
    bool isVoice;

private:
    duilite_vad *vad;
    QLibrary lib;
    int chunkSize;
    int (*feedFunc)(struct duilite_vad *, char *, int);
    int (*stopFunc)(struct duilite_vad *);
    int (*startFunc)(struct duilite_vad *, char *);
};

#endif // DUILITEVAD_H
