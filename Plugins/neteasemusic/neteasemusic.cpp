#include "neteasemusic.h"
#include "../../Recorder/player.h"
#include "../../Utils/config.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QThread>
#include <stdlib.h>
#include <time.h>

NeteaseMusic::NeteaseMusic()
    : isLogin(false), lastRecommandTime(0), styleTime(0), styleCursor(0) {
    srand(time(0));
}

NeteaseMusic::~NeteaseMusic() {
#ifdef NETEASE_USE_JS
    process.kill();
#endif
}

QString NeteaseMusic::getName() { return "NeteaseMusic"; }

void NeteaseMusic::setPluginHelper(IPluginHelper *helper) {
    this->helper = helper;
    QJsonObject neteaseConfig = helper->getConfig()->getConfig("netease_cloud");

#ifdef NETEASE_USE_JS
    QString homeDir = neteaseConfig.value("homeDir").toString();
    QString port = neteaseConfig.value("port").toString();
    this->port = "3000";
    if (port != 0) {
        this->port = port;
        qputenv("PORT", port.toStdString().c_str());
    }
    connect(&process, &QProcess::errorOccurred, this,
            [=](QProcess::ProcessError error) {
                qWarning() << "netease node api error" << error;
            });
    connect(&process, &QProcess::readyReadStandardOutput, this, [=]() {
        login();
        qDebug() << process.readAllStandardOutput();
    });
    process.setWorkingDirectory(homeDir);
    process.start("node", {homeDir + "/app.js"});
#else
    login();
#endif

    searchTrigger = {"搜索", "找", "播放", "听", "放", "来", "唱", "再来"};
    connect(helper->getPlayer(), &Player::playEnd, this, [=](QVariant meta) {
        QVariantMap metaMap = meta.toMap();
        if (metaMap.contains("type") && metaMap["type"] == "song") {
            qint64 id = metaMap["id"].toLongLong();
            musicInfoMap.remove(id);
        }
        if (helper->getPlayer()->normalEnd()) {
            searchDefault();
            // helper->quitImmersive(getName());
        }
    });
    connect(helper->getPlayer(), &Player::playStart, this, [=](QVariant meta) {
        QVariantMap metaMap = meta.toMap();
        if (metaMap.contains("type") && metaMap["type"] == "song") {
            qint64 id = metaMap["id"].toLongLong();
            MusicInfo musicInfo = musicInfoMap[id];
            qInfo() << "play" << musicInfo.name << musicInfo.artist
                    << musicInfo.url;
        }
    });
}

void NeteaseMusic::login() {
    if (isLogin)
        return;
    QJsonObject neteaseConfig = helper->getConfig()->getConfig("netease_cloud");
    QVariantMap params;
    QFile cookieFile(Config::getDataPath("Tmp/netease_cookie"));
    if (!cookieFile.open(QIODevice::ReadOnly)) {
        qWarning() << "netease cookie file open error";
    } else {
        cookie = cookieFile.readAll();
        cookieFile.close();
    }
    volumeStep = neteaseConfig.value("volumeStep").toInt();
    QVariantMap result;
    result = invokeMethod("login_status", params);
    bool loginanoni = result["body"]
                          .toMap()["data"]
                          .toMap()["account"]
                          .toMap()["anonimousUser"]
                          .toBool();
    if (result["status"] != 200) {
        result = invokeMethod("login_status", params);
    }
    if (cookie == "" || (result["status"] != 200) || loginanoni) {
        params["phone"] = neteaseConfig.value("phone").toString();
        params["password"] = neteaseConfig.value("password").toString();
        bool success = false;
        for (int i = 0; i < 5; i++) {
            result = invokeMethod("login_cellphone", params);
            if (result["status"] == 200) {
                QString importCookie = QUrl::toPercentEncoding(
                    result["cookie"].toString().toLocal8Bit());
                if (importCookie != "") {
                    cookie = importCookie;
                    if (!cookieFile.open(QIODevice::WriteOnly |
                                         QIODevice::Truncate)) {
                        qWarning() << "netease cookie write error";
                    } else {
                        cookieFile.write(cookie.toLocal8Bit());
                        cookieFile.close();
                    }
                }
                success = true;
                isLogin = true;
                break;
            } else {
                if (result["body"] != "") {
                    qWarning() << result["body"].toMap()["message"].toString();
                }
                QThread::msleep(1000);
            }
        }
        if (!success && cookie == "") {
            params.clear();
            result = invokeMethod("register_anonimous", params);
            if (result["status"] == 200) {
                QString importCookie = QUrl::toPercentEncoding(
                    result["cookie"].toString().toLocal8Bit());
                if (importCookie != "") {
                    cookie = importCookie;
                    if (!cookieFile.open(QIODevice::WriteOnly |
                                         QIODevice::Truncate)) {
                        qWarning() << "netease cookie write error";
                    } else {
                        cookieFile.write(cookie.toLocal8Bit());
                        cookieFile.close();
                    }
                }
                isLogin = true;
            } else {
                qWarning() << "netease login fail";
                isLogin = false;
            }
        }
    } else {
        isLogin = true;
    }
    if (isLogin) {
        params.clear();
        result = invokeMethod("style_preference", params);
        if (result["status"] == 200) {
            QList<QVariant> styleList = result["body"]
                                            .toMap()["data"]
                                            .toMap()["tagPreferenceVos"]
                                            .toList();
            for (int i = 0; i < styleList.size(); i++) {
                QVariantMap style = styleList[i].toMap();
                preferTags.push_back(style["tagId"].toInt());
            }
        }
    }
}

