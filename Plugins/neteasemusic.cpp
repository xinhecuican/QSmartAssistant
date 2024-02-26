#include "neteasemusic.h"
#include "../Utils/config.h"
#include "PluginReflector.h"
#include "../Recorder/player.h"
#include <QJsonArray>
REG_CLASS(NeteaseMusic)

NeteaseMusic::NeteaseMusic(IPluginHelper* helper, QObject* parent)
    :Plugin(helper, parent),
    voiceControl(new VoiceControl(helper, this)){
    QJsonObject neteaseConfig = Config::instance()->getConfig("netease_cloud");
    QVariantMap params;
    params["phone"] = neteaseConfig.value("phone").toString();
    params["password"] = neteaseConfig.value("password").toString();
    cookie = neteaseConfig.value("cookie").toString();
    volumeStep = neteaseConfig.value("volumeStep").toInt();
    QVariantMap result = api.login_status(params);
    login = true;
    if(cookie == "" || (result["status"] != 200)){
        result = api.login_cellphone(params);
        if(result["status"] == 200){
            cookie = result["cookie"].toString();
            Config::instance()->saveConfig("netease_cloud", "cookie", cookie);
        }
        else{
            result = api.register_anonimous(params);
            if(result["status"] == 200){
                cookie = result["cookie"].toString();
                Config::instance()->saveConfig("netease_cloud", "cookie", cookie);
            }
            else{
                qWarning() <<  "netease login fail";
                login = false;
            }
        }
    }
    searchTrigger = {"搜索","找","播放","听","放","来","唱", "再来"};
}

QString NeteaseMusic::getName(){
    return "NeteaseMusic";
}

bool NeteaseMusic::handle(const QString& text,
                          const ParsedIntent& parsedIntent,
                          bool& isImmersive){
    if(!login) return false;
    isSearchTrigger = false;
    for(auto& trigger : searchTrigger){
        if(text.contains(trigger)){
            isSearchTrigger = true;
            break;
        }
    }
    isSearch = false;
    float conf=0;
    for(auto& intent : parsedIntent.intents){
        if(intent.name == "MUSICRANK" ||
            intent.name == "MUSICINFO" ||
            intent.name == "MUSICPROP" ||
            intent.name == "OPEN_MUSIC"){
            isSearch = true;
            conf = intent.conf;
        }
    }
    bool needHandle = isSearchTrigger && isSearch && conf > 0.5;
    if(needHandle || isImmersive){
        if(needHandle && !isImmersive) isImmersive = true;
        doHandle(text, parsedIntent, isImmersive);
    }
    return needHandle;
}

void NeteaseMusic::doHandle(const QString& text,
                            const ParsedIntent& parsedIntent,
                            bool& isImmersive){
    if(text.contains("这首歌")){
        getCurrentTrack();
    }
    else if(parsedIntent.hasIntent("CLOSE_MUSIC") || text.contains("退出")){
        isImmersive = false;
        Player::instance()->stop();
    }
    else if(parsedIntent.hasIntent("PAUSE")){
        Player::instance()->pause();
    }
    else if(parsedIntent.hasIntent("CONTINUE")){
        Player::instance()->resume();
    }
    else if(parsedIntent.hasIntent("CHANGE_VOL")){
        bool immersive=false;
        voiceControl->handle(text, parsedIntent, immersive);
    }
    else if(parsedIntent.hasIntent("CHANGE_TO_NEXT")){
        Player::instance()->next();
    }
    else if(parsedIntent.hasIntent("CHNAGE_TO_LAST")){
        Player::instance()->previous();
    }
    else if(isSearch && isSearchTrigger){
        searchAlbum(parsedIntent);
    }
    else{
        qInfo() << "netease can't understand" << text;
        parsedIntent.toString();
        isImmersive = false;
    }
}

void NeteaseMusic::getCurrentTrack(){
    QVariantMap meta = Player::instance()->getCurrentMeta().toMap();
    if(meta.contains("type") && meta["type"] == "song"){
        qint64 id = meta["id"].toLongLong();
        MusicInfo musicInfo = musicInfoMap[id];
        helper->say("这首歌叫" + musicInfo.name + ".歌手是" + musicInfo.artist);
    }
}

