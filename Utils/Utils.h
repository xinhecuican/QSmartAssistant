#ifndef UTILS_H
#define UTILS_H
#include <stdint.h>
#include <QString>
#include <QMap>

static int16_t float2int16(float f){
    if(f < -0.999999f){
        return INT16_MIN;
    }
    else if(f > 0.999999f){
        return INT16_MAX;
    }
    else{
        return static_cast<int16_t>(f*32767.0f);
    }
}

static qint64 chineseToNum(const QString &chinese)
{
    bool success = false;
    int res = chinese.toInt(&success);
    if(success){
        return res;
    }
    static const QMap<QString, qint64> chineseChar {
                                                   {u8"零", 0},
                                                   {u8"一", 1},
                                                   {u8"二", 2},
                                                   {u8"三", 3},
                                                   {u8"四", 4},
                                                   {u8"五", 5},
                                                   {u8"六", 6},
                                                   {u8"七", 7},
                                                   {u8"八", 8},
                                                   {u8"九", 9},
                                                   {u8"壹", 1},
                                                   {u8"贰", 2},
                                                   {u8"叁", 3},
                                                   {u8"肆", 4},
                                                   {u8"伍", 5},
                                                   {u8"陆", 6},
                                                   {u8"柒", 7},
                                                   {u8"捌", 8},
                                                   {u8"玖", 9},
                                                   };
    static const QMap<QString, qint64> chineseUnit {
                                                   {u8"拾", 10},
                                                   {u8"佰", 100},
                                                   {u8"仟", 1000},
                                                   {u8"萬", 10000},
                                                   {u8"十", 10},
                                                   {u8"百", 100},
                                                   {u8"千", 1000},
                                                   {u8"万", 10000},
                                                   {u8"亿", 100000000},
                                                   };
    qint64 ans = 0;
    qint64 firstUnit = 1;
    qint64 secondUnit = 1;
    qint64 tempUnit = 1;
    QChar ch;
    QString str = chinese;
    int pos = chinese.indexOf("之");
    if(pos != -1){
        str = chinese.mid(pos+1);
    }
    if (str.length() == 0) {
        return 0;
    } else if (str.length() == 1) {
        ch = str.at(0);
        if (chineseUnit.contains(ch)) {
            return chineseUnit.value(ch);
        } else if (chineseChar.contains(ch)) {
            return chineseChar.value(ch);
        } else {
            return 0;
        }
    }
    for(int i = str.length() - 1; i > -1; --i) {
        ch = str.at(i);
        tempUnit = chineseUnit.contains(ch) ? chineseUnit.value(ch) : 1;
        if (tempUnit > firstUnit) {
            firstUnit = tempUnit;
            secondUnit = 1;
            continue;
        } else if (tempUnit > secondUnit) {
            secondUnit = tempUnit;
            continue;
        }
        ans += firstUnit * secondUnit * (chineseChar.contains(ch) ? chineseChar.value(ch) : -1);
    }
    return ans;
}
#endif // UTILS_H
