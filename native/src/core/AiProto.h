#pragma once
// AI 子进程线协议（spec §5）。stdout 只走帧，日志只走 stderr。
// 血泪规约（EraseProto 判例继承）：RES 体必读满——waitForReadyRead 的
// 到达批次边界任意，readExactly 循环直到 n 字节或超时。
#include <cstdint>
#include <cstddef>
#include <cstdio>   // std::sscanf（brief 省略项，编译必需）
#include <cstring>  // std::strcmp（T1 评审遗留：前缀 strncmp → 全词 strcmp 收紧）

#ifdef _WIN32
#include <fcntl.h>  // _O_BINARY（MinGW 该宏在此头，非 io.h）
#include <io.h>
#define AI_PROTO_FORCE_BINARY() _setmode(_fileno(stdout), _O_BINARY)
#else
#define AI_PROTO_FORCE_BINARY() ((void)0)
#endif

namespace aiproto {
inline const char* kReq   = "REQ ";
inline const char* kRes   = "RES ";
inline const char* kErr   = "ERR ";
inline const char* kReady = "READY";

struct Header {
    unsigned long long id = 0;
    char kind[16] = {0};   // "det"（canonical，face）| "path"（face 旧别名，exe 同义接受）| "jpg"（clip）
    unsigned long long len = 0;
};

// 解析 "REQ <id> <kind> <len>"。返回 false = 坏帧。
// 行尾 \r\n 必须先由调用方剥除再传入 n=strlen(规范化行)——%n 全耗严格比对依赖它
// （子进程侧与 runner 侧两条读线共用此契约，#75 门覆盖）。
// T1 评审遗留收口（T4）：首词全词比对 strcmp——"REQX"/"REQ123" 等前缀伪装词必须拒
// （旧 strncmp(3) 只比前缀会放行，父端发错帧时子进程会错误消费 body 造成流错位）。
inline bool parseReq(const char* line, size_t n, Header& h) {
    char tok[8] = {0};
    unsigned long long a = 0, c = 0;
    int used = 0;
    if (std::sscanf(line, "%7s %llu %15s %llu %n",
                    tok, &a, h.kind, &c, &used) < 4) return false;
    if (std::strcmp(tok, "REQ") != 0 || used <= 0) return false;
    if (static_cast<size_t>(used) != n) return false;
    h.id = a; h.len = c; return true;
}
} // namespace aiproto
