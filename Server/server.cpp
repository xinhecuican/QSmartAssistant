#include "server.h"
#include "../Utils/config.h"
#include <QDebug>
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

    QJsonObject serverConfig = Config::instance()->getConfig("server");
    port = serverConfig.value("port").toInt(20214);
    auto tcpserver = new QTcpServer(this);
    if (!tcpserver->listen(QHostAddress::Any, port) || !server.bind(tcpserver)) {
        tcpserver->deleteLater();
        qWarning() << "tcp server create fail";
    }

    server.addWebSocketUpgradeVerifier(&server, [](const QHttpServerRequest &request) {
        return QHttpServerWebSocketUpgradeResponse::accept();
    });
    connect(&server, &QHttpServer::newWebSocketConnection, this, [=]() {
        while (server.hasPendingWebSocketConnections()) {
            std::unique_ptr<QWebSocket> socket = server.nextPendingWebSocketConnection();
            QWebSocket *socket_ptr = socket.get();
            socket.release();
            socket_ptr->setProperty("id", id);
            increaseId();
            connect(socket_ptr, &QWebSocket::textMessageReceived, this,
                    [=](const QString &message) {
                        QWebSocket *currentSocket = qobject_cast<QWebSocket *>(sender());
                        uint32_t currentId = currentSocket->property("id").toUInt();
                        QJsonParseError error;
                        QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &error);
                        if (error.error != QJsonParseError::NoError) {
                            qInfo() << "socket parse error";
                            return;
                        }
                        QJsonObject root = doc.object();
                        QString op = root.value("op").toString();
                        if (op == "ping") {
                            // Respond with pong
                            QJsonObject response;
                            response["type"] = "pong";
                            response["timestamp"] = root["timestamp"];
                            QJsonDocument responseDoc(response);
                            currentSocket->sendTextMessage(QString::fromUtf8(responseDoc.toJson(QJsonDocument::Compact)));
                        } else if (op == "conversation") {
                            QString question = root.value("question").toString();
                            QList<QString> texts = conversation->intentRequest(question, currentId);
                            currentSocket->sendTextMessage(texts.join("\n"));
                        }
                    });
            connect(socket_ptr, &QWebSocket::disconnected, this, [=]() {
                QWebSocket *currentSocket = qobject_cast<QWebSocket *>(sender());
                uint32_t currentId = currentSocket->property("id").toUInt();
                currentSocket->deleteLater();
            });
            connect(socket_ptr, &QWebSocket::errorOccurred, this,
                    [=](QAbstractSocket::SocketError error) {
                        qInfo() << "socket error" << error;
                        QWebSocket *currentSocket = qobject_cast<QWebSocket *>(sender());
                        uint32_t currentId = currentSocket->property("id").toUInt();
                        currentSocket->deleteLater();
                    });
        }
    });

    id = 1;
    QFile htmlFile(":/Web/index.html");
    htmlFile.open(QIODevice::ReadOnly);
    htmlData = htmlFile.readAll();
    htmlFile.close();
    server.route("/", [this]() { return QHttpServerResponse(htmlData); });
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
    server.route("/intent", QHttpServerRequest::Method::Get,
                 [=](const QHttpServerRequest &request) {
                     if (request.query().hasQueryItem("text")) {
                         QString text = request.query().queryItemValue("text");
                         uint32_t currentId = id;
                         this->increaseId();
                         return QtConcurrent::run([conversation, text, currentId]() {
                             QList<QString> texts = conversation->intentRequest(text, currentId);
                             QJsonArray array;
                             for (const QString &data : texts) {
                                 array.append(data);
                             }
                             return QHttpServerResponse(array);
                         });
                     }
                     return QtConcurrent::run([]() {
                         return QHttpServerResponse(QHttpServerResponder::StatusCode::BadRequest);
                     });
                 });

    headers.append("Access-Control-Allow-Origin", "*");
    headers.append("Access-Control-Allow-Methods", "POST,GET,PUT,DELETE");
    headers.append("Access-Control-Max-Age", "3600");
    headers.append("Access-Control-Allow-Headers", "*");
    headers.append("Access-Control-Allow-Credentials", "true");
    server.addAfterRequestHandler(
        &server, [=](const QHttpServerRequest &req, QHttpServerResponse &response) {
            response.setHeaders(headers);
        });
    server.setMissingHandler(
        &server, [](const QHttpServerRequest &request, QHttpServerResponder &responder) {
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
}

void Server::increaseId() {
    if (id == UINT_MAX) {
        id = 1;
    } else {
        id++;
    }
}
