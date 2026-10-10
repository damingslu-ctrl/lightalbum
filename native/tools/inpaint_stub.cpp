// 确定性 stub：实现 EraseProto 协议，mask==255 → 灰(128)，其余逐字节拷贝。
// 无模型、无随机、无网络。用于 LA_ERASE_QA 恒绿契约门。非发布产物。
// QA 专用延迟旋钮：LA_ERASE_STUB_DELAY_MS=<毫秒> 在应答前 Sleep（模拟真模型秒级耗时，
// E 组非阻塞判别的注入点）。未设 = 0 延迟，其余门与生产路径零影响。仅测试工具面，
// 不进任何产品类（TDD 规范：测试侧注入放测试侧）。
#include <QCoreApplication>
#include <QIODevice>
#include <QThread>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>   // _O_BINARY（偏差：见下方 setmode 注释）
#include <io.h>      // _setmode/_fileno（MinGW）
#include "core/EraseProto.h"

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const long delayMs = QByteArray(qgetenv("LA_ERASE_STUB_DELAY_MS")).toLong();
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
        if (delayMs > 0) QThread::msleep(delayMs);   // QA：模拟推理耗时（应答前睡，E 组判别窗口）
        const int px = w * h;
        for (int i = 0; i < px && i < msk.size(); ++i) {
            if ((uchar)msk[i] == 255) { res[i*3+0]=128; res[i*3+1]=128; res[i*3+2]=128; }
        }
        fprintf(out, "OK %u %d %d %d\n", id, w, h, res.size());
        // 分块应答（仅 QA 复现装置）：真 Windows 管道本就会把 120KB 应答拆成多块
        // （全量套件里以 short rgb 形态间歇复现）。LA_ERASE_STUB_CHUNK_KB 令桩确定性地
        // 每 K KiB 写一次并 flush，把拆包变成必现，供宿主无关的门测协议健壮性。0/未设=单块。
        const long chunkKb = QByteArray(qgetenv("LA_ERASE_STUB_CHUNK_KB")).toLong();
        const long chunkMs = QByteArray(qgetenv("LA_ERASE_STUB_CHUNK_MS")).toLong();
        if (chunkKb > 0) {
            const int step = int(chunkKb * 1024);
            for (int off = 0; off < res.size(); off += step) {
                const int len = qMin(step, res.size() - off);
                fwrite(res.constData() + off, 1, len, out);
                fflush(out);
                if (chunkMs > 0 && off + step < res.size()) QThread::msleep(chunkMs);
            }
        } else {
            fwrite(res.constData(), 1, res.size(), out);
        }
        fflush(out);
    }
    return 0;
}
