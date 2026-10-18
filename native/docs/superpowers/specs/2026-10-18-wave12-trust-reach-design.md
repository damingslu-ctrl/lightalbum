# 波1+波2「可信+可达」设计规格 · 2026-10-18

> 来源：Design Review 方向1（用户 2026-10-18 拍板「按方向1走正式流程」，逐节 §1–§5
> 已过批准门）。愿景真相源：`docs/design_studio/2026-10-14-vision/`（V0/V1/V2/V3/V4/V5，
> 未跟踪目录）。本 spec 是 writing-plans 的论证基座；冲突处本 spec 优先。

## 0 · 范围与已拍板决策

波1「可信」：凝视模式 + 四灯切换 + 那年今天 + ⌘K v1
波2「可达」：⌘K v1 + 人物馆展室化 + 入藏登记语义

**用户五次拍板**（AskUserQuestion 逐题确认）：
1. 前厅 = **按 V2 屏A 新写**（不沿用 Quiet Frame）。
2. Quiet Frame 城市章节叙事 = **归长廊·波4**（本轮不实现，仅语义归档）。
3. 凝视 = **全面替代选片页**（CullPage 退役）。
4. 人物馆 = **独立新页，接选片空出的导航行**。
5. 厅心展品 = **那年今天**（策展管家/常设展引擎 = 波3，不在本轮）。

**明确不做**（波3+）：策展管家常设展、时间滑轨长廊、⌘K v2 中文整句句法解析、
画布布展/海报导出、暗房合并升级、周年展轮换、偏好学习。**永远不做**（V5 第四象限）：
云端同步、3D 展厅、在线 LLM、社交 feed、视频剪辑、皮肤市场。

## 1 · 总体架构

### 1.1 导航重排表（7 行骨架不动，行号 1:1 对应 stack_ 索引）

| 行 | 现在 | 本轮后 | 说明 |
|---|---|---|---|
| 0 | 首页·HomePage（P17 Quiet Frame） | **前厅·LobbyPage（新）** | 厅心=那年今天 · 玻璃指令栏 · 最新到馆条 |
| 1 | 足迹·MapPage | 不变 | 长廊地理切片=波4 |
| 2 | 图库·GalleryPage | 标题微调「全部藏品」 | 网格降为全库视图，四条 chips 保留 |
| 3 | 选片·CullPage | **人物馆·PeoplePage（新）** | kNav 表换名换图标，栈序不变 |
| 4–6 | 修图/日志/关于 | 不变 | 暗房=波3+ |

现役骨架事实（改时不动它）：`MainWindow.cpp` kNav 表（:104-108）、kNavIcons（:112，
条目 `home/map/gallery/cull/retouch/enhance/log`）、stack addWidget 0..6（:723-743）、
onNav(int)（:1142-1191，MotionKit NavFade 转场）。`Icons.h:12`
`Icons::make(kind, color, size)` 自绘工厂补 `people`（门厅/双人像）与新 `lamp` kind。

### 1.2 组件拓扑

新增 4 份 UI 文件：`ui/LobbyPage.{h,cpp}`、`ui/PeoplePage.{h,cpp}`、
`ui/CmdKPanel.{h,cpp}`、`ui/GazeLightbox.{h,cpp}`（现内嵌于 MainWindow.cpp:153 的
PhotoLightbox 拆出并凝视化；外部 FullCache 注入纪律保留——灯箱内 new FullCache 曾致
LA_MAP_CLICK_QA 0xC0000005，注释与做法原样迁移）。

改造 3 处：`ui/Theme.h`（灯表）、`ui/DarkPalette.h`（apply 泛化）、`core/MotionKit.h`
（+3 预设）；接线层 MainWindow（灯钮/⌘K 快捷键/凝视入口/refresh 汇合点）。

退役 2 对文件：`ui/HomePage.{h,cpp}`、`ui/CullPage.{h,cpp}`（git history 即归档；
buildChapters 波4 重写长廊时从历史取回）。

## 2 · 组件契约

### 2.1 Theme.h 灯表（唯一运行时可变 token 源；constexpr 不可变值组）

