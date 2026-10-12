#ifndef QSA_NETEASEMUSIC_CAPTCHAFLOW_H
#define QSA_NETEASEMUSIC_CAPTCHAFLOW_H

#include <QRegularExpression>
#include <QString>
#include <QVariant>

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
 * 除了问答本身的判定，这里还负责**业务码判定**（businessCode / isSuccess）：
 * 「验证码到底发出去了没有」「验证码登录到底成没成」只能由它回答，见下面
 * businessCode 的说明。
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

/**
 * 手机号脱敏：只留前 3 位与后 4 位，中间打码。
 *
 * 用于「验证码到底发到哪个号上了」这类排障日志 —— 既要能核对，又不能把完整
 * 号码写进日志文件（同 password / cookie / captcha 的脱敏原则）。
 * 位数不足 7 位时一律返回 "***"：宁可看不出，也不漏出去。
 */
inline QString maskPhone(const QString &phone) {
    const QString trimmed = phone.trimmed();
    if (trimmed.size() < 7) {
        return QStringLiteral("***");
    }
    return trimmed.left(3) + QStringLiteral("****") + trimmed.right(4);
}

/// 把 QVariant 当整数读。先按数字读，再退化成字符串（网易在部分接口上
/// 把 code 返回成字符串，例如 /register/anonimous 的 "400"）。
inline bool asInt(const QVariant &value, int *out) {
    bool ok = false;
    const int direct = value.toInt(&ok);
    if (ok) {
        *out = direct;
        return true;
    }
    const int text = value.toString().toInt(&ok);
    if (ok) {
        *out = text;
        return true;
    }
    return false;
}

/**
 * 从 NeteaseMusic::invokeMethod() 的返回值里取出**业务码**，并告知调用方
 * 「这次响应里到底有没有业务码」。
 *
 * invokeMethod 会把两种形状都归一化成 {status, body}：
 *   A. 顶层就是网易的业务响应 —— {"code":200,"cookie":"…"}
 *      → status 已经是业务码，body 是整个响应；
 *   B. {status, body} 信封（api-enhanced 对部分路由原样转发上游响应）——
 *      {"status":200,"body":{"code":400,"msg":"…"}}
 *      → **status 只是外层 HTTP 码**，业务码在 body.code 里。
 *
 * ⚠ 只认 status 会把「HTTP 200 + 业务 400」读成成功。放在验证码流程上后果很具体：
 *   短信压根没发出去，助手却先念「验证码已发送到您的手机」，用户对着手机空等
 *   一整轮问答，最后听到的是「没有收到有效的验证码」（用户实测反馈）。
 *
 * @param known 非空时写出是否真的读到了业务码。**验证码/登录这类判定必须看它**：
 *              响应里没有 code 字段时返回的 200 只是沿用 invokeMethod 的兜底，
 *              不能当成成功 —— 宁可不做，也不能嘴上说做成了。
 */
inline int businessCode(const QVariantMap &reply, bool *known = nullptr) {
    if (known != nullptr) {
        *known = false;
    }
    // body 优先：形状 A 下 body 就是整个响应（code 值相同）；形状 B 下
    // 只有 body 里才有真正的业务码。
    const QVariantMap body = reply.value(QStringLiteral("body")).toMap();
    const QVariantMap candidates[2] = {body, reply};
    for (const QVariantMap &candidate : candidates) {
        const auto it = candidate.constFind(QStringLiteral("code"));
        if (it == candidate.constEnd()) {
            continue;
        }
        int code = 0;
        if (asInt(*it, &code)) {
            if (known != nullptr) {
                *known = true;
            }
            return code;
        }
    }
    int status = 0;
    return asInt(reply.value(QStringLiteral("status")), &status) ? status : 200;
}

/// 业务码是否**明确**为 200。没有业务码时一律算失败。
inline bool isSuccess(const QVariantMap &reply) {
    bool known = false;
    const int code = businessCode(reply, &known);
    return known && code == 200;
}

/**
 * 校验验证码（`/captcha/verify`）的判定结果。
 *
 * ⚠⚠ **必须和「登录」分开判**：同一个业务码 503 在两个接口里含义完全不同 ——
 *   `/login/cellphone` 的 503 是「需要短信验证码」，而 `/captcha/verify` 的 503
 *   是「验证码本身不对」（实测原文：`{"message":"验证码错误","code":503,"data":false}`）。
 *   所以本函数只认两种确定情形，其余一律 Unknown，让调用方**保持原行为**
 *   （直接去试登录）：读不懂响应时把用户的验证码判成错的、让他反复重说，
 *   比多打一次登录接口更糟。
 */
enum class VerifyOutcome {
    Ok,        ///< 业务码 200：验证码正确
    WrongCode, ///< 业务码 503 或 message 提到「验证码」：码不对，可以让用户重说
    Unknown,   ///< 说不清（超时/风控/无 code/其它业务码）—— 不要阻断流程
};

/// 依据 `/captcha/verify` 的响应判定验证码对不对。
inline VerifyOutcome classifyVerify(const QVariantMap &reply) {
    const QVariantMap body = reply.value(QStringLiteral("body")).toMap();
    bool known = false;
    const int code = businessCode(reply, &known);
    if (!known) {
        // 没有业务码：HTTP 层失败、超时、或后端换了形状 —— 一律不表态。
        return VerifyOutcome::Unknown;
    }
    if (code == 200) {
        return VerifyOutcome::Ok;
    }
    // message 是网易自己的原话（"验证码错误"），比单看码更稳。⚠ 它可能在外层
    // （形状 A：顶层就是业务响应）也可能在 body 里（形状 B：{status, body} 信封），
    // 两边都要看 —— 只听 body 会在形状 A 上漏判（单测抓到过）。
    const QVariantMap candidates[2] = {body, reply};
    for (const QVariantMap &candidate : candidates) {
        const QString message =
            candidate.value(QStringLiteral("message")).toString() +
            candidate.value(QStringLiteral("msg")).toString();
        if (message.contains(QStringLiteral("验证码"))) {
            return VerifyOutcome::WrongCode;
        }
    }
    return code == 503 ? VerifyOutcome::WrongCode : VerifyOutcome::Unknown;
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
