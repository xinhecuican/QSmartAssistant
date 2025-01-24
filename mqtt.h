#ifndef __MQTT_H__
#define __MQTT_H__
#include <QObject>
#include <QtMqtt/QMqttClient>

class MQTTHandler : public QObject {
    Q_OBJECT
public:
    MQTTHandler(QObject* parent=nullptr);
public slots:
    void onWakeup();
    void onDetect(bool stop);
    void onASR(const QString& text, int id);
    void onSay(const QString& text, int id);

private:
    QString topic;
    QMqttClient* client;
};

#endif