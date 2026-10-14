#pragma once
// preproc —— clipinfer 侧图像预处理核（第四轮 T7）。
// 平价真源 = classifier_server.py:186 `prep(im.convert('RGB'))`（open_clip ViT-B-32
// image_transform：shortest-edge=224 BICUBIC 缩放 + CenterCrop 224 + ToTensor /255 +
// CLIP mean/std 归一 RGB NCHW）与 :160-169 is_monochrome（`convert('RGB').resize((48,48))`
// 默认 BICUBIC → HSV 平均饱和度）。
//
// bicubic 变体裁定（spec 风险 #1，R-14 收口）：oracle 由 Pillow 生成，Pillow 的
// BICUBIC = Catmull-Rom(a=-0.5)（见 Resample.c bicubic_filter），故唯一能与 gold
// 对拍的核即 Catmull-Rom；scipy-mitchell(B=1/3,C=1/3) 异核、与 oracle 不同族 → 未选、删净
// （评审看最终码）。缩放链 verbatim 移植 libImaging Resample.c 8bpc 定点整数路径
// （precompute_coeffs + normalize_coeffs_8bpc + clip8 + 两趟 H/V），reducing_gap=None
// （Pillow resize 默认不做预滤波，见 Image.py resize 形参 reducing_gap 默认 None）。
//
// 铁律29：无 Qt/无宿主因子，纯整数/浮点确定性（同 faceinfer imgkit 判例族）。
#include <cstdint>
#include <string>
#include <vector>

namespace preproc {

// 交错 RGB HWC uint8（stride=sw*3）。
struct RgbImage {
    int w = 0, h = 0;
    std::vector<uint8_t> rgb;   // w*h*3
};

// 按 magic 嗅探解码 JPEG/PNG/BMP 等到 RGB888（stb_image 同解，R-15：PNG 夹具不换格式）。
// 失败返回 false。
bool loadRgb(const std::string& path, RgbImage& out);

// PIL bicubic(Catmull-Rom) 两趟缩放，box=(0,0,sw,sh)，reducing_gap=None。
// src=sw×sh×3，dst=dw×dh×3。与 libImaging ImagingResample 位序一致。
void resizeBicubic(const RgbImage& src, int dw, int dh, RgbImage& dst);

// CLIP 主干归一：shortest-edge=224 resize → center-crop 224 → /255 → (x-mean)/std。
// 产出 NCHW float 1×3×224×224（3*224*224 floats）。
void clipTensor(const RgbImage& src, std::vector<float>& nchw);

// is_monochrome：48×48 bicubic 缩略 → 逐像素 RGB→HSV 取 S 通道（0-255）求均值。
// 返回平均饱和度（0-255 域）；判定 mono 由 rules 层用阈值 18.0 做（此处只算 sat）。
double meanSaturation(const RgbImage& src);

} // namespace preproc

// rules —— prompts.bin 校验 + CLIP 判定三段（classifier_server.py:205-231 逐字对读）。
// 常量 verbatim：MONO_LOW_SAT=18.0 / MONO_HIGH_SAT=45.0 / MIN_CONFIDENCE=0.20 /
// LOGIT_SCALE=100.0；「其他」为第 9 兜底名（不在 prompts 行内）。
// 铁律29：全整数/定点/纯算术确定性；SHA-256 自带实现（无 Qt/无 SSL 依赖）。
namespace rules {

// LABELS 行序 = prompts.bin 行序（0..7，'·'=U+00B7 UTF-8 c2 b7）。
extern const char* const kLabels[8];
extern const char* const kOtherLabel;             // "其他"
// prompts.bin 矩阵体 SHA256（bytes[0..16404)），T6 导出口径 d06822a2…（非整文件 SHA）。
extern const char* const kPromptsMatrixSha256;

struct PromptTable {
    int n = 0;                          // 8
    std::vector<float> rows;            // n×512 float32LE 行主序（每行 L2=1）
};

// 校验链：文件存在且 16436B → 魔数 "LAPROMPTv1" → 偏移10-11 双零 pad → int32LE N=8 →
// int32LE D=512 → SHA256(bytes[0..16404)) == kPromptsMatrixSha256。任一不符返回 false
// 并给 reason（G10 反例：末字节翻转 → SHA 失配 → READY 0 + ERR badprompts）。
bool loadPrompts(const std::string& path, PromptTable& out, std::string& reason);

struct Top3Item { const char* label; double prob; };

struct Verdict {
    std::string label;                  // UTF-8（八类名或「其他」）
    double score = 0.0;                 // 已 round4
    double sat = 0.0;                   // 已 round1（0-255 域）
    bool hasNote = false;
    std::string note;                   // low_saturation(..)|low_confidence|mono_guard(sat=..)
    std::vector<Top3Item> top;          // 按 round(p,4) 降序（并列保索引序）前 3
};

// 判定三段（顺序 verbatim）：feat512（未 L2 归一的原样塔输出，此函数内做 L2）×
// prompts → logits=100*f@T → softmax → best=argmax(raw)；
// (1) sat<=18 → 黑白/0.99/low_saturation(%.1f)；(2) elif best<0.20 → 其他/best/
// low_confidence；(3) elif label==黑白 && sat>45 → 非黑白 max（严格 > 更新，先者胜）
// 若 alt>=0.20 换名，note=mono_guard(sat=%.1f)，否则 其他/best/同 note。
void decide(const float* feat512, const PromptTable& p, double satRaw, Verdict& v);

// RES JSON 体手工渲染（控制 %.4f/%.1f 与 note null-or-quoted，中文 UTF-8 直出）：
// {"ok":true,"label":"..","score":0.8700,"note":null,"top":[["..",0.8700],...],"sat":42.0}
std::string renderJson(const Verdict& v);

} // namespace rules