```cpp
namespace Lumen {
enum class Lamp : int { Nocturne = 1, Jade = 2, Gekkan = 3, Dusk = 4 };  // L1..L4
enum class Space : int { Lobby, Gallery, People, Studio, Vault, Common };
struct LampTokens { QColor bg, surface, inset, ink, muted, line, gold, emerald; };
inline const LampTokens& lamp(Lamp l);   // 4 组 constexpr 表，值=V4 全文
Lamp  lampFor(Space s);                  // Lobby=Nocturne；People=Jade；Gallery=Jade；
                                         // Studio=Jade；Vault=Jade（Dusk 留给波4 长廊）
bool  lampLocked();   void setLampLocked(bool);   // 锁=手动灯全局封存 lampFor
Lamp  activeLamp();   void setActiveLamp(Lamp);   // QSettings 持久（lamp/mode,lamp/active）
QString lampCssVars(Lamp l);                      // 壳层 :root 变量段
}
// DarkPalette.h：Dark::apply(QApplication&) → Lamp::apply(app, lamp)（QPalette +
// 全 app unpolish/polish）。切灯动效（slow 640 光先行包络）由 MainWindow::rethemeAll 包裹。
```

- V4 四灯值（进 Theme.h 的 constexpr）：L1 夜展 bg #141B16 面 #1D2A22 文 #E8E4D8 缀
  #D8A657；L2 玉照 底 #F2EFE6 面 #FFFFFF 文 #2B2B28 主 #3F6B52；L3 素印 底 #FAFAFA
  灰阶 8 档唯一彩点 #B98A2F；L4 暮金 底 #241A10 霞 #E2A95C 线 #B98A2F。
- **切灯 = 全窗重装帧**（P1 v2 颜色以 Lumen::hex 常量编进构造期 QSS，不能热刷）：
  `MainWindow::rethemeAll()` 记当前行 → apply → 重建 7 栈页与顶栏 → 回放当前行。
  重装会重置图库滚动/筛选——语义对齐「换厅灯重开整殿」，文案用 D5 馆务语。
- 棘轮：`qa_palette_ratchet.ps1` 既有 Theme.h/DarkPalette.h 豁免不变；新代码颜色
  必须走 Lumen 工厂或 LampTokens；C1..C5 棘轮继续全局断言；C2 字阶收 7 档。

### 2.2 LobbyPage（前厅）

```cpp
class LobbyPage : public QWidget {
public:
    LobbyPage(const QVector<Photo>* photos, const QString& thumbsDir, QWidget* parent);
    void setHidePrivate(bool hide);   void refresh();
signals:
    void openGaze(const QString& startPath);
    void navigateTo(int row);
    void importRequested();   void icloudImportRequested();
public:
    static QVector<const Photo*> onThisDay(const QVector<Photo>& in, const QDate& today,
                                           bool hidePrivate, int maxN = 9);
    static QVector<const Photo*> recent(const QVector<Photo>& in,
                                        bool hidePrivate, int maxN = 12);
};
```

- onThisDay：候选 = `!priv && !missing && !thumbFile.isEmpty()` 且 date 月-日 ==
  today 月-日；排序 = |year−today.year| 升序优先、同级 rating 降序；<3 张回退同月、
  再回退去年/往年同季、全空 → 「今日无展 · 明日再会」静默态（绝不画假数据）。
- recent： photos_ 已由 Library 载入后按 date ISO 串**倒序**排序（Library.cpp:130-132，拍摄日期新→旧；photos 表**没有**入库时间戳列），自**向量头**取 N 条即「最新拍摄的到馆展品」；文案「最新到馆」（措辞对齐数据真相，不虚称入库序）。
- 动效：入场 kLightFirst 640ms + 展品 kChipPop quick 220ms 错峰（Motion::staggerStart）。

### 2.3 PeoplePage（人物馆）