void NeteaseMusic::recvMessage(const QString &text,
                               const ParsedIntent &parsedIntent,
                               const PluginMessage &message) {}

bool NeteaseMusic::handle(const QString &text, const ParsedIntent &parsedIntent,
                          bool &isImmersive) {
    if (!isLogin) {
        login();
        if (!isLogin)
            return false;
    }
    isSearchTrigger = false;
    for (auto &trigger : searchTrigger) {
        if (text.contains(trigger)) {
            isSearchTrigger = true;
            break;
        }
    }
    isSearch = false;
    float conf = 0;
    for (auto &intent : parsedIntent.intents) {
        if (intent.name == "MUSICRANK" || intent.name == "MUSICINFO" ||
            intent.name == "MUSICPROP" || intent.name == "OPEN_MUSIC") {
            isSearch = true;
            conf = intent.conf;
        }
    }
    bool needHandle = isSearchTrigger && isSearch && conf > 0.5;
    bool result = needHandle || isImmersive;
    if (needHandle || isImmersive) {
        if (needHandle && !isImmersive)
            isImmersive = true;
        bool success = doHandle(text, parsedIntent, isImmersive);
        result = result & success;
    }
    return result;
}

bool NeteaseMusic::doHandle(const QString &text,
                            const ParsedIntent &parsedIntent,
                            bool &isImmersive) {
    if (text.contains("这首歌")) {
        getCurrentTrack();
    } else if (parsedIntent.hasIntent("CLOSE") ||
               text.contains("退出")) {
        isImmersive = false;
        helper->getPlayer()->stop();
    } else if (parsedIntent.hasIntent("PAUSE")) {
        helper->getPlayer()->pause();
    } else if (parsedIntent.hasIntent("CONTINUE")) {
        helper->getPlayer()->resume();
    } else if (parsedIntent.hasIntent("CHANGE_VOL")) {
        PluginMessage message;
        message.dst = "VoiceControl";
        message.message = "handle";
        emit sendMessage(message);
    } else if (parsedIntent.hasIntent("CHANGE_TO_NEXT")) {
        helper->getPlayer()->next();
    } else if (parsedIntent.hasIntent("CHNAGE_TO_LAST")) {
        helper->getPlayer()->previous();
    }
    else if (parsedIntent.hasIntent("LIKE")) {
        likeCurrent(true);
    }
    else if(parsedIntent.hasIntent("UNLIKE")){
        likeCurrent(false);
    }
    else if (isSearch && isSearchTrigger) {
        searchAlbum(parsedIntent);
    } else {
        qInfo() << "netease can't understand" << text;
        return false;
    }
    return true;
}