void NeteaseMusic::searchAlbum(const ParsedIntent& parsedIntent){
    QList<QString> singerName, songName, songType;
    if(parsedIntent.hasIntent("MUSICINFO")){
        auto intent = parsedIntent.intents["MUSICINFO"];
        for(auto& slot : intent.intentSlots){
            if(slot.name == "user_music_name"){
                songName.append(slot.value);
            }
            else if(slot.name == "user_singer_name"){
                singerName.append(slot.value);
            }
            else if(slot.name == "user_music_type"){
                songType.append(slot.value);
            }
        }
    }
    else if(parsedIntent.hasIntent("MUSICPROP")){
        auto intent = parsedIntent.intents["MUSICPROP"];
        for(auto& slot : intent.intentSlots){
            if(slot.name == "user_music_name"){
                songName.append(slot.value);
            }
            else if(slot.name == "user_singer_name"){
                singerName.append(slot.value);
            }
        }
    }
    else if(parsedIntent.hasIntent("MUSICRANK")){
        auto intent = parsedIntent.intents["MUSICRANK"];
        for(auto& slot : intent.intentSlots){
            if(slot.name == "user_music_name"){
                songName.append(slot.value);
            }
            else if(slot.name == "user_singer_name"){
                singerName.append(slot.value);
            }
            else if(slot.name == "user_music_type"){
                songType.append(slot.value);
            }
        }
    }
    setPlaylist(singerName, songName);
}

void NeteaseMusic::setPlaylist(const QList<QString>& singerName, const QList<QString>& songName){
    QString searchWord = "";
    bool songValid = songName.size() != 0;
    bool singerValid = singerName.size() != 0;
    if(songValid) searchWord = songName.join(' ');
    if(songValid && singerValid) searchWord.append(' ');
    if(singerValid) searchWord = searchWord.append(singerName.join(' '));
    QVariantMap params;
    params["keywords"] = searchWord;
    params["cookie"] = cookie;
    if(songValid || singerValid){
        QVariantMap result = api.cloudsearch(params);
        if(result["status"] == 200){
            QList<QVariant> songs = result["body"].toMap()["result"].toMap()["songs"].toList();
            if(songs.size() > 0){
                QList<qint64> ids;
                for(int i=0; i<3; i++){
                    QVariantMap song = songs[i].toMap();
                    ids.append(song["id"].toLongLong());
                }
                QList<QString> urls = getAudio(ids);
                for(int i=0; i<urls.size(); i++){
                    if(urls[i] != ""){
                        QVariantMap song = songs.at(i).toMap();
                        MusicInfo musicInfo;
                        musicInfo.id = ids[i];
                        musicInfo.url = urls[i];
                        musicInfo.name = song["name"].toString();
                        musicInfo.artist = getArtist(song);
                        musicInfoMap.insert(ids[i], musicInfo);
                        QVariantMap meta = {
                            {"type", "song"},
                            {"id", musicInfo.id}
                        };
                        Player::instance()->play(musicInfo.url, AudioPlaylist::NORMAL, meta);
                        qDebug() << "play" << musicInfo.name << musicInfo.url;
                        break;
                    }
                }

            }
        }
    }
}

QList<QString> NeteaseMusic::getAudio(QList<qint64> ids){
    QVariantMap params;
    QVariantList idList;
    for(int i=0; i<ids.size(); i++){
        idList.append(ids[i]);
    }
    params["id"] = idList;
    params["level"] = "lossless";
    QVariantMap result = api.song_url_v1(params);
    QList<QString> urls;
    QMap<qint64, QString> idUrlMap;
    QVariantMap body = result["body"].toMap();
    if(body.contains("data")){
        QList<QVariant> songs = body["data"].toList();
        for(auto& songv : songs){
            QVariantMap song = songv.toMap();
            idUrlMap.insert(song["id"].toLongLong(), song["url"].toString());
        }
        for(auto& id : ids){
            urls.append(idUrlMap[id]);
        }
    }
    return urls;
}

QString NeteaseMusic::getArtist(const QVariantMap& song){
    QList<QVariant> artists = song["ar"].toList();
    QString result;
    for(auto& artistv : artists){
        QVariantMap artist = artistv.toMap();
        result += artist["name"].toString() + ",";
    }
    return result;
}