```cpp
struct Room {
    QString name; int count = 0; int yearMin = 0, yearMax = 0;
    QColor ring;                                    // 首-字母色环哈希沿用 GalleryPage
    QVector<const Photo*> photos;                   // 日期升序串
};
class PeoplePage : public QWidget {
public:
    PeoplePage(const QVector<Photo>* photos, const QString& thumbsDir, QWidget* parent);
    void setHidePrivate(bool hide);   void refresh();
signals:
    void openGaze(const QString& person, const QString& startPath);
    void filterGallery(const QString& person);
    void clusteringRequested();                    // 复用 MainWindow 人脸检测/聚类链
public:
    static QVector<Room> buildRooms(const QVector<Photo>& in, bool hidePrivate);
};
```

- 单次扫库聚合 person.split(',')（同人照片入多人房）。展签卡 = --mat 装裱（12px
  白卡+2px 投影+金 hover）；展室视图 = year 分桶横向串。**本轮不含**：周年展开关、
  合并/拆分新动作入口（留在图库 chips 右键现状，页脚一行跳转防双入口）。
- 空态：「馆内布展中」引导卡 → clusteringRequested（术语「识别人物」保持）。

### 2.4 CmdKPanel（⌘K v1；E4 开馆层；V2 屏D 形）

```cpp
struct CmdFilter {
    int yearMin = 0, yearMax = 0; int month = 0, day = 0;
    QString season, person, tokenPlace, category;   // 空=未指定
    bool empty() const;
};
class CmdKPanel : public QWidget {
public:
    explicit CmdKPanel(const QVector<Photo>* photos, QWidget* parent);
    void openFor(Lumen::Space s);
signals:
    void applyFilter(const CmdFilter& f);
    void openRoom(const QString& person);
    void openGazeFiltered(const CmdFilter& f);
    void lookBackToday(const QDate& d);
public:
    static CmdFilter parse(const QString& sentence);   // 纯函数·词典层
    static QStringList parseChips(const QString& sentence);
};
```

- 词典四级（全本机零网络）：①时间词法表（去年/前年/今年/XXXX年/春夏秋冬/月份/
  昨天/今天/去年秋天组合）→ year/month/season/day；②人物 = buildRooms(hidePrivate=true)
  派生名单（前缀模糊命中即 chip）——锁定态词典不含私密照人物名；③地点 = region/
  prov/city distinct 集合；④风格 = clipinfer normalizeLabel 8 类中文字面。
  兜底：现有 search_ 的标题/文件名/器材 contains 全保留（老能力零丢失）。
- chips 可见即可纠：点 chip 循环切候选、× 移除；动作行 Enter 执行 / C 暂存为合集
  （v1 禁用占位+tooltip「下轮上线」）。
- 视觉：E4 开馆（金描边+内发光 34px），入场 quick 220ms 无布局位移。

### 2.5 GazeLightbox（凝视全局灯箱）

```cpp
class GazeLightbox : public QWidget {
public:
    enum class Mode { Browse, Gaze };
    void setScope(const BrowseScope& s);   void setHidePrivate(bool);
    void enter(Mode m, const QString& startPath);   void setFullCache(FullCache*);
signals:
    void markRequested(const QStringList& paths, const QString& mark); // → Library::setMarks
    void ratingChanged(const QString& path, int rating);
    void dismissed(bool dirty);
};
```

- Browse 保留现键盘流（←→/Esc/星级/0xC0000005 外部缓存纪律）；Gaze 叠加档：无边框
  + hover 雾条 + 环境光晕（主色取自缩略 64px 中心加权中位数饱和色，低饱和回退
  lamp 辅助色；实时算不落库）+ 翻页即换展（旧画淡出 120ms / 新画 360ms 携光入位，
  MotionKit kLightbox 变奏，同屏并发 ≤2，可被任何输入打断）。
- 键位隐喻（V3 原意=键位非摄像头）：S=like（点头收藏）、Delete/A=skip（摇头待淘汰）、
  L=lock（锁定防误删）。**即时写库**：每次按键即 `Library::setMarks(len=1)`（单事务
  原子，mark 列语义 "like"/"skip"/"lock" 与图库/去重一致），toast 可撤销 3s
  （撤销=同键再按一次→回写空串清除，API 原生语义）。

