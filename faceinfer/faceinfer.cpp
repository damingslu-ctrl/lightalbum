// faceinfer.exe —— SCRFD 检测 + 对齐嵌入 ONNX 子进程（第四轮 T1 = 工程骨架壳）。
// stdout 只走帧（AiProto 强制 _O_BINARY），日志只走 stderr；T2/T3 在本文件填 ORT 会话
// （SessionOptions 创建在 READY 行之前完成，全建好才发 READY 1，缺模型发 READY 0 后保活）。
// T1 壳语义（controller ruling verbatim）：不建任何 ONNX 会话——默认 `READY 0`；
// 调试 env LA_FACE_SHELL_READY1=1 时改发 `READY 1`（仅供握手自检）；随后逐 REQ 回
// `ERR <id> nomodel`（含 body 必读满后丢弃），坏帧回 `ERR 0 badframe`。
#include "core/AiProto.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <fcntl.h>
#include <io.h>

namespace {

// 剥除行尾 \r\n —— parseReq %n 全耗比对要求 n 为规范化长度（与桩同口径，一行式契约）。
void stripEol(char* line)
{
    size_t n = std::strlen(line);
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
}

bool readExact(FILE* in, char* buf, size_t n)
{
    size_t got = 0;
    while (got < n) {
        const size_t r = std::fread(buf + got, 1, n - got, in);
        if (r == 0) return false;
        got += r;
    }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) {   // 装配自检钩（release C10 腿复用）
            std::printf("faceinfer 0.0.0-shell round4-t1\n");
            return 0;
        }
    }
    _setmode(_fileno(stdin), _O_BINARY);
    AI_PROTO_FORCE_BINARY();
    FILE* in = stdin;
    FILE* out = stdout;

    // T2 落位点：此处创建 ORT env/session（detection.onnx + recognition.onnx，
    // 同目录解析，缺任一 => READY 0 保活语义，spec §5）。T1 壳 = 零会话，恒 READY 0。
    const char* r1 = std::getenv("LA_FACE_SHELL_READY1");
    const bool sayReady1 = r1 && r1[0] == '1';
    std::fprintf(out, "READY %d\n", sayReady1 ? 1 : 0);
    std::fflush(out);

    char line[512];
    while (std::fgets(line, sizeof(line), in)) {
        stripEol(line);
        const size_t n = std::strlen(line);
        aiproto::Header h;
        if (!aiproto::parseReq(line, n, h)) {
            std::fprintf(out, "ERR 0 badframe\n");
            std::fflush(out);
            continue;
        }
        std::string body(static_cast<size_t>(h.len), '\0');
        if (h.len > 0 && !readExact(in, &body[0], static_cast<size_t>(h.len))) break;
        std::fprintf(out, "ERR %llu nomodel\n", h.id);   // T1 壳无模型 = nomodel 恒答
        std::fflush(out);
    }
    return 0;
}
