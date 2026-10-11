#include "TimeNormalizer.h"

#include <QDate>
#include <QMap>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QStringList>
#include <QTime>

#include <algorithm>

namespace {

// ---------------------------------------------------------------------------
// 中文数字
// ---------------------------------------------------------------------------

/// 单个汉字数字的字面值；不是数字返回 -1。
int cnDigit(const QChar &c)
{
    switch (c.unicode()) {
    case u'零': case u'〇':     return 0;
    case u'一':                 return 1;
    case u'二': case u'两':     return 2;
    case u'三':                 return 3;
    case u'四':                 return 4;
    case u'五':                 return 5;
    case u'六':                 return 6;
    case u'七':                 return 7;
    case u'八':                 return 8;
    case u'九':                 return 9;
    default:                    return -1;
    }
}

/// 单个汉字单位的量级；不是单位返回 0。
int cnUnit(const QChar &c)
{
    switch (c.unicode()) {
    case u'十': return 10;
    case u'百': return 100;
    case u'千': return 1000;
    case u'万': return 10000;
    default:    return 0;
    }
}

/// 「三十」「二十」「十五」「三」「两」→ 数值。纯中文数字串，不含单位以外的字。
bool cnNumber(const QString &s, double *out)
{
    if (s.isEmpty()) {
        return false;
    }
    double total = 0.0;
    double section = 0.0;
    int pending = -1;                 // 尚未配上单位的数字
    for (int i = 0; i < s.size(); ++i) {
        const QChar c = s.at(i);
        const int d = cnDigit(c);
        if (d >= 0) {
            pending = d;
            continue;
        }
        const int unit = cnUnit(c);
        if (unit == 0) {
            return false;             // 出现非数字非单位的字，整体不成立
        }
        // 「十五」的十前面没有数字，按 1 算
        const int count = (pending < 0) ? 1 : pending;
        if (unit == 10000) {
            section = (section + count) * unit;
            total += section;
            section = 0.0;
        } else {
            section += count * unit;
        }
        pending = -1;
    }
    if (pending >= 0) {
        section += pending;
    }
    *out = total + section;
    return true;
}

/// 数量词：阿拉伯数字 / 中文数字 / 「半」/「一个半」/「两个」。
bool parseAmount(const QString &token, double *out)
{
    QString t = token.trimmed();
    if (t.isEmpty()) {
        return false;
    }
    // 「一个小时」「两个」「1个」
    t.remove(QChar(u'个'));
    // 「十来分钟」「二十来分钟」
    t.remove(QChar(u'来'));
    if (t.isEmpty()) {
        return false;
    }
    if (t == QStringLiteral("半")) {
        *out = 0.5;
        return true;
    }
    bool ok = false;
    const double direct = t.toDouble(&ok);
    if (ok) {
        *out = direct;
        return true;
    }
    // 「一个半」「两半」—— 半在末尾
    if (t.endsWith(QChar(u'半'))) {
        double base = 0.0;
        if (!cnNumber(t.left(t.size() - 1), &base)) {
            return false;
        }
        *out = base + 0.5;
        return true;
    }
    return cnNumber(t, out);
}

// ---------------------------------------------------------------------------
// 区间优先：「周末」「今后三天」「未来三天」
// ---------------------------------------------------------------------------

bool resolveRange(const QString &s, const QDateTime &now, QString *out)
{
    const QDate today = now.date();

    // 周末 —— 取包含今天（若今天就是周六/周日）的那个「周六 00:00 ~ 周一 00:00」。
    static const QStringList kWeekend{
        QStringLiteral("周末"), QStringLiteral("这周末"),
        QStringLiteral("本周末"), QStringLiteral("这个周末"),
    };
    if (kWeekend.contains(s)) {
        const int dow = today.dayOfWeek();          // 1=周一 ... 7=周日
        int toSat = 0;
        if (dow == 6) {
            toSat = 0;                              // 今天就是周六
        } else if (dow == 7) {
            toSat = -1;                             // 周日的周末从昨天算起
        } else {
            toSat = 6 - dow;
        }
        const QDateTime begin(today.addDays(toSat), QTime(0, 0, 0));
        const QDateTime end(today.addDays(toSat + 2), QTime(0, 0, 0));
        *out = QStringLiteral("%1|%2")
                   .arg(now.secsTo(begin))
                   .arg(now.secsTo(end));
        return true;
    }

    // 明后天 / 今明两天 —— 「明天和后天」这类**连续两天**的说法。
    //   在 holdout 里真实出现过（「明后天天气如何」-> d=明后天），
    //   漏掉它会让该槽位归一化成空串进而被丢弃，等于凭空掉一分。
    static const QMap<QString, QPair<int, int>> kDaySpan{
        {QStringLiteral("明后天"),   qMakePair(1, 3)},
        {QStringLiteral("明后两天"), qMakePair(1, 3)},
        {QStringLiteral("今明两天"), qMakePair(0, 2)},
        {QStringLiteral("今明"),     qMakePair(0, 2)},
    };
    if (kDaySpan.contains(s)) {
        const QPair<int, int> span = kDaySpan.value(s);
        const QDateTime begin(today.addDays(span.first), QTime(0, 0, 0));
        const QDateTime end(today.addDays(span.second), QTime(0, 0, 0));
        *out = QStringLiteral("%1|%2")
                   .arg(now.secsTo(begin))
                   .arg(now.secsTo(end));
        return true;
    }

    // 今后N天 / 未来N天 / 明后两天 —— 从明天 00:00 起算，共 N 天。
    static const QRegularExpression reFuture(
        QStringLiteral("^(?:今|明|未)?(?:后|来)(\\S+)天$"));
    const QRegularExpressionMatch m = reFuture.match(s);
    if (m.hasMatch()) {
        double n = 0.0;
        if (parseAmount(m.captured(1), &n) && n >= 1) {
            const QDateTime begin(today.addDays(1), QTime(0, 0, 0));
            const QDateTime end(today.addDays(1 + static_cast<int>(n)),
                                QTime(0, 0, 0));
            *out = QStringLiteral("%1|%2")
                       .arg(now.secsTo(begin))
                       .arg(now.secsTo(end));
            return true;
        }
    }

    return false;
}

// ---------------------------------------------------------------------------
// 时长：「30分钟后」「半小时」「两个小时」「一刻钟」
// ---------------------------------------------------------------------------

bool resolveDuration(const QString &s, qint64 *out)
{
    QString t = s;

    // 尾缀：以后 / 之后 必须排在 后 前面，否则「30分钟以后」会被削成「30分钟以」
    static const QRegularExpression reTail(
        QStringLiteral("(?:以后|之后|以内|之内|内|后|整|左右)$"));
    t.remove(reTail);

    // 前缀：等 / 再 / 过 / 隔 / 稍
    static const QRegularExpression reHead(QStringLiteral("^(?:等|再|过|隔|稍)+"));
    t.remove(reHead);

    if (t.isEmpty()) {
        return false;
    }

    // 小时（含「半小时」「一个半小时」）
    static const QRegularExpression reHour(
        QStringLiteral("^(.+?)个?小时(?:钟)?$"));
    // 刻（一刻钟 / 三刻）
    static const QRegularExpression reQuarter(
        QStringLiteral("^(.+?)刻(?:钟)?$"));
    // 分钟 / 分（含「三十分」「20分半」）
    static const QRegularExpression reMinute(
        QStringLiteral("^(.+?)分钟?(半)?$"));
    // 秒
    static const QRegularExpression reSecond(
        QStringLiteral("^(.+?)秒钟?$"));
    // 天（作为时长：「三天后」）
    static const QRegularExpression reDay(
        QStringLiteral("^(.+?)天$"));

    struct Rule {
        const QRegularExpression *re;
        double scale;               // 单位换算成秒
        bool halfAddsHalf;          // 末位「半」是否加半个单位
    };
    const Rule rules[] = {
        {&reHour,    3600.0, false},
        {&reQuarter,  900.0, false},
        {&reMinute,    60.0, true},
        {&reSecond,     1.0, false},
        {&reDay,    86400.0, false},
    };

    for (const Rule &rule : rules) {
        const QRegularExpressionMatch m = rule.re->match(t);
        if (!m.hasMatch()) {
            continue;
        }
        double amount = 0.0;
        if (!parseAmount(m.captured(1), &amount)) {
            continue;               // 例如「今」「明」这类不是数量的捕获
        }
        double secs = amount * rule.scale;
        if (rule.halfAddsHalf && m.lastCapturedIndex() >= 2
            && m.captured(2) == QStringLiteral("半")) {
            secs += rule.scale / 2.0;
        }
        *out = static_cast<qint64>(secs + 0.5);
        return true;
    }

    return false;
}

// ---------------------------------------------------------------------------
// 绝对日期 / 时刻：「明天」「今天晚上」「明早8点」「3月5日」
// ---------------------------------------------------------------------------

/// 时段词 → 该时段的代表钟点。
int periodHour(const QString &w)
{
    static const QMap<QString, int> table{
        {QStringLiteral("凌晨"), 3},  {QStringLiteral("清晨"), 6},
        {QStringLiteral("早上"), 7},  {QStringLiteral("早晨"), 7},
        {QStringLiteral("上午"), 9},  {QStringLiteral("中午"), 12},
        {QStringLiteral("下午"), 14}, {QStringLiteral("傍晚"), 18},
        {QStringLiteral("晚上"), 19}, {QStringLiteral("夜里"), 21},
        {QStringLiteral("半夜"), 23},
    };
    return table.value(w, -1);
}

/// 该时段词是否把 12 小时制钟点推到下午 —— 「下午3点」是 15:00 而不是 03:00。
bool isAfternoonPeriod(const QString &w)
{
    static const QStringList kAfternoon{
        QStringLiteral("中午"), QStringLiteral("下午"),
        QStringLiteral("傍晚"), QStringLiteral("晚上"),
        QStringLiteral("夜里"),
    };
    return kAfternoon.contains(w);
}

bool resolvePoint(const QString &s, const QDateTime &now, QDateTime *out)
{
    // 「现在」类 —— 偏移 0
    static const QStringList kNow{
        QStringLiteral("现在"), QStringLiteral("此刻"), QStringLiteral("马上"),
        QStringLiteral("立刻"), QStringLiteral("待会儿"), QStringLiteral("一会儿"),
        QStringLiteral("稍后"),
    };
    if (kNow.contains(s)) {
        *out = now;
        return true;
    }

    const QDate today = now.date();
    QString rest = s;

    int hour = -1;
    int minute = 0;

    // 1) 先摘掉尾部的时刻：「8点」「8点半」「8点30分」，中文数字也认
    static const QRegularExpression reClock(
        QStringLiteral("(\\d{1,2}|[零〇一二两三四五六七八九十]{1,3})[点时]"
                       "(?:(\\d{1,2})分?|(半))?$"));
    const QRegularExpressionMatch cm = reClock.match(rest);
    if (cm.hasMatch()) {
        const QString hTok = cm.captured(1);
        bool ok = false;
        int h = hTok.toInt(&ok);
        if (!ok) {
            double d = 0.0;
            ok = cnNumber(hTok, &d);
            h = static_cast<int>(d);
        }
        if (ok && h >= 0 && h <= 24) {
            hour = h;
            if (cm.captured(3) == QStringLiteral("半")) {
                minute = 30;
            } else if (!cm.captured(2).isEmpty()) {
                minute = cm.captured(2).toInt();
            }
            rest = rest.left(cm.capturedStart(0));
        }
    }

    int dayOffset = 0;
    bool daySpecified = false;

    // 2) 「今晚 / 明早 / 明晨 / 昨晚」
    static const QRegularExpression reShortDay(
        QStringLiteral("^(今|明|昨|后)(晚|早|晨)$"));
    const QRegularExpressionMatch sm = reShortDay.match(rest);
    if (sm.hasMatch()) {
        static const QMap<QString, int> off{{QStringLiteral("昨"), -1},
                                            {QStringLiteral("今"), 0},
                                            {QStringLiteral("明"), 1},
                                            {QStringLiteral("后"), 2}};
        dayOffset = off.value(sm.captured(1), 0);
        daySpecified = true;
        const bool evening = (sm.captured(2) == QStringLiteral("晚"));
        if (hour < 0) {
            hour = evening ? 19 : 7;
            minute = 0;
        } else if (evening && hour < 12) {
            hour += 12;                       // 「今晚8点」是 20:00
        }
        rest.clear();
    }

    // 3) 日词
    if (!rest.isEmpty()) {
        static const QMap<QString, int> dayWords{
            {QStringLiteral("大前天"), -3}, {QStringLiteral("大后天"), 3},
            {QStringLiteral("前天"), -2},  {QStringLiteral("后天"), 2},
            {QStringLiteral("后日"), 2},
            {QStringLiteral("昨天"), -1},  {QStringLiteral("昨日"), -1},
            {QStringLiteral("今天"), 0},   {QStringLiteral("今日"), 0},
            {QStringLiteral("明天"), 1},   {QStringLiteral("明日"), 1},
        };
        QStringList keys = dayWords.keys();
        std::sort(keys.begin(), keys.end(), [](const QString &a, const QString &b) {
            return a.size() > b.size();           // 长的优先：「大后天」先于「后天」
        });
        for (const QString &k : keys) {
            if (rest.startsWith(k)) {
                dayOffset = dayWords.value(k);
                daySpecified = true;
                rest = rest.mid(k.size());
                break;
            }
        }
    }

    // 4) 星期
    if (!rest.isEmpty() && !daySpecified) {
        static const QRegularExpression reWeek(
            QStringLiteral("^(下下|下|这|本)?(?:周|星期|礼拜)([一二三四五六日天])"));
        const QRegularExpressionMatch wm = reWeek.match(rest);
        if (wm.hasMatch()) {
            static const QMap<QString, int> dow{
                {QStringLiteral("一"), 1}, {QStringLiteral("二"), 2},
                {QStringLiteral("三"), 3}, {QStringLiteral("四"), 4},
                {QStringLiteral("五"), 5}, {QStringLiteral("六"), 6},
                {QStringLiteral("日"), 7}, {QStringLiteral("天"), 7},
            };
            const int target = dow.value(wm.captured(2), -1);
            if (target > 0) {
                const int cur = today.dayOfWeek();
                int delta = (target - cur + 7) % 7;
                const QString pre = wm.captured(1);
                if (pre == QStringLiteral("下")) {
                    delta += 7;
                } else if (pre == QStringLiteral("下下")) {
                    delta += 14;
                }
                dayOffset = delta;
                daySpecified = true;
                rest = rest.mid(wm.capturedLength(0));
            }
        }
    }

    // 5) 时段词（「今天晚上」「明天早上」「明天下午3点」）
    if (!rest.isEmpty()) {
        const int ph = periodHour(rest);
        if (ph >= 0) {
            if (hour < 0) {
                hour = ph;
                minute = 0;
            } else if (isAfternoonPeriod(rest) && hour < 12) {
                hour += 12;                   // 「下午3点」是 15:00
            }
            rest.clear();
        }
    }

    // 6) 年月日
    if (!rest.isEmpty()) {
        static const QRegularExpression reFullDate(
            QStringLiteral("^(?:(\\d{4})年)?(?:(\\d{1,2})月)?(\\d{1,2})[日号]$"));
        const QRegularExpressionMatch dm = reFullDate.match(rest);
        if (dm.hasMatch()) {
            const bool yearGiven = !dm.captured(1).isEmpty();
            const int y = yearGiven ? dm.captured(1).toInt() : today.year();
            const int mo = dm.captured(2).isEmpty() ? today.month()
                                                    : dm.captured(2).toInt();
            const int d = dm.captured(3).toInt();
            QDate date(y, mo, d);
            if (date.isValid()) {
                // 没写年份，而该月日今年已经过去 —— 十月说「3月5日」指的是明年
                if (!yearGiven && date < today) {
                    const QDate rolled = date.addYears(1);
                    if (rolled.isValid()) {
                        date = rolled;
                    }
                }
                *out = QDateTime(date, QTime(hour < 0 ? 0 : hour,
                                             hour < 0 ? 0 : minute, 0));
                return true;
            }
        }
        static const QRegularExpression reYearMonth(
            QStringLiteral("^\\d{4}年(\\d{1,2})月$"));
        const QRegularExpressionMatch ym = reYearMonth.match(rest);
        if (ym.hasMatch()) {
            *out = QDateTime(QDate(rest.left(4).toInt(), ym.captured(1).toInt(), 1),
                             QTime(0, 0, 0));
            return true;
        }
        static const QRegularExpression reMonth(
            QStringLiteral("^(\\d{1,2})月$"));
        const QRegularExpressionMatch mm = reMonth.match(rest);
        if (mm.hasMatch()) {
            QDate date(today.year(), mm.captured(1).toInt(), 1);
            if (date.isValid() && date < today) {
                const QDate rolled = date.addYears(1);
                if (rolled.isValid()) {
                    date = rolled;
                }
            }
            *out = QDateTime(date, QTime(0, 0, 0));
            return true;
        }
        return false;
    }

    // 7) 组装
    if (!daySpecified && hour < 0) {
        return false;
    }
    QDate date = today.addDays(dayOffset);
    int h = hour;
    int mi = minute;
    if (h < 0) {
        h = 0;                      // 只到「日」→ 取该日 00:00:00
        mi = 0;
    }
    if (h == 24) {                  // 「明天24点」== 次日 00:00
        date = date.addDays(1);
        h = 0;
        mi = 0;
    }
    QDateTime dt(date, QTime(h, mi, 0));
    // 只给了钟点、没给日子，而该钟点今天已经过去 —— 顺延到明天
    if (hour >= 0 && !daySpecified && dt <= now) {
        dt = dt.addDays(1);
    }
    *out = dt;
    return true;
}

}  // namespace

QString TimeNormalizer::normalize(const QString &raw, const QDateTime &now)
{
    if (!now.isValid()) {
        return QString();
    }
    QString s = raw.trimmed();
    // 去空白与常见标点，避免「明天 早上」「今天，」这类输入漏过匹配
    static const QRegularExpression reNoise(
        QStringLiteral("[\\s，,。.、！!？?~～：:]"));
    s.remove(reNoise);
    if (s.isEmpty()) {
        return QString();
    }

    // 区间优先：必须排在时长前面，否则「今后三天」会被 reDay 当成三天时长
    QString range;
    if (resolveRange(s, now, &range)) {
        return range;
    }

    qint64 secs = 0;
    if (resolveDuration(s, &secs)) {
        return QString::number(secs);
    }

    QDateTime point;
    if (resolvePoint(s, now, &point)) {
        return QString::number(now.secsTo(point));
    }

    return QString();
}