### 2.6 MainWindow 接线

- 顶栏 `search_`（:634-656 QLineEdit）删除；`searchToggle_` 🔍 改指 CmdKPanel，
  注册 Ctrl+K QShortcut 全局；onNav 维护 `currentSpace`。
- 灯钮 `💡`（Icons 自绘 kind=lamp，InstantPopup）：4 灯 + 「跟随空间」勾选锁；
  切灯即 rethemeAll + slow 640 包络。
- 凝视入口：前厅厅心、人物馆展室、图库双击 `openLightbox` 改 `gaze_->enter(...)`。
- refresh 汇合点（导入完成/凝视 dismissed/vault 解锁）扩到 lobby_/people_。

## 3 · 动效与语义（D3 档位全继承，零新时长）

MotionKit 预设表 5+3：kNavFade/kLightbox/kChipPop/kStarBounce/kHeartBurst +
kLightFirst(slow 640 进前厅/凝视入场) + kPushDoor(standard 360 进人物馆，scale
1.02→1.00) + kLightsDown(standard 360 预留键)。onNav 挂预设：进前厅=kLightFirst、
进人物馆=kPushDoor、其余 kNavFade；**转场可被任何输入打断**（D3 动宪前 T 第一条
原文）；减动效 MotionFX animate=false 自动 instant（现有语义）。

字阶：Theme::Type 现行 5 档（Display/Title/Head/Body/Small，Theme.h:125）扩至
V4 七档：+Label 17/500（展签体：人物名/年份牌/合集题/⌘K 展题）+Mono-Meta 11/500；
衬线混排仅 L1/L4 下允许。LA_PALETTE_QA C2 字阶断言随之收 7 档。

共用三件（一律走工厂）：--mat 装裱单元（12px 白卡+2px 投影：人物馆卡/⌘K 容器/前厅
展签，不含图库网格与凝视大图）；E4 开馆层（仅 ⌘K 面板+前厅厅心卡）；光晕语言
`Lumen::glowCss(color, strength)`（径向渐变 alpha ≤10%、中心上缘外侧；三处共用）。

语义命名表（i18n 直入）：剔底片(skip)/收藏(like)、检索(Ctrl+K)、入藏登记
（导入 toast「N 幅入藏」）、全部藏品（图库标题）、布展中（人物馆/前厅索引进度）。
「其他」类 note 依据外显到 chips tooltip（CLIP note 字段已有，零模型调用）。

隐私红线（铁律 21 注入点过滤）：四处新 UI 全部 setHidePrivate(!vaultUnlocked_)；
私密照根本不进候选池/词典/展室；锁定态 ⌘K 人物词典派生自 hidePrivate 集合。

i18n：一律 `QCoreApplication::translate("LightAlbum", "中文")`；新条目预计 40–60；
.ts 手编（</message> 锚点、& ' < > 转义）、lrelease、.qm 同 commit（f92ecb4 模板）、
lupdate 0 新增。

## 4 · 数据流

**切灯流**：灯钮/QShortcut → Lamp::setActiveLamp（QSettings lamp/mode|lamp/active）
→ rethemeAll：记当前行 → Lamp::apply + unpolish/polish → 重建栈页/顶栏 → 回放。
follow 模式 onNav 时 lampFor(space) 自动切。代价（真实，非缺陷）：图库滚动/筛选
重置由回放行号补偿到「回到页头」，筛选条件不保留。

**凝视写库流**：按键即 setMarks(len=1) → toast+3s 撤销 → dismissed(dirty) →
Library read 型 refresh → 三新页同步重建（photos_ 引用不改左右值，重建索引即可）。

**⌘K 流**：parse → chips 纠 → Enter：人物高置信 → openRoom；否则 applyFilter →
GalleryModel 过滤键扩容（year/month/season 三键，机制承现有 hasCat/categoryFilter_）
+ person/category 直达 → openGazeFiltered 或凝视。

**前厅刷新流**：库变化点（导入/凝视 dismissed/解锁）→ refresh 汇合点 → lobby_/
people_ 双页重建（onThisDay 以当天实际日期重算）。

