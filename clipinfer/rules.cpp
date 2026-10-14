// rules.cpp —— prompts.bin 校验 + CLIP 判定三段（classifier_server.py:205-231 verbatim）。
// 常量钉死：MONO_LOW_SAT=18.0 / MONO_HIGH_SAT=45.0 / MIN_CONFIDENCE=0.20 / SCALE=100.0；
// 判定顺序三段不可换序（low_saturation 先于 low_confidence，mono_guard 收尾）。
// 中文标签用 \x 十六进制转义直出 UTF-8（源文件保持纯 ASCII，规避码页漂移；
// '·'=U+00B7=UTF-8 c2 b7）。SHA-256 自带实现（铁律29：无 Qt/无外部密码学依赖）。
#include "preproc.h"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace rules {

const char* const kLabels[8] = {
    "\xe4\xba\xba\xe5\x83\x8f",                       // 人像
    "\xe9\xa3\x8e\xe5\x85\x89",                       // 风光
    "\xe5\xa4\x9c\xe6\x99\xaf\xc2\xb7\xe6\x98\x9f\xe7\xa9\xba", // 夜景·星空
    "\xe5\x9f\x8e\xe5\xb8\x82\xc2\xb7\xe5\xbb\xba\xe7\xad\x91", // 城市·建筑
    "\xe5\xbe\xae\xe8\xb7\x9d\xc2\xb7\xe8\x8a\xb1\xe5\x8d\x89", // 微距·花卉
    "\xe5\x8a\xa8\xe7\x89\xa9",                       // 动物
    "\xe7\xbe\x8e\xe9\xa3\x9f",                       // 美食
    "\xe9\xbb\x91\xe7\x99\xbd",                       // 黑白
};
const char* const kOtherLabel = "\xe5\x85\xb6\xe4\xbb\x96";  // 其他

const char* const kPromptsMatrixSha256 =
    "d06822a29f77911c5559d1f770793123da9f4f8e438a78bb4a82ec9dee9fc481";

