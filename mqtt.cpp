#include "mqtt.h"
#include <QJsonObject>
#include "Utils/config.h"

MQTTHandler::MQTTHandler(QObject *parent) : QObject(parent) {
    QJsonObject config = Config::instance()->getConfig("mqtt");
    topic = config.value("topic").toString();
    client = new QMqttClient(this);
    client->setHostname(config.value("host").toString());
    client->setPort(config.value("port").toInt());
    client->setUsername(config.value("user").toString());
    client->setPassword(config.value("password").toString());
    client->connectToHost();
}

void MQTTHandler::onWakeup() {
    client->publish(topic + "wakeup");
}

void MQTTHandler::onDetect() {
    client->publish(topic + "detect");
}

void MQTTHandler::onASR(const QString& text, int id) {
    if (id == 0) {
        client->publish(topic + "question", text.toUtf8());
    }
}

void MQTTHandler::onSay(const QString& text, int id) {
    if (id == 0) {
        client->publish(topic + "response", text.toUtf8());
    }
}