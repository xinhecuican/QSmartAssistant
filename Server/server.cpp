#include "server.h"
#include "../Utils/config.h"
#include <QMimeDatabase>
#include <QMimeType>
#include <QtConcurrent/QtConcurrent>

Server::Server(Conversation *conversation, QObject *parent) : QObject(parent) {
    this->conversation = conversation;
    // connect(this, &Server::requestASR, conversation,
    //         &Conversation::onASRRequest);
    // connect(conversation, &Conversation::asrRecognize, this,
    //         [=](QString result, int id) {
    //             asrResult.insert(id, result);
    //             waiting[id].quit();
    //         });
    id = 1;
    qDebug() << QThread::currentThreadId;
    server.route("/", []() {
        return QHttpServerResponse::fromFile(":/Web/index.html");
    });
    // server.route("/asr", QHttpServerRequest::Method::Get,
    //              [=](const QHttpServerRequest &request) {
    //                  auto &query = request.query();
    //                  if (query.hasQueryItem("text")) {
    //                      QString text = query.queryItemValue("text");
    //                      uint32_t currentId = id;
    //                      this->increaseId();
    //                      return QtConcurrent::run([=]() {
    //                          emit requestASR(text, currentId);
    //                          waiting.insert(currentId,
    //                          std::move(QEventLoop()));
    //                          waiting[currentId].exec(
    //                              QEventLoop::ExcludeUserInputEvents);
    //                          QString result = asrResult[id];
    //                          asrResult.remove(id);
    //                          return result;
    //                      });
    //                  }
    //                  QHttpServerResponse response("");
    //                  return response;
    //              });
    server.route(
        "/intent", QHttpServerRequest::Method::Get,
        [=](const QHttpServerRequest &request) {
            if (request.query().hasQueryItem("text")) {
                QString text = request.query().queryItemValue("text");
                uint32_t currentId = id;
                this->increaseId();
                return QtConcurrent::run([conversation, text, currentId]() {
                    QList<QString> texts =
                        conversation->intentRequest(text, currentId);
                    QJsonArray array;
                    for (const QString &data : texts) {
                        array.append(data);
                    }
                    return QHttpServerResponse(array);
                });
            }
            return QtConcurrent::run([]() {
                return QHttpServerResponse(
                    QHttpServerResponder::StatusCode::BadRequest);
            });
        });
    server.afterRequest([](QHttpServerResponse &&response) {
        response.setHeader("Access-Control-Allow-Origin", "*");
        response.setHeader("Access-Control-Allow-Methods",
                           "POST,GET,PUT,DELETE");
        response.setHeader("Access-Control-Max-Age", "3600");
        response.setHeader("Access-Control-Allow-Headers", "*");
        response.setHeader("Access-Control-Allow-Credentials", "true");
        return std::move(response);
    });
    server.setMissingHandler([](const QHttpServerRequest &request,
                                QHttpServerResponder &&responder) {
        QString path = QString(":/Web%1").arg(request.url().path());
        qDebug() << path;
        QFile file(path);
        if (!file.exists(path)) {
            qDebug() << "file unexist";
            responder.write(QHttpServerResponse::StatusCode::NotFound);
        } else {
            if (!file.open(QIODevice::ReadOnly)) {
                responder.write(QHttpServerResponse::StatusCode::NotFound);
            } else {
                QByteArray data = file.readAll();
                QMimeDatabase mimeDatabase;
                QMimeType mimeType = mimeDatabase.mimeTypeForFile(path);
                file.close();
                responder.write(data, mimeType.name().toLocal8Bit());
            }
        }
    });
    QJsonObject serverConfig = Config::instance()->getConfig("server");
    port = serverConfig.value("port").toInt(20214);
    server.listen(QHostAddress::Any, port);
}

void Server::increaseId() {
    if (id == UINT_MAX) {
        id = 1;
    } else {
        id++;
    }
}