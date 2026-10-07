#pragma once
#include <QString>
#include <QStringList>
#include <QJsonObject>

// ==========================================
// GlossaryStore：术语表全局持久层（纯逻辑，无网络无 UI，可单测）
// - 全局存储：%LOCALAPPDATA%/unityTools/glossary/<exe名>.json（{"术语":"译名"}）
// - 编译器：合并写入游戏目录 XUAT 原生词典 _Substitutions.txt（不清空用户既有条目）
// ==========================================
namespace glossarystore {

QString storeDir();
QString globalFile();                                 // 全局术语表 global.json（跨游戏共享）

// JSON 读写（格式 {"术语":"译名",...}；损坏文件视为空表）
enum class SeedType { Adult, Galgame, Standard };
QJsonObject load(SeedType seed = SeedType::Galgame); // 首次=种子落盘，此后读 global.json
void save(const QJsonObject &terms); // 追加式写入全局表

// 合并写入游戏目录 XUAT 词典；返回词典总条数（含用户既有条目）
int compileToXuat(SeedType seed, const QString &gameDir, QString *err); // 合并 种子表(seed)+全局表 → 游戏词典

// 自进化整理模型输出的解析器：只收"原文=译文"行，其余丢弃
QStringList parseEvolutionOutput(const QString &modelOutput); // 返回 "原文=译文" 行列表

} // namespace glossarystore
