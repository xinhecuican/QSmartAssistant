#include "qrcode.h"

#include <QColor>
#include <QImage>
#include <QtGlobal>

#include <cmath>

namespace qrcode {
namespace {

// ---------------------------------------------------------------------------
// GF(256)
// ---------------------------------------------------------------------------
struct Gf256 {
    quint8 exp[512];
    quint8 log[256];

    Gf256() {
        int x = 1;
        for (int i = 0; i < 255; ++i) {
            exp[i] = static_cast<quint8>(x);
            log[x] = static_cast<quint8>(i);
            x <<= 1;
            if (x & 0x100) {
                x ^= 0x11d; // QR 规范指定的本原多项式 x^8+x^4+x^3+x^2+1
            }
        }
        for (int i = 255; i < 512; ++i) {
            exp[i] = exp[i - 255]; // 扩表，省掉乘法的取模
        }
        log[0] = 0;
    }

    quint8 mul(quint8 a, quint8 b) const {
        if (a == 0 || b == 0) {
            return 0;
        }
        return exp[log[a] + log[b]];
    }
};

const Gf256 &gf() {
    static const Gf256 table;
    return table;
}

// 生成多项式 g(x) 的系数：高次在前，去掉首项 1（与常见实现口径一致）。
// 构建过程中数组是大端序（[0] 为最高次项），因此还原时直接丢掉首元素。
QVector<quint8> rsCoefficients(int degree) {
    QVector<quint8> gen;
    gen.append(1);
    for (int i = 0; i < degree; ++i) {
        QVector<quint8> next(gen.size() + 1, 0);
        for (int j = 0; j < gen.size(); ++j) {
            next[j] ^= gen[j];                              // 乘 x
            next[j + 1] ^= gf().mul(gen[j], gf().exp[i]);   // 乘 a^i
        }
        gen = next;
    }
    QVector<quint8> coeffs;
    coeffs.reserve(degree);
    for (int i = 1; i < gen.size(); ++i) {
        coeffs.append(gen[i]);
    }
    return coeffs;
}

// Reed-Solomon 校验码字（系统码，余数法）。
QVector<quint8> rsRemainder(const QVector<quint8> &data, int degree) {
    const QVector<quint8> coeffs = rsCoefficients(degree);
    QVector<quint8> result(degree, 0);
    for (int k = 0; k < data.size(); ++k) {
        const quint8 factor = data.at(k) ^ result.at(0);
        for (int i = 0; i + 1 < degree; ++i) {
            result[i] = result.at(i + 1);
        }
        result[degree - 1] = 0;
        for (int i = 0; i < degree; ++i) {
            result[i] = static_cast<quint8>(result.at(i) ^
                                            gf().mul(coeffs.at(i), factor));
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// 版本参数（纠错等级 L）
// ---------------------------------------------------------------------------
struct VersionSpec {
    int dataCodewords;
    int eccCodewords;
};

// 版本 1..5 在纠错等级 L 下都只有一个 RS 块，因此不需要交织。
const VersionSpec kSpecs[] = {
    {19, 7},    // v1
    {34, 10},   // v2
    {55, 15},   // v3
    {80, 20},   // v4
    {108, 26},  // v5
};
const int kMaxVersion = 5;

QVector<int> alignmentCenters(int version) {
    switch (version) {
    case 2:
        return {6, 18};
    case 3:
        return {6, 22};
    case 4:
        return {6, 26};
    case 5:
        return {6, 30};
    default:
        return {}; // v1 没有校正图案
    }
}

int chooseVersion(int payloadSize) {
    for (int v = 1; v <= kMaxVersion; ++v) {
        // 4 位模式指示 + 8 位字符计数（版本 1..9 字节模式固定 8 位）
        if (4 + 8 + payloadSize * 8 <= kSpecs[v - 1].dataCodewords * 8) {
            return v;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 位流
// ---------------------------------------------------------------------------
struct BitBuffer {
    QVector<quint8> bits;

    void put(quint32 value, int length) {
        for (int i = length - 1; i >= 0; --i) {
            bits.append(static_cast<quint8>((value >> i) & 1u));
        }
    }
    int size() const { return bits.size(); }
};

QVector<quint8> buildCodewords(const QByteArray &payload, int version) {
    const VersionSpec spec = kSpecs[version - 1];
    const int capacityBits = spec.dataCodewords * 8;

    BitBuffer bb;
    bb.put(0x4, 4); // 0100 = 字节模式
    bb.put(static_cast<quint32>(payload.size()), 8);
    for (int i = 0; i < payload.size(); ++i) {
        bb.put(static_cast<quint8>(payload.at(i)), 8);
    }

    const int terminator = qMin(4, capacityBits - bb.size());
    if (terminator > 0) {
        bb.put(0, terminator);
    }
    while (bb.size() % 8 != 0) {
        bb.bits.append(0);
    }
    // 交替填充码字 0xEC / 0x11
    int k = 0;
    while (bb.size() < capacityBits) {
        bb.put(k % 2 == 0 ? 0xECu : 0x11u, 8);
        ++k;
    }

    QVector<quint8> data;
    data.reserve(spec.dataCodewords);
    for (int i = 0; i < capacityBits; i += 8) {
        quint8 byte = 0;
        for (int b = 0; b < 8; ++b) {
            byte = static_cast<quint8>((byte << 1) | bb.bits.at(i + b));
        }
        data.append(byte);
    }

    QVector<quint8> out = data;
    out += rsRemainder(data, spec.eccCodewords);
    return out;
}

// ---------------------------------------------------------------------------
// 矩阵
// ---------------------------------------------------------------------------
struct Matrix {
    int size = 0;
    QVector<quint8> value; // 模块值
    QVector<quint8> func;  // 功能模块标记：掩码只作用于非功能模块

    explicit Matrix(int n) : size(n), value(n * n, 0), func(n * n, 0) {}

    inline void setFunc(int x, int y, bool dark) {
        value[y * size + x] = dark ? 1 : 0;
        func[y * size + x] = 1;
    }
    inline bool isFunc(int x, int y) const { return func.at(y * size + x) != 0; }
};

// 掩码条件。注意规范里的 (i, j) 是 (行, 列)。
inline bool maskCondition(int mask, int i, int j) {
    switch (mask) {
    case 0:
        return (i + j) % 2 == 0;
    case 1:
        return i % 2 == 0;
    case 2:
        return j % 3 == 0;
    case 3:
        return (i + j) % 3 == 0;
    case 4:
        return (i / 2 + j / 3) % 2 == 0;
    case 5:
        return (i * j) % 2 + (i * j) % 3 == 0;
    case 6:
        return ((i * j) % 2 + (i * j) % 3) % 2 == 0;
    case 7:
        return ((i + j) % 2 + (i * j) % 3) % 2 == 0;
    default:
        return false;
    }
}

void drawFinder(Matrix &m, int cx, int cy) {
    for (int dy = -4; dy <= 4; ++dy) {
        for (int dx = -4; dx <= 4; ++dx) {
            const int x = cx + dx;
            const int y = cy + dy;
            if (x < 0 || x >= m.size || y < 0 || y >= m.size) {
                continue;
            }
            const int d = qMax(qAbs(dx), qAbs(dy));
            // d==3 是外圈，d<=1 是中心方块；d==2 为白环，d==4 为分隔符。
            m.setFunc(x, y, d != 2 && d != 4);
        }
    }
}

void drawFunctionPatterns(Matrix &m, int version) {
    const int n = m.size;
    drawFinder(m, 3, 3);
    drawFinder(m, n - 4, 3);
    drawFinder(m, 3, n - 4);

    // 定位（timing）图案
    for (int i = 8; i < n - 8; ++i) {
        m.setFunc(i, 6, i % 2 == 0);
        m.setFunc(6, i, i % 2 == 0);
    }

    // 校正图案（与三个定位图案重叠的位置要跳过）
    const QVector<int> centers = alignmentCenters(version);
    for (int ci = 0; ci < centers.size(); ++ci) {
        for (int cj = 0; cj < centers.size(); ++cj) {
            const int cx = centers.at(ci);
            const int cy = centers.at(cj);
            if ((cx == 6 && cy == 6) || (cx == 6 && cy == n - 7) ||
                (cx == n - 7 && cy == 6)) {
                continue;
            }
            for (int dy = -2; dy <= 2; ++dy) {
                for (int dx = -2; dx <= 2; ++dx) {
                    m.setFunc(cx + dx, cy + dy, qMax(qAbs(dx), qAbs(dy)) != 1);
                }
            }
        }
    }

    // 预留格式信息区。(6,8)/(8,6) 属于定位图案，不能被覆盖成浅色。
    for (int i = 0; i <= 8; ++i) {
        if (i == 6) {
            continue;
        }
        m.setFunc(8, i, false);
        m.setFunc(i, 8, false);
    }
    for (int i = 0; i < 8; ++i) {
        m.setFunc(8, n - 1 - i, false);
        m.setFunc(n - 1 - i, 8, false);
    }
    // 固定的暗模块
    m.setFunc(8, n - 8, true);
}

void placeData(Matrix &m, const QVector<quint8> &codewords) {
    const int n = m.size;
    QVector<quint8> bits;
    bits.reserve(codewords.size() * 8);
    for (int i = 0; i < codewords.size(); ++i) {
        const quint8 cw = codewords.at(i);
        for (int b = 7; b >= 0; --b) {
            bits.append(static_cast<quint8>((cw >> b) & 1u));
        }
    }

    int idx = 0;
    bool upward = true;
    // 从右侧两列一组向左推进，组内按上下往返的蛇形顺序填。
    for (int col = n - 1; col > 0; col -= 2) {
        if (col == 6) {
            --col; // 跳过竖直定位图案所在列
        }
        for (int i = 0; i < n; ++i) {
            const int row = upward ? n - 1 - i : i;
            for (int c = 0; c < 2; ++c) {
                const int x = col - c;
                if (m.isFunc(x, row)) {
                    continue;
                }
                m.value[row * n + x] =
                    idx < bits.size() ? bits.at(idx) : static_cast<quint8>(0);
                ++idx;
            }
        }
        upward = !upward;
    }
}

void applyMask(Matrix &m, int mask) {
    const int n = m.size;
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            if (!m.isFunc(x, y) && maskCondition(mask, y, x)) {
                m.value[y * n + x] ^= 1;
            }
        }
    }
}

void drawFormat(Matrix &m, int mask) {
    const int n = m.size;
    const int eccBits = 0x1; // 纠错等级 L = 01
    const int data = (eccBits << 3) | mask;
    int rem = data;
    for (int i = 0; i < 10; ++i) {
        rem = (rem << 1) ^ ((rem >> 9) * 0x537);
    }
    const int bits = ((data << 10) | rem) ^ 0x5412;

    for (int i = 0; i < 15; ++i) {
        const quint8 mod = static_cast<quint8>((bits >> i) & 1);
        // 竖直方向（第 8 列）
        if (i < 6) {
            m.value[i * n + 8] = mod;
        } else if (i < 8) {
            m.value[(i + 1) * n + 8] = mod;
        } else {
            m.value[(n - 15 + i) * n + 8] = mod;
        }
        // 水平方向（第 8 行）
        if (i < 8) {
            m.value[8 * n + (n - i - 1)] = mod;
        } else if (i < 9) {
            m.value[8 * n + 7] = mod;
        } else {
            m.value[8 * n + (14 - i)] = mod;
        }
    }
    m.value[(n - 8) * n + 8] = 1; // 固定暗模块
}

// 规范里的四条罚分规则（N1=3、N2=3、N3=40、N4=10）。
int penalty(const Matrix &m) {
    const int n = m.size;
    int score = 0;

    // N1：行/列上连续同色 >= 5 的段
    for (int line = 0; line < n; ++line) {
        for (int isRow = 0; isRow < 2; ++isRow) {
            int run = 1;
            quint8 prev = isRow ? m.value.at(line * n) : m.value.at(line);
            for (int k = 1; k < n; ++k) {
                const quint8 cur =
                    isRow ? m.value.at(line * n + k) : m.value.at(k * n + line);
                if (cur == prev) {
                    ++run;
                } else {
                    if (run >= 5) {
                        score += 3 + (run - 5);
                    }
                    run = 1;
                    prev = cur;
                }
            }
            if (run >= 5) {
                score += 3 + (run - 5);
            }
        }
    }

    // N2：2x2 同色块
    for (int y = 0; y + 1 < n; ++y) {
        for (int x = 0; x + 1 < n; ++x) {
            const quint8 v = m.value.at(y * n + x);
            if (v == m.value.at(y * n + x + 1) &&
                v == m.value.at((y + 1) * n + x) &&
                v == m.value.at((y + 1) * n + x + 1)) {
                score += 3;
            }
        }
    }

    // N3：出现 1:1:3:1:1（两侧留 4 个浅色）的类定位图案
    static const quint8 patA[11] = {1, 0, 1, 1, 1, 0, 1, 0, 0, 0, 0};
    static const quint8 patB[11] = {0, 0, 0, 0, 1, 0, 1, 1, 1, 0, 1};
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x + 11 <= n; ++x) {
            bool a = true;
            bool b = true;
            for (int k = 0; k < 11; ++k) {
                const quint8 v = m.value.at(y * n + x + k);
                if (v != patA[k]) {
                    a = false;
                }
                if (v != patB[k]) {
                    b = false;
                }
            }
            if (a || b) {
                score += 40;
            }
        }
    }
    for (int x = 0; x < n; ++x) {
        for (int y = 0; y + 11 <= n; ++y) {
            bool a = true;
            bool b = true;
            for (int k = 0; k < 11; ++k) {
                const quint8 v = m.value.at((y + k) * n + x);
                if (v != patA[k]) {
                    a = false;
                }
                if (v != patB[k]) {
                    b = false;
                }
            }
            if (a || b) {
                score += 40;
            }
        }
    }

    // N4：暗色比例偏离 50% 的程度（以 5% 为步长）
    int dark = 0;
    for (int i = 0; i < m.value.size(); ++i) {
        dark += m.value.at(i);
    }
    const int total = n * n;
    const double percent = dark * 100.0 / total;
    const int k = qAbs(static_cast<int>(std::ceil(percent / 5.0)) - 10);
    score += 10 * k;

    return score;
}

Matrix render(int version, int mask, const QVector<quint8> &codewords) {
    Matrix m(version * 4 + 17);
    drawFunctionPatterns(m, version);
    placeData(m, codewords);
    applyMask(m, mask);
    drawFormat(m, mask);
    return m;
}

} // namespace

Bitmap encode(const QByteArray &payload) {
    Bitmap bitmap;
    const int version = chooseVersion(payload.size());
    if (version == 0) {
        return bitmap; // 超出容量
    }
    const QVector<quint8> codewords = buildCodewords(payload, version);

    int bestScore = -1;
    Matrix best(0);
    for (int mask = 0; mask < 8; ++mask) {
        Matrix m = render(version, mask, codewords);
        const int score = penalty(m);
        if (bestScore < 0 || score < bestScore) {
            bestScore = score;
            best = m;
        }
    }

    bitmap.size = best.size;
    bitmap.modules = best.value;
    return bitmap;
}

QString toAscii(const Bitmap &bitmap, int quietZone) {
    if (bitmap.isEmpty()) {
        return QString();
    }
    const int n = bitmap.size;
    const QString dark = QStringLiteral("\u2588\u2588");
    const QString light = QStringLiteral("  ");
    QString out;
    for (int y = -quietZone; y < n + quietZone; ++y) {
        for (int x = -quietZone; x < n + quietZone; ++x) {
            const bool inside = x >= 0 && x < n && y >= 0 && y < n;
            out += (inside && bitmap.at(y, x)) ? dark : light;
        }
        out += QLatin1Char('\n');
    }
    return out;
}

bool savePng(const Bitmap &bitmap, const QString &path, int scale,
             int quietZone) {
    if (bitmap.isEmpty() || scale <= 0) {
        return false;
    }
    const int n = bitmap.size;
    const int side = (n + 2 * quietZone) * scale;
    QImage image(side, side, QImage::Format_RGB32);
    image.fill(QColor(255, 255, 255));

    const QRgb black = qRgb(0, 0, 0);
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            if (!bitmap.at(y, x)) {
                continue;
            }
            for (int dy = 0; dy < scale; ++dy) {
                QRgb *line = reinterpret_cast<QRgb *>(
                    image.scanLine((y + quietZone) * scale + dy));
                for (int dx = 0; dx < scale; ++dx) {
                    line[(x + quietZone) * scale + dx] = black;
                }
            }
        }
    }
    return image.save(path, "PNG");
}

} // namespace qrcode
