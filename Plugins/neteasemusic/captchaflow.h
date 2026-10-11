#ifndef QSA_NETEASEMUSIC_CAPTCHAFLOW_H
#define QSA_NETEASEMUSIC_CAPTCHAFLOW_H

#include <QRegularExpression>
#include <QString>

/**
 * 网易云短信验证码「语音获取」流程里所有可以脱离插件环境单独测试的纯逻辑。
 *
 * 为什么要单独开一个头文件：neteasemusic 插件本体要链接 QtNetwork /
 * QProcess / 整个 node 后端，单测既没法也不该把它拉进来；而这里只有字符串
 * 处理，任何 QtCore 目标都能直接断言。回归目标见 Test/tst_sherpa.h 的
 * neteaseCaptcha 用例 ——
 *
 *   ⚠ 「用户没说话（空回答）」必须与「说了但格式不对」区分开。
 *
 * 历史实现（2026-10 用户实测反馈）把两者混为一谈：用户一句话没说，却连续听到
 * 多次「验证码格式不正确，请重新说出您的短信验证码」。空回答的成因有两种，
 * 都与「格式错误」无关：
 *   1. 本地麦克风：VAD 应答窗口（wakeup.responseSlient）到期，ASR 拿到空输入；
 *   2. 网页/HTTP 请求：Conversation::question() 没有录音通道，只能把问题当
 *      文本回给请求方并立刻返回空串。
 */
namespace qsa {
namespace captcha {

/**
 * 把 ASR 文本归一化成纯数字。
 * 处理中文数字与口语说法（「幺」=1、「两」=2），其余字符全部丢弃。
 * 用字符串索引而不是 QMap 初始化列表（MSVC 下 QChar 字面量不匹配）。
 */
inline QString normalize(const QString &text) {
    static const QString cnKeys = QStringLiteral("零〇一幺二两三四五六七八九");
    static const QString cnVals = QStringLiteral("0011223456789");

    QString out;
    out.reserve(text.size());
    for (const QChar &ch : text) {
        if (ch.isDigit()) {
            out.append(ch);
        } else {
            const int idx = cnKeys.indexOf(ch);
            if (idx >= 0) {
                out.append(cnVals.at(idx));
            }
        }
    }
    return out;
}

/// 网易云短信验证码格式：4 或 6 位纯数字。
inline bool formatValid(const QString &code) {
    return (code.size() == 4 || code.size() == 6) &&
           !code.contains(QRegularExpression(QStringLiteral("[^0-9]")));
}

/// 一轮问答结束后该怎么走。
enum class Step {
    Accept,      ///< 拿到合法验证码，可以拿去登录
    RetryFormat, ///< 用户说话了，但不是 4/6 位数字 -> 提示格式错误后重问
    RetrySilent, ///< 没听到任何回答 -> 只重问，**不报格式错误、不消耗重试次数**
    GiveUp,      ///< 放弃，落到游客（匿名）登录
};

/**
 * 依据一轮问答的结果决定下一步。
 *
 * @param answer       一次问答的返回文本（空串/纯空白代表「没听到」）
 * @param attemptsLeft 还剩几次「格式错误重问」额度
 * @param silentLeft   还剩几次「静默重问」额度
 */
inline Step classifyAnswer(const QString &answer, int attemptsLeft,
                           int silentLeft) {
    if (formatValid(normalize(answer))) {
        return Step::Accept;
    }
    if (answer.trimmed().isEmpty()) {
        // ⚠ 顺序很重要：空回答必须走 RetrySilent，而不是落到下面的格式错误。
        return silentLeft > 0 ? Step::RetrySilent : Step::GiveUp;
    }
    return attemptsLeft > 0 ? Step::RetryFormat : Step::GiveUp;
}

} // namespace captcha
} // namespace qsa

#endif // QSA_NETEASEMUSIC_CAPTCHAFLOW_H
