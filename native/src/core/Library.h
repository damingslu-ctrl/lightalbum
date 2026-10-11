#pragma once
// P1 数据层：直读 library.db，exif_json 经 QJsonDocument 解包。
// 数据诚实铁律（平移自 web 版 L 节）：缺失 = 0/空串/hasGps=false，严禁编造默认值。
// P16：小量写回（评分/私密标记/隐私库口令）走独立读写连接；大 SELECT 仍只读。
#include <QString>
#include <QStringList>
#include <QVector>
#include <QVariant>
#include <QHash>
#include <QByteArray>
#include <QSqlDatabase>
#include <atomic>

// 方向 D 第五步：批量写回结果。
// 语义铁律：**原子** —— 任一条失败则整体回滚，done 归零、内存副本不动。
// failedIndex 给出首个失败下标（-1 = 无失败）；error 为 SQL 原文（便于门断言/排障）。
struct BatchResult {
    int     total = 0;        // 请求条数
    int     done = 0;// 实际命中行数（UPDATE 影响行数之和）
    int     failedIndex = -1; // 首个失败下标
    bool    ok = false;       // 事务是否提交成功
    QString error;      // ok=false 时的 SQL 错误原文
    bool isNoop() const { return total == 0; }
};

// 方向 C·智能分类：单条写回项（path 标识 + CLIP 场景标签 + 置信度）。
// score 仅用于分类器侧阈值判定；写回 DB 时只持久化 label（与 photos 表列一致）。
struct CatItem {
    QString path;
    QString label;
    double  score = 0.0;
};

// 方向 C·人脸识别（方案A）：单条写回项（path 标识 + 人物名）。
// 写回 DB 时只持久化 name（与 photos 表 person 列一致，逗号分隔多人物）。
struct PersonItem {
    QString path;
    QString name;
};

// 方向 C·人物自动聚类：单条写回项（path 标识 + 512-d float32 代表脸 embedding BLOB）。
// emb 为小端序 float32 字节流（512 * 4 = 2048 字节），由 PersonDetector 从 face_server 抽取。
struct FaceItem {
    QString     path;
    QByteArray  emb;
};

// 方向 C·人物自动聚类：单条写回项（path 标识 + 簇标签，如「人物1」）。
// 写回 DB 时只持久化 label（与 photos 表 person 列一致）。
struct ClusterItem {
    QString path;
    QString label;
};

struct Photo {
    QString path, rel, thumbFile;
    // exif_json 字段（与 web 版 PHOTOS 对象一一对应）
    QString title, date, time, cam, lens, ap, sh, dim, orient;  // orient: landscape/portrait
    QString region, prov, city;
    int    year = 0, iso = 0, rating = 0;
    double focal = 0;
    bool   hasGps = false;
    double la = 0, lo = 0;    // GPS 十进制度（仅 hasGps 时有效；P10 逆地理用）
    bool   missing = false;
    // P16 旧版移植列（photos 表列，非 exif_json）
    QString category;         // CLIP 本地批量分类标签；空 = 未分类
    QString person;           // 人物（逗号分隔）；空 = 无
    bool   priv = false;      // 隐私库标记（native vault：密码门，非破坏）
    QString mark;              // 去重/选片标记（"skip"/"like"/"lock" 等；空=无）
    // 2.28.0 视频：mediaType = "image" | "video"（老行/照片恒 image）；
    // durSec = 时长（秒，0 = 未知）。取自 ffprobe，随列一并读出。
    QString mediaType = QStringLiteral("image");
    double  durSec = 0;
};

struct Kpi {
    int total = 0;
    int yearCount = 0;
    int provCount = 0;
    int minYear = 0, maxYear = 0;
};

class Library {
public:
    // ★线程归属（2.33.0）：本类**无锁**，只能在 GUI 线程读写。异步任务（AI 分类/人脸/
    //   聚类）不得把 Library* 交给 worker —— la_rw 的 QSqlDatabase 连接与线程绑定，
    //   且 mutator 会同步 photos_（模型正按引用读）。正解：worker 只算，写回经
    //   core/AiWriter 投递回 GUI 线程。lastWriteThreadId() 把这条约束变成可断言的事实。
    bool open(const QString& dbPath, QString* err = nullptr);
    const QVector<Photo>& photos() const { return photos_; }
    const Kpi& kpi() const { return kpi_; }
    // 2.33.0：只读 O(1) 路径查找（走已有 pathIx_）。此前 UI 多处为拿到一个 Photo*
    //   线性扫全库（openLightbox 每次点击、灯箱翻图每次按键），D5 建的索引被绕过。
    const Photo* find(const QString& path) const;