void NeteaseMusic::getCurrentTrack() {
    QVariantMap meta = helper->getPlayer()->getCurrentMeta().toMap();
    if (meta.contains("type") && meta["type"] == "song") {
        qint64 id = meta["id"].toLongLong();
        MusicInfo musicInfo = musicInfoMap[id];
        helper->say("这首歌叫" + musicInfo.name + ".歌手是" + musicInfo.artist);
    }
}

void NeteaseMusic::searchAlbum(const ParsedIntent &parsedIntent) {
    QList<QString> singerName, songName, songType;
    if (parsedIntent.hasIntent("MUSICINFO")) {
        auto intent = parsedIntent.intents["MUSICINFO"];
        for (auto &slot : intent.intentSlots) {
            if (slot.name == "user_music_name") {
                songName.append(slot.value);
            } else if (slot.name == "user_singer_name") {
                singerName.append(slot.value);
            } else if (slot.name == "user_music_type") {
                songType.append(slot.value);
            }
        }
    } else if (parsedIntent.hasIntent("MUSICPROP")) {
        auto intent = parsedIntent.intents["MUSICPROP"];
        for (auto &slot : intent.intentSlots) {
            if (slot.name == "user_music_name") {
                songName.append(slot.value);
            } else if (slot.name == "user_singer_name") {
                singerName.append(slot.value);
            }
        }
    } else if (parsedIntent.hasIntent("MUSICRANK")) {
        auto intent = parsedIntent.intents["MUSICRANK"];
        for (auto &slot : intent.intentSlots) {
            if (slot.name == "user_music_name") {
                songName.append(slot.value);
            } else if (slot.name == "user_singer_name") {
                singerName.append(slot.value);
            } else if (slot.name == "user_music_type") {
                songType.append(slot.value);
            }
        }
    }
    setPlaylist(singerName, songName);
}

void NeteaseMusic::setPlaylist(const QList<QString> &singerName,
                               const QList<QString> &songName) {
    QString searchWord = "";
    bool songValid = songName.size() != 0;
    bool singerValid = singerName.size() != 0;
    if (songValid)
        searchWord = songName.join(' ');
    if (songValid && singerValid)
        searchWord.append(' ');
    if (singerValid)
        searchWord = searchWord.append(singerName.join(' '));
    QVariantMap params;
    params["keywords"] = searchWord;
    if (songValid || singerValid) {
        QVariantMap result = invokeMethod("cloudsearch", params);
        if (result["status"] == 200) {
            QList<QVariant> songs =
                result["body"].toMap()["result"].toMap()["songs"].toList();
            if (songs.size() > 0) {
                QList<qint64> ids;
                for (int i = 0; i < 3 && i < songs.size(); i++) {
                    QVariantMap song = songs[i].toMap();
                    ids.append(song["id"].toLongLong());
                }
                QList<QString> urls = getAudio(ids);
                for (int i = 0; i < urls.size(); i++) {
                    if (urls[i] != "") {
                        QVariantMap song = songs.at(i).toMap();
                        MusicInfo musicInfo;
                        musicInfo.id = ids[i];
                        musicInfo.url = urls[i];
                        musicInfo.name = song["name"].toString();
                        musicInfo.artist = getArtist(song);
                        musicInfoMap.insert(ids[i], musicInfo);
                        QVariantMap meta = {{"type", "song"},
                                            {"id", musicInfo.id}};
                        helper->getPlayer()->play(musicInfo.url,
                                                  AudioPlaylist::NORMAL, meta);
                        break;
                    }
                }
            }
        }
    } else {
        searchDefault();
    }
}

QList<QString> NeteaseMusic::getAudio(QList<qint64> ids) {
    QList<QString> urls;
    QVariantMap params;
    QStringList idList;
    for (int i = 0; i < ids.size(); i++) {
        idList.append(QString::number(ids[i]));
    }
    params["id"] = idList.join(",");
    params["level"] = "hires";
    QVariantMap result = invokeMethod("song_url", params);
    QMap<qint64, QString> idUrlMap;
    QVariantMap body = result["body"].toMap();
    if (body.contains("data")) {
        QList<QVariant> songs = body["data"].toList();
        for (auto &songv : songs) {
            QVariantMap song = songv.toMap();
            idUrlMap.insert(song["id"].toLongLong(), song["url"].toString());
        }
        for (auto &id : ids) {
            urls.append(idUrlMap[id]);
        }
    } else {
        helper->say("网络连接出错了");
    }
    return urls;
}