## 5 · 测试与门禁（名册 78 → 79）

新增 5 门（fast-tier exe 静态/合成门，物形承 LA_CULL_EXEMPT_QA：分组断言+
FORCE_BROKEN 反例变红+measuredMs 回填；纯 ASCII 注册进 gate_matrix.ps1）：

- LA_LOBBY_QA：onThisDay 边界（恰逢今日/闰 2-29/不足 3 张三级回退/全空静默态/
  priv 排除）+ recent 头取（date 倒序即最新拍摄优先）+ 任意「今天」重放（QDate 注入）；反例=priv 涌入候选 → 红。
- LA_PEOPLE_QA：buildRooms（逗号多人入多房/私密剔除/年跨度聚合/空 name 防御）；
  反例=hidePrivate 失效 → 红。
- LA_CMDK_QA：parse 金句集（「去年秋天和小雨在公园的黑白照片」→ year/season/
  person/category 四 chip 全中）+ 地点/风格/兜底 contains；反例=词典错位 → 红。
- LA_GAZE_QA：主色提取（暖/冷/低饱和回退）+ BrowseScope 私密步进纪律复用现断言；
  反例=主色钳制旁路 → 红。
- LA_LAMP_QA：4×8 token 完备且成对相异 + lampFor 映射 + 锁语义优先级；反例=两组
  表互换 → 红。

退役 4 门：LA_CULL_SMOKE、LA_CULL_DUEL_QA（png tier，名册布尔登记同步删）、
LA_CULL_VIDEO_QA、LA_HOME_SMOKE（随类删）。**保留** LA_CULL_EXEMPT_QA（纯谓词，
CullEngine/PhotoDedup 与页面解耦存活，exemptFromQualityCull 不依赖 CullPage）。
净变化 78−4+5=**79**；名册 SelfTest 与 fast-tier ps1 禁令（E5/E9d）联动核验。

配额纪律：LA_PALETTE_QA baseline 重生成（C2 记 7 档新档）；LA_UI_SHOT 重定基底
（前厅/人物馆/⌘K/凝视四张新截图入 baseline map）；LA_MOTION_QA 预设表新增 3 键
注册断言；LA_I18N_QA 读 .qm 验新增条目。

版本收口 **2.41.0**：CMakeLists/CMakeLists 版本、CHANGELOG [2.41.0]、指导书 §7 行、
使用说明书。R-06 红线：这一轮一切新 QA 用合成图，provisioning 候选集也是合成——
用户的真实照片永远不进仓（模板承 R4-T12 FIX 波）。

## 6 · 风险与已勘明事实（writing-plans 输入）

1. **P1 v2 颜色常量编进构造期 QSS** → 切灯必须整帧重建（§2.1），不存在「热刷」
   便宜路；文案与回滚行为按 D5 语义定名。
2. **photos_ 引用型数据流**：LobbyPage/PeoplePage 持 `const QVector<Photo>*` 同
   GalleryPage/CullPage——refresh=重建内存视图，无新线程锁（铁律：Library GUI 线程
   单写者）。
3. **CullEngine 保留但页面退役**：LA_CULL_EXEMPT_QA 属 CullItem 谓词门，不碰
   CullPage；PhotoDedup（phash hamming）在图库去重按钮里存活。
4. **切灯语义整帧重建的重置代价**已写进 §2.1 与 §4；若用户否认该语义，唯一备选
   是「切灯仅换 QPalette+不重建」——那是 P1 v2 后已证实不可行的伪热刷，spec 不收。
5. **展签体衬线族**：Georgia/宋刻 仅 L1/L4，平台字符串 "Microsoft YaHei UI" 已在
   Theme.h:127（QFont 命名纪律沿用）。
6. 文档真相源 链接：V1 归置表（7 页去向）、V2 屏A/D（前厅+⌘K 形）、V3 六交互
   （凝视换展/时间滑轨项=波4/⌘K 动效档）、V4（四灯 token+字阶七档+--mat+E4）、
   V5（波次与象限定性）。
