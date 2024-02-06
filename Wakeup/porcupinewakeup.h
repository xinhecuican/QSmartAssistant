#ifndef PORCUPINEWAKEUP_H
#define PORCUPINEWAKEUP_H
#include "WakeupModel.h"
#include "pv_porcupine.h"

class PorcupineWakeup : public WakeupModel
{
public:
    PorcupineWakeup(QObject* parent=nullptr);
    ~PorcupineWakeup();
    void detect(const QByteArray& data) override;
    void stop() override;
private:
    pv_porcupine* porcupine;
    bool valid;
};

#endif // PORCUPINEWAKEUP_H
