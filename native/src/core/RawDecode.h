#pragma once
// RAW 解码桥（2.28.0）—— 通过**外部进程**调用 LibRaw 的 dcraw_emu 产出中间图，
// 再交回 QImageReader 读取。与其余解码栈（全部走 QImageReader）保持同一收口。
//
// 为什么走外部进程而不是链入 libraw（架构决策，勿轻易推翻）：
//   LibRaw 官方只发 MSVC 预编译产物（LibRaw-*-Win64.zip）。若链入 exe，则 exe 与
//   libraw.dll 之间存在 CRT/ABI 绑定（跨 MinGW/MSVC 不兼容）。而以独立进程调用时，
//   宿主编译器与解码器编译器**完全解耦** —— 实测 MSVC 2022 预编译产物在本工程
//   MinGW13.1 构建的 exe 下直接可用（vcruntime140.dll 系统已带）。
//
// 中间格式选 PPM 而非 TIFF（实测标定，勿凭直觉改）：
//   本机 Qt 插件目录仅 qgif/qico/qjpeg/qsvg，**无 qtiff.dll**。实测宿主的读图能力：
//     PPM(P6) → 1737x1157 非空（Qt **内建** handler，非插件，不依赖部署）
//     JPEG    → 1024x683 非空（qjpeg）
//     TIFF    → null / 0x0   ← 走 TIFF 会在运行期静默失败
//   故 PPM 是唯一既能全解码、又被宿主实读通过的中间格式。
//
// 尺寸来源：EXIF 的 out.dim 走 QImageReader::size()，对 RAW 返回 invalid
//   （QImageReader 打不开 RAW）→ RAW 的尺寸必须取自 PPM 头部，本模块负责解析。
//
// 数据诚实铁律：任一环节失败一律返回「空 / 失败」，绝不返回编造的尺寸或占位图。
#include <QString>
#include <QSize>

namespace RawDecode {

// RAW 扩展名白名单（不含 .dng 之外的容器噪声；判定与扫描层共用同一真源）。
// 说明：.dng 一并在内 —— 实测 ExifReader 按**文件签名**（II*\0 / MM\0）分派 TIFF 容器，
// DNG 的 EXIF 今天就能解析，无需任何新代码。
bool isRawExt(const QString& fileName);

// 该文件此刻**是否应被导入**（纯函数，availability 显式传入 → 可被门禁直接断言，
// 不依赖机器上是否真装了解码器）。
// 语义：普通照片恒真；RAW 仅在解码器就位时为真。
//   —— 刻意如此：解码器缺失时若仍导入 RAW，会产出一整排**没有缩略图的空卡片**，
//      比"看不见 RAW"更糟。优雅降级为"不导入"才是诚实行为。
bool isImportable(const QString& fileName, bool decoderAvailable);

// 该文件是否属于「需要外部解码器才能纳入」的集合 —— 扫描枚举阶段的**唯一**判据。
// ★ 语义边界（极易写错，勿与 isImportable 混用）：
//   本函数**只对 RAW 返回 true**，其余一律 false。它是给扫描器做「并联」用的：
//       if (!PathGuess::isPhoto(n) && !RawDecode::needsExternalDecode(n, avail)) continue;
//   绝不能返回「非 RAW 即 true」—— 那样 `!isPhoto && !needs...` 永假，
//   等于放行 .json/.ini/.AAE/.icloud 等一切噪声（实测 LA_ICLOUD_QA total 5→10 即此坑）。
bool needsExternalDecode(const QString& fileName, bool decoderAvailable);

// 解码器可用性：外部产物（dcraw_emu.exe + libraw.dll）是否就位。
// 缺失时全链路优雅降级为「RAW 不参与导入」，绝不崩溃、绝不静默产出坏图。
bool available();

// 解码器的落位目录（供 UI 提示 / 门禁取证），未就位返回空串。
QString toolDir();

// 探测 RAW 尺寸（走一次真实解码读 PPM 头；失败返回无效 QSize）。
// 代价约 0.5s/张 —— 仅在需要真实尺寸时调用，缩略图链路请走 decodeToTempPpm。
QSize probeSize(const QString& rawPath);

// 把 RAW 解码为 PPM 并返回**临时文件路径**（调用方负责删除）。
// 成功返回非空路径；失败返回空串。绝不返回未解码成功的中间文件。
//
// ★ 反例/门禁铁律：dcraw_emu 对**未知选项不报错、退出码仍为 0**
//   （实测传 -e 时打印 Unknown option 却照样产出 24MB PPM）。
//   故判定成败**只能**看 PPM 产物是否存在且尺寸合理，绝不可依赖 exit code。
//
// ★ 反例证据（tools/raw_counterexample2.sh 实测，勿推翻）：
//   注入缺陷 = 「rc==0 即认为成功 + 产物名按『替换扩展名』猜测（真实缺陷形态）」，
//   输入 = **合法 DNG**。结果两种模式 **rc 都是 0**，但：
//     正确实现 → derived=x.dng.ppm size=1737x1157 exists=1 → 绿
//     缺陷实现 → derived=x.ppm    exists=0               → 红
//   即 exit code 在此**完全不携带信息**，唯一判据是产物校验。
//   （前一版反例 v1 注入「只信 rc」，因畸形输入时 dcraw_emu 实测 rc=2 也照样失败 →
//     红绿不分、无法判别，已按铁律 23 作废重做。教训：反例必须选对缺陷形态。）
QString decodeToTempPpm(const QString& rawPath, int timeoutMs = 30000);

// QA 钩子（2.28.0）：置 true 后 decodeToTempPpm 改走缺陷形态 ——
// 「rc==0 即认为成功，且产物名按**替换**扩展名猜测」，跳过一切产物校验。
// 这正是真实会犯的错（实测 dcraw_emu 对合法 DNG 的 rc 恒为 0，exit code 不携带信息）。
// 期望值不随本开关改变（铁律 22）：正确实现与缺陷实现必须给出**不同**结果。
void setForceArtifactBlindForQa(bool on);

} // namespace RawDecode