QString NeteaseMusic::getArtist(const QVariantMap &song) {
    QList<QVariant> artists = song["ar"].toList();
    QString result;
    for (auto &artistv : artists) {
        QVariantMap artist = artistv.toMap();
        result += artist["name"].toString() + ",";
    }
    return result;
}

QVariantMap NeteaseMusic::invokeMethod(QString name, QVariantMap &args) {
    QVariantMap ret;
    if (cookie.size() != 0) {
        args["cookie"] = cookie;
    }
#ifndef NETEASE_USE_JS
    api.invoke(name, args);
    // QMetaObject::invokeMethod(&api, name.toUtf8(), Qt::DirectConnection,
    //                           Q_RETURN_ARG(QVariantMap, ret),
    //                           Q_ARG(QVariantMap, args));
#else
    name.replace('_', '/');
    QUrl url("http://127.0.0.1:" + port + "/" + name);
    QString query;
    if (args.size() != 0) {
        for (auto iter = args.begin(); iter != args.end(); iter++) {
            query += iter.key() + "=" +
                     QUrl::toPercentEncoding(iter.value().toString()) + "&";
        }
        query.remove(query.size() - 1, 1);
    }
    // request.setUrl(url);
    // QNetworkReply *reply = manager.get(request);
    // QEventLoop eventLoop;
    // connect(reply, &QNetworkReply::finished, &eventLoop, &QEventLoop::quit);
    // eventLoop.exec(QEventLoop::ExcludeUserInputEvents);
    // QByteArray body = reply->readAll();
    // QString bodystr = QString::fromUtf8(body);
    // QVariantMap bodyMap = QVariant(bodystr).toMap();
    QProcess curlProcess;
    QStringList curlArgs;
    curlArgs << "http://127.0.0.1:" + port + "/" + name +
                    (args.size() != 0 ? "?" + query : "");
    curlArgs << "-H" << "charset: utf-8";
    curlProcess.start("curl", curlArgs);
    curlProcess.waitForFinished();
    QString body = curlProcess.readAllStandardOutput();
    QVariantMap bodyMap =
        QJsonDocument::fromJson(body.toLocal8Bit()).object().toVariantMap();
    //    if (reply->header(QNetworkRequest::SetCookieHeader).isValid()) {
    //        QList<QNetworkCookie> cookies =
    //        reply->header(QNetworkRequest::SetCookieHeader)
    //                           .value<QList<QNetworkCookie>>();
    //        if(cookies.size() > 0){
    //            QNetworkCookie& cookie = cookies[0];
    //            cookie.setDomain("");
    //            ret["cookie"] = cookie.toRawForm();
    //        }
    //    }
    if (bodyMap.contains("status")) {
        ret["status"] = bodyMap["status"];
        ret["body"] = bodyMap["body"];
    } else {
        ret["body"] = bodyMap;
        if (bodyMap.contains("data")) {
            ret["status"] = bodyMap["data"].toMap().value("code", 200);
        } else {
            ret["status"] = bodyMap.value("code", 200);
        }
        ret["cookie"] = bodyMap.value("cookie");
    }
    // reply->deleteLater();
#endif
    return ret;
}