    // ---- P16 写回 API（独立读写连接；同时同步内存副本）----
    // 方向 D5：单条 = 长度为 1 的批量（统一走唯一事务入口，禁旁路直写）
    bool setRating(const QString& path, int rating);     // 1..5（0=清除）
    bool setPrivate(const QString& path, bool priv);     // 隐私库标记
    bool setMark(const QString& path, const QString& mark);  // 去重/选片标记（空=清除）

    // ---- 方向 D5：批量写回（单事务原子；任一失败整体回滚，DB 与内存副本同时不变）----
    BatchResult setRatings(const QStringList& paths, int rating);
    BatchResult setPrivates(const QStringList& paths, bool priv);
    BatchResult setMarks(const QStringList& paths, const QString& mark);
    // 方向 C·智能分类：批量写回场景标签（每条 path 各自 label，非统一值）。
    // 复用 rw() 单事务；COMMIT 成功后才同步内存副本（与 applyBatch 语义一致）。
    // 守恒契约：只改 category 列，不增删照片，row 总数不变。
    BatchResult setCategories(const QVector<CatItem>& items);

    // 方向 C·人脸识别（方案A）：批量写回人物标签（每条 path 各自 name，与 setCategories 并列）。
    // 复用 rw() 单事务；COMMIT 成功后才同步内存副本。守恒契约：只改 person 列，不增删照片。
    BatchResult setPersons(const QVector<PersonItem>& items);
    // 人物重命名（GalleryPage 人物 chips 右键）：将全库 person 字段出现的 oldName
    // （逗号分隔 token 级）替换为 newName。newName 为空 = 删除该标签。守恒契约：
    // 只改 person 列、不改其它列、不增删照片行。
    bool renamePerson(const QString& oldName, const QString& newName);
    // QA 反例钩子：setPersons 写库但跳过内存同步（使 GalleryModel 按内存 person 检索失效）。
    static void setForcePersonNoSyncForQa(bool on);

    // ---- 方向 C·人物自动聚类：写回 API（复用 rw() 单事务 + COMMIT 后同步）----
    // 批量写回「代表脸 embedding」：每张待检测照片（person 为空）的 512-d float32 BLOB。
    // 守恒契约：只改 face_emb 列，不增删照片；embedding 不进内存副本（按需 getFaceEmbeddings 读盘）。
    BatchResult setFaceEmbeddings(const QVector<FaceItem>& items);
    // 批量写回「聚类簇标签」：把 FaceCluster 产出的簇 label 写回 person 列。
    // 守恒 + 不覆盖非空：UPDATE 带 WHERE person IS NULL OR person=''；内存同步同样只对空者生效。
    // 复用 g_forcePersonNoSync 反例钩子（写库但跳过内存同步 → 检索失效 RED）。
    BatchResult applyClusters(const QVector<ClusterItem>& items);
    // 读取「person 为空且有 face_emb」的照片（聚类输入集）；缺失 BLOB 的行跳过。
    QVector<FaceItem> getFaceEmbeddings() const;

    // ---- 方向 C·QA 钩子（反例用）----
    // 打开后 setCategories 故意"写库但跳过内存同步"——使 GalleryModel（读内存
    // Photo.category）的按类检索失效。门禁检索断言因此变红，证明断言有效（非空心）。
    static void setForceCategoryNoSyncForQa(bool on);
    // 索引自检（QA 门）：path <-> photos_ 下标双向一致，且大小 == photos_.size()
    bool verifyPathIndex() const;
    // 最近一次**异步写回 mutator**（setCategories/setPersons/setFaceEmbeddings/applyClusters）
    // 的调用线程 id；0 = 从未写过。恒等于 GUI 线程 id 才算守住了上面的线程归属约束
    // （LA_GUI_THREAD_QA 据此判定，并自带「instrument 本身不是空的」负向对照）。
    quintptr lastWriteThreadId() const { return lastWriteThread_.load(std::memory_order_relaxed); }
    // meta 表 KV（隐私库口令 salt/hash 存这里）
    QString metaValue(const QString& key) const;
    bool setMetaValue(const QString& key, const QString& value);

private:
    enum class BatchField { Rating, Private, Mark };
    BatchResult applyBatch(BatchField field, const QStringList& paths, const QVariant& value);

    void rebuildPathIndex();                             // photos_ 变动后唯一重建入口
    Photo* findMutable(const QString& path);             // O(1) 走 pathIx_（禁线性扫描）
    void noteWriteThread();                              // 记录调用线程（atomic 存 QThread*）

    QSqlDatabase rw();                                   // 惰性读写连接「la_rw」
    QVector<Photo> photos_;
    QHash<QString, int> pathIx_;                         // path -> photos_ 下标（D5：批量写回去 O(N²)）
    Kpi kpi_;
    QString dbPath_;
    std::atomic<quintptr> lastWriteThread_{0};   // 异步写回 mutator 的调用线程（见 open() 上方约束）
};
