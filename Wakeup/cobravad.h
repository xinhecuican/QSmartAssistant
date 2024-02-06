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
    void detect(const QByteArray& data) override;
    void stop() override;
    void startDetect() override;
private:
    pv_cobra_t* cobra;
    float confidence;
    int detectSlient;
    QTimer* undetectTimer;
    qint64 currentSlient;
    bool valid;
};

#endif // COBRAVAD_H
