#ifndef COBRAVAD_H
#define COBRAVAD_H
#include "VadModel.h"
#include "pv_cobra.h"
#include <QTimer>

class CobraVad : public VadModel
{
public:
    CobraVad(QObject* parent=nullptr);
    ~CobraVad();
    bool detectVoice(const QByteArray& data) override;
    void stop() override;
    int getChunkSize() override;
private:
    pv_cobra_t* cobra;
    float confidence;
    int chunkSize;
};

#endif // COBRAVAD_H
