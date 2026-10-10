// 确定性 stub：实现 EraseProto 协议，mask==255 → 灰(128)，其余逐字节拷贝。
// 无模型、无随机、无网络。用于 LA_ERASE_QA 恒绿契约门。非发布产物。
#include <QCoreApplication>
#include <QIODevice>
#include <cstdio>
#include <fcntl.h>   // _O_BINARY（偏差：见下方 setmode 注释）
#include <io.h>      // _setmode/_fileno（MinGW）
#include "core/EraseProto.h"

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    // 偏差（正确性必需，非 brief 原文）：Windows CRT 的 stdin/stdout 默认文本模式，
    // 输出侧会把每个 0x0A 扩大为 0D0A（实测：RGB 体 0a00... 播出 0d0a00...，
    // 长度超出 OK 头承诺的 no，EraseProto::readResponse 必坏）；输入侧 CRLF 也会被改写。
    // 强制二进制模式保证帧字节精确。
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    FILE* in = stdin;
    FILE* out = stdout;
    fprintf(out, "READY 1\n"); fflush(out);
    char hdr[256];
    while (fgets(hdr, sizeof(hdr), in)) {
        unsigned id; int w, h, nb, nm;
        if (sscanf(hdr, "TASK %u %d %d %d %d", &id, &w, &h, &nb, &nm) != 5) break;
        QByteArray rgb(nb, '\0');         fread(rgb.data(), 1, nb, in);
        QByteArray msk(nm, '\0');         fread(msk.data(), 1, nm, in);
        QByteArray res = rgb;             // mask 外逐字节不变
        const int px = w * h;
        for (int i = 0; i < px && i < msk.size(); ++i) {
            if ((uchar)msk[i] == 255) { res[i*3+0]=128; res[i*3+1]=128; res[i*3+2]=128; }
        }
        fprintf(out, "OK %u %d %d %d\n", id, w, h, res.size());
        fwrite(res.constData(), 1, res.size(), out);
        fflush(out);
    }
    return 0;
}