namespace {

constexpr int    kMonoIdx = 7;              // 黑白行号（LABELS 行序）
constexpr double kMonoLowSat = 18.0;
constexpr double kMonoHighSat = 45.0;
constexpr double kMinConfidence = 0.20;
constexpr double kLogitScale = 100.0;
constexpr int    kDim = 512;
constexpr size_t kPromptsFileSize = 16436;  // 12+8 + 8*512*4 + 32

// ---------- SHA-256（FIPS 180-4 参考实现，纯标量） ----------
struct Sha256 {
    uint32_t h[8];
    uint64_t len = 0;
    uint8_t buf[64];
    size_t nbuf = 0;
    static uint32_t ror(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
    void blocks(const uint8_t* p, size_t n)
    {
        static const uint32_t K[64] = {
            0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,
            0x923f82a4u,0xab1c5ed5u,0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,
            0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,0xe49b69c1u,0xefbe4786u,
            0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
            0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,
            0x06ca6351u,0x14292967u,0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,
            0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,0xa2bfe8a1u,0xa81a664bu,
            0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
            0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,
            0x5b9cca4fu,0x682e6ff3u,0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,
            0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u};
        for (; n >= 64; p += 64, n -= 64) {
            uint32_t w[64];
            for (int i = 0; i < 16; ++i)
                w[i] = (uint32_t(p[i * 4]) << 24) | (uint32_t(p[i * 4 + 1]) << 16)
                     | (uint32_t(p[i * 4 + 2]) << 8) | uint32_t(p[i * 4 + 3]);
            for (int i = 16; i < 64; ++i) {
                const uint32_t s0 = ror(w[i-15],7)^ror(w[i-15],18)^(w[i-15]>>3);
                const uint32_t s1 = ror(w[i-2],17)^ror(w[i-2],19)^(w[i-2]>>10);
                w[i] = w[i-16] + s0 + w[i-7] + s1;
            }
            uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
            for (int i = 0; i < 64; ++i) {
                const uint32_t S1 = ror(e,6)^ror(e,11)^ror(e,25);
                const uint32_t ch = (e&f)^((~e)&g);
                const uint32_t t1 = hh + S1 + ch + K[i] + w[i];
                const uint32_t S0 = ror(a,2)^ror(a,13)^ror(a,22);
                const uint32_t mj = (a&b)^(a&c)^(b&c);
                const uint32_t t2 = S0 + mj;
                hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
            }
            h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
        }
    }
    void update(const uint8_t* p, size_t n)
    {
        len += n;
        if (nbuf) {
            const size_t take = (64 - nbuf) < n ? (64 - nbuf) : n;
            std::memcpy(buf + nbuf, p, take); nbuf += take; p += take; n -= take;
            if (nbuf == 64) { blocks(buf, 64); nbuf = 0; }
        }
        blocks(p, n & ~size_t(63));
        const size_t rest = n & 63;
        if (rest) { std::memcpy(buf, p + (n - rest), rest); nbuf = rest; }
    }
    void finalHex(char out[65])
    {
        const uint64_t bits = len * 8;
        const uint8_t pad = 0x80;
        update(&pad, 1);
        const uint8_t zero = 0;
        while (nbuf != 56) update(&zero, 1);
        uint8_t lb[8];
        for (int i = 0; i < 8; ++i) lb[i] = uint8_t(bits >> (56 - 8 * i));
        update(lb, 8);
        for (int i = 0; i < 8; ++i)
            std::sprintf(out + i * 8, "%08x", h[i]);
        out[64] = 0;
    }
};

inline uint32_t le32(const uint8_t* p)
{
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

// Python round 半偶舍入（仅用于 top 排序键 round(p,4) 与输出 score/sat 同域比较）。
// 格式化输出走 %.4f/%.1f（dtoa 近偶，与 Python str 格式化同源）；此处数值键用 nearbyint
// （默认舍入模式 FE_TONEAREST == 半偶），与 oracle `round(p, 4)` 语义对齐。
inline double round4(double v) { return std::nearbyint(v * 10000.0) / 10000.0; }

} // namespace

bool loadPrompts(const std::string& path, PromptTable& out, std::string& reason)
{
    FILE* f = nullptr;
    // UTF-8 路径 → 宽字符（与 preproc.loadRgb 同口径）。
    const int wn = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), int(path.size()), nullptr, 0);
    std::wstring wp;
    if (wn > 0) {
        wp.resize(size_t(wn));
        MultiByteToWideChar(CP_UTF8, 0, path.c_str(), int(path.size()), &wp[0], wn);
        f = _wfopen(wp.c_str(), L"rb");
    }
    if (!f) { reason = "prompts: file not found"; return false; }
    std::vector<uint8_t> bytes;
    {
        uint8_t buf[4096];
        size_t r = 0;
        while ((r = std::fread(buf, 1, sizeof(buf), f)) > 0) bytes.insert(bytes.end(), buf, buf + r);
        std::fclose(f);
    }
    if (bytes.size() != kPromptsFileSize) {
        reason = "prompts: bad file size";
        return false;
    }
    if (std::memcmp(bytes.data(), "LAPROMPTv1", 10) != 0 || bytes[10] != 0 || bytes[11] != 0) {
        reason = "prompts: bad magic/pad";
        return false;
    }
    if (le32(bytes.data() + 12) != 8 || le32(bytes.data() + 16) != 512) {
        reason = "prompts: bad N/D";
        return false;
    }
    char hex[65];
    Sha256 s;
    s.h[0]=0x6a09e667u; s.h[1]=0xbb67ae85u; s.h[2]=0x3c6ef372u; s.h[3]=0xa54ff53au;
    s.h[4]=0x510e527fu; s.h[5]=0x9b05688cu; s.h[6]=0x1f83d9abu; s.h[7]=0x5be0cd19u;
    s.update(bytes.data(), 16404);
    s.finalHex(hex);
    if (std::strcmp(hex, kPromptsMatrixSha256) != 0) {
        reason = "prompts: matrix sha256 mismatch";
        return false;
    }
    out.n = 8;
    out.rows.assign(size_t(8) * kDim, 0.f);
    // float32LE → native float32：小端 x86 直 memcpy（MinGW/MSVC 同形）。
    for (int i = 0; i < 8 * kDim; ++i)
        std::memcpy(&out.rows[size_t(i)], bytes.data() + 20 + size_t(i) * 4, 4);
    return true;
}