void NeteaseMusic::parseSongs(const QList<QVariant> &songs) {
    if (songs.size() > 0) {
        QList<qint64> ids;
        for (int i = 0; i < songs.size(); i++) {
            QVariantMap song = songs[i].toMap();
            ids.append(song["id"].toLongLong());
        }
        QList<QString> urls = getAudio(ids);
        int currentNumber = helper->getPlayer()->getAudioNumber();
        bool isPlaying = helper->getPlayer()->isPlaying();
        for (int i = 0; i < urls.size(); i++) {
            if (urls[i] != "") {
                QVariantMap song = songs.at(i).toMap();
                MusicInfo musicInfo;
                musicInfo.id = ids[i];
                musicInfo.url = urls[i];
                musicInfo.name = song["name"].toString();
                musicInfo.artist = getArtist(song);
                musicInfoMap.insert(ids[i], musicInfo);
                QVariantMap meta = {{"type", "song"}, {"id", musicInfo.id}};
                helper->getPlayer()->play(musicInfo.url, AudioPlaylist::NORMAL,
                                          meta);
            }
        }
        if (urls.size() != 0 && isPlaying) {
            helper->getPlayer()->play(currentNumber);
        }
    }
}

void NeteaseMusic::searchDefault() {
    QVariantMap params;
    double possibility = ((double)rand() / (RAND_MAX));
    if (QDateTime::currentMSecsSinceEpoch() - lastRecommandTime >
        24 * 60 * 60 * 1000) {
        lastRecommandTime = QDateTime::currentMSecsSinceEpoch();
        double possibility2 = ((double)rand() / (RAND_MAX));
        // 歌单id来源: https://nie.su/archives/2229.html
        if (possibility2 > 0.3) {
            params["id"] = "3136952023"; // 私人雷达id
        } else {
            params["id"] = "5300458264"; // 新歌雷达id
        }
        QVariantMap audioResult = invokeMethod("playlist_track_all", params);
        if (audioResult["status"] == 200) {
            QList<QVariant> songs =
                audioResult["body"].toMap()["songs"].toList();
            parseSongs(songs);
        } else {
            helper->say("网络连接出错了");
        }
    } else if (possibility > 0.95) { // 每日新歌
        params["limit"] = 20;
        QVariantMap result = invokeMethod("personalized_newsong", params);
        if (result["status"] == 200) {
            QList<QVariant> songs = result["body"].toMap()["result"].toList();
            parseSongs(songs);
        }
    } else if (possibility > 0.7) { // 根据曲风偏好搜歌
        int tagId = 1000;
        if (preferTags.size() > 0) {
            int index = 0;
            while (((double)rand() / (RAND_MAX)) > 0.5 &&
                   index < preferTags.size() - 1) {
                index++;
            }
            tagId = preferTags[index];
        }
        params["tagId"] = tagId;
        if (QDateTime::currentMSecsSinceEpoch() - styleTime >
            24 * 60 * 60 * 1000) {
            styleTime = QDateTime::currentMSecsSinceEpoch();
            styleCursor = 0;
        }
        params["cursor"] = styleCursor;
        params["size"] = 20;
        QVariantMap result = invokeMethod("style_song", params);
        if (result["status"] == 200) {
            QList<QVariant> songs =
                result["body"].toMap()["data"].toMap()["songs"].toList();
            parseSongs(songs);
            styleCursor += songs.size();
        }
    } else { // 热门歌单
        params["limit"] = 1;
        params["offset"] = rand() % 499;
        QVariantMap result = invokeMethod("top_playlist", params);
        if (result["status"] == 200) {
            QList<QVariant> playlists =
                result["body"].toMap()["playlists"].toList();
            if (playlists.size() > 0) {
                QVariantMap playlist = playlists[0].toMap();
                QString id = playlist["id"].toString();
                params.clear();
                params["id"] = id;
                QVariantMap audioResult =
                    invokeMethod("playlist_track_all", params);
                if (audioResult["status"] == 200) {
                    QList<QVariant> songs =
                        audioResult["body"].toMap()["songs"].toList();
                    parseSongs(songs);
                } else {
                    helper->say("网络连接出错了");
                }
            }
        }
    }
}

void NeteaseMusic::likeCurrent(bool like) {
    QVariantMap metaMap = helper->getPlayer()->getCurrentMeta().toMap();
    if (metaMap.contains("type") && metaMap["type"] == "song") {
        qint64 id = metaMap["id"].toLongLong();
        QVariantMap params;
        params["id"] = id;
        params["like"] = like;
        invokeMethod("like", params);
    }
}