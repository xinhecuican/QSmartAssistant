#ifndef QSA_QRCODE_H
#define QSA_QRCODE_H

#include <QByteArray>
#include <QString>
#include <QVector>

// ---------------------------------------------------------------------------
// 极简 QR 码生成器（只做字节模式 / 纠错等级 L / 版本 1..5）
// ---------------------------------------------------------------------------
// 网易云官方开放平台的扫码登录会返回一个很短的链接（形如
// https://163cn.tv/xxxxxxx，约 25 字节），把二维码渲染出来给用户扫是最
// 自然的交互方式。为了不引入第三方依赖，这里手写了一个最小实现：
//
//   * 只支持字节模式（UTF-8 字节串），这正是 URL 需要的模式；
//   * 只支持纠错等级 L（二维码约 7% 冗余，屏幕显示足够）；
//   * 版本按容量自适应（1..5，字节容量 17/32/53/78/106），
//     这五个版本在纠错等级 L 下都只有 **一个** RS 块，无需交织；
//   * 掩码 0..7 全算一遍，按规范的四条罚分规则取最优。
//
// 该实现已与独立参考实现逐位对齐验证：
//   * Reed-Solomon 用 thonky.com 的经典 v1-M「HELLO WORLD」向量校验；
//   * 三个版本的完整矩阵（含掩码选择）与 node `qrcode` 包输出逐位一致；
//   * 生成的图形可被独立解码器 jsQR 正确还原原文。
namespace qrcode {

struct Bitmap {
    int size = 0;
    // size*size 个模块，行优先，取值 0/1。
    QVector<quint8> modules;

    bool isEmpty() const { return size <= 0 || modules.size() != size * size; }
    inline bool at(int row, int col) const {
        return modules.at(row * size + col) != 0;
    }
};

// 用字节模式编码 payload（UTF-8 字节）。超出全部支持版本的容量时返回空。
Bitmap encode(const QByteArray &payload);

// 渲染成文本：深色模块用 "██"、浅色用两个空格，便于写进日志或文本文件。
QString toAscii(const Bitmap &bitmap, int quietZone = 2);

// 渲染成 PNG 落盘（scale 为每个模块的像素边长）。成功返回 true。
bool savePng(const Bitmap &bitmap, const QString &path, int scale = 8,
             int quietZone = 4);

} // namespace qrcode

#endif // QSA_QRCODE_H
