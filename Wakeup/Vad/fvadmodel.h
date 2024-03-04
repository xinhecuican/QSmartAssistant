#ifndef FVAD_H
#define FVAD_H
#include "fvad.h"
#include "VadModel.h"
#include <QTimer>
#include <QLibrary>

class FVadModel : public VadModel
{
public:
    FVadModel(QObject* parent=nullptr);
    ~FVadModel();
    bool detectVoice(const QByteArray& data) override;
    void stop() override;
    int getChunkSize() override;
private:
    Fvad* fvad;
    QTimer* undetectTimer;
    int detectSlient;
    qint64 currentSlient;
    QLibrary lib;
    int(*processFunc)(Fvad*,const int16_t*,int);
    void(*freeFunc)(Fvad*);
    bool findVoice;
};

#endif // FVAD_H
