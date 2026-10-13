// faceinfer_stub.exe —— AiProto 线协议确定性桩（LA_FACE_AI_QA #75 契约门专用，非发布产物）。
// 模式 = inpaint_stub 先例：无模型、无随机、无网络；失败矩阵由 LA_FACE_FORCE_* env 钩子驱动，
// QA 期望表固定不随桩分支（铁律22）。stdout 只走帧（强制 _O_BINARY），日志只走 stderr。
// 钩子表（E1 组，spec §5 / task-1-brief Step 2）：
//   LA_FACE_STUB=1            —— runner 侧强制选桩标记（T4 接线消费；桩本身无行为差）。
//   LA_FACE_FORCE_NOMODEL=1   —— READY 0 起步；每 REQ 回 ERR <id> nomodel（保活不退出）。
//   LA_FACE_FORCE_ERR=1       —— READY 1；每 REQ 回 ERR <id> loadfail。
//   LA_FACE_FORCE_HANG=1      —— READY 1；收到 REQ 后睡 90s 不回帧（G6 超时+重启，T4 断言）。
//   LA_FACE_FORCE_SLOW=1      —— 每帧前睡 3s（G7 单飞拒绝窗口，T4 断言）。
//   LA_FACE_FORCE_FRAG=1      —— 正常确定性 RES 应答，但 JSON 体按 64B 分片写 + 片间 fflush
//                                （G4：runner 侧体读循环必须读满，EraseProto 判例继承）。
#include "core/AiProto.h"   // include dir = native/src（CMakeLists 设定），与 app 侧同一真源

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <chrono>
#include <fcntl.h>   // _O_BINARY（Windows CRT 文本模式会改写 0x0A/0x0D，帧字节必须精确）
#include <io.h>      // _setmode/_fileno（MinGW）

namespace {

// 确定性 JSON 体：单脸 bbox/kps + 512 维零值 embedding。与门侧期望表逐字节一致，
// 无任何时间/地址/随机因子（铁律29：合成夹具字节确定）。
std::string faceJson()
{
    std::string s = "{\"ok\":true,\"count\":1,\"faces\":[{\"bbox\":[0,0,8,8],"
                    "\"score\":0.9,\"kps\":[[1,1],[2,2],[3,3],[4,4],[5,5]],"
                    "\"embedding\":[";
    for (int i = 0; i < 512; ++i) { if (i) s += ','; s += "0.0"; }
    s += "]}]}";
    return s;
}

// 剥除行尾 \r\n —— parseReq 的 %n 全耗严格比对要求 n 为规范化后的长度。
void stripEol(char* line)
{
    size_t n = std::strlen(line);
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
}

// 读满 n 字节 body（fread 对短读到 EOF 才收敛，此处再套循环显式守约「必读满」）。
bool readExact(FILE* in, char* buf, size_t n)
{
    size_t got = 0;
    while (got < n) {
        const size_t r = std::fread(buf + got, 1, n - got, in);
        if (r == 0) return false;   // EOF/错误：永远凑不满 = 上游断链
        got += r;
    }
    return true;
}

bool envOn(const char* key)
{
    const char* v = std::getenv(key);
    return v && v[0] == '1';
}

} // namespace

int main()
{
    _setmode(_fileno(stdin), _O_BINARY);
    AI_PROTO_FORCE_BINARY();   // stdout 强制二进制（AiProto 单一真源口径）
    FILE* in = stdin;
    FILE* out = stdout;

    const bool noModel = envOn("LA_FACE_FORCE_NOMODEL");
    const bool forceErr = envOn("LA_FACE_FORCE_ERR");
    const bool forceHang = envOn("LA_FACE_FORCE_HANG");
    const bool forceSlow = envOn("LA_FACE_FORCE_SLOW");
    const bool forceFrag = envOn("LA_FACE_FORCE_FRAG");

    std::fprintf(out, "READY %d\n", noModel ? 0 : 1);
    std::fflush(out);

    char line[512];
    while (std::fgets(line, sizeof(line), in)) {
        stripEol(line);
        const size_t n = std::strlen(line);
        aiproto::Header h;
        if (!aiproto::parseReq(line, n, h)) {
            std::fprintf(out, "ERR %llu badframe\n", h.id);
            std::fflush(out);
            continue;
        }
        std::string body(static_cast<size_t>(h.len), '\0');
        if (h.len > 0 && !readExact(in, &body[0], static_cast<size_t>(h.len))) break;

        if (noModel) {   // spec §5：READY 0 后保活，每 REQ 回 nomodel
            std::fprintf(out, "ERR %llu nomodel\n", h.id);
            std::fflush(out);
            continue;
        }
        if (forceHang) { // G6 断言装置：不回帧，父端超时预算内必须观察到无应答（T4 消费）
            std::this_thread::sleep_for(std::chrono::seconds(90));
            continue;
        }
        if (forceSlow) std::this_thread::sleep_for(std::chrono::seconds(3));

        if (forceErr) {
            std::fprintf(out, "ERR %llu loadfail\n", h.id);
            std::fflush(out);
            continue;
        }
        const std::string res = faceJson();
        std::fprintf(out, "RES %llu %llu\n", h.id,
                     static_cast<unsigned long long>(res.size()));   // spec §5 verbatim：RES <id> <len>；%llu 非 %zu：MSVCRT/UCRT 通吃
        if (forceFrag) { // G4：体按 64B 分片 + 片间 fflush，把管道拆包变必现
            std::fflush(out);
            for (size_t off = 0; off < res.size(); off += 64) {
                const size_t chunk = off + 64 < res.size() ? 64 : res.size() - off;
                std::fwrite(res.data() + off, 1, chunk, out);
                std::fflush(out);
            }
        } else {
            std::fwrite(res.data(), 1, res.size(), out);
        }
        std::fflush(out);
    }
    return 0;
}