void decide(const float* feat512, const PromptTable& p, double satRaw, Verdict& v)
{
    // L2 归一（无 eps，oracle 同式）。
    double norm = 0.0;
    for (int i = 0; i < kDim; ++i) norm += double(feat512[i]) * double(feat512[i]);
    norm = std::sqrt(norm);
    std::vector<float> f;
    f.resize(size_t(kDim));
    for (int i = 0; i < kDim; ++i) f[i] = feat512[i] / float(norm);

    // logits = 100.0 * f @ prompts.T；softmax（max 减法数值稳定）。
    double logits[8], probs[8];
    for (int r = 0; r < 8; ++r) {
        double dot = 0.0;
        const float* row = p.rows.data() + size_t(r) * kDim;
        for (int i = 0; i < kDim; ++i) dot += double(f[i]) * double(row[i]);
        logits[r] = kLogitScale * dot;
    }
    double mx = logits[0];
    for (int r = 1; r < 8; ++r) if (logits[r] > mx) mx = logits[r];
    double sum = 0.0;
    for (int r = 0; r < 8; ++r) { probs[r] = std::exp(logits[r] - mx); sum += probs[r]; }
    for (int r = 0; r < 8; ++r) probs[r] /= sum;

    // best = argmax(raw probs)（严格 > → 并列先者胜，numpy argmax 同义）。
    int best = 0;
    for (int r = 1; r < 8; ++r) if (probs[r] > probs[best]) best = r;
    const double bestScore = probs[best];

    // top3：按 round(p,4) 降序、并列保索引序（Python sorted 稳定 + key=round 同构）。
    int order[8];
    for (int r = 0; r < 8; ++r) order[r] = r;
    for (int i = 1; i < 8; ++i) {                    // 插入排序 = 稳定
        const int x = order[i];
        int j = i - 1;
        while (j >= 0 && round4(probs[order[j]]) < round4(probs[x])) { order[j + 1] = order[j]; --j; }
        order[j + 1] = x;
    }
    v.top.clear();
    for (int i = 0; i < 3; ++i) v.top.push_back(Top3Item{kLabels[order[i]], probs[order[i]]});

    // 判定三段（顺序 verbatim，classifier_server.py:205-231）。
    char nb[64];
    if (satRaw <= kMonoLowSat) {
        v.label = kLabels[kMonoIdx]; v.score = 0.99; v.hasNote = true;
        std::snprintf(nb, sizeof(nb), "low_saturation(%.1f)", satRaw); v.note = nb;
    } else if (bestScore < kMinConfidence) {
        v.label = kOtherLabel; v.score = bestScore; v.hasNote = true; v.note = "low_confidence";
    } else if (best == kMonoIdx && satRaw > kMonoHighSat) {
        int alt = -1;
        for (int r = 0; r < kMonoIdx; ++r)            // 非黑白 max，严格 >（先者胜）
            if (alt < 0 || probs[r] > probs[alt]) alt = r;
        if (probs[alt] >= kMinConfidence) {
            v.label = kLabels[alt]; v.score = probs[alt];
        } else {
            v.label = kOtherLabel; v.score = bestScore;
        }
        v.hasNote = true;
        std::snprintf(nb, sizeof(nb), "mono_guard(sat=%.1f)", satRaw); v.note = nb;
    } else {
        v.label = kLabels[best]; v.score = bestScore; v.hasNote = false;
    }
    v.sat = satRaw;
}

std::string renderJson(const Verdict& v)
{
    // 手工渲染：%.4f/%.1f 与 note null-or-quoted 全控（dtoa 近偶 = Python 同源）。
    std::string s = "{\"ok\":true,\"label\":\"";
    s += v.label;
    char b[512];
    std::snprintf(b, sizeof(b), "\",\"score\":%.4f,\"note\":", v.score); s += b;
    if (v.hasNote) { std::snprintf(b, sizeof(b), "\"%s\"", v.note.c_str()); s += b; }
    else s += "null";
    s += ",\"top\":[";
    for (size_t i = 0; i < v.top.size(); ++i) {
        if (i) s += ",";
        std::snprintf(b, sizeof(b), "[\"%s\",%.4f]", v.top[i].label, v.top[i].prob);
        s += b;
    }
    s += "]";
    std::snprintf(b, sizeof(b), ",\"sat\":%.1f}", v.sat);
    s += b;
    return s;
}

} // namespace rules
