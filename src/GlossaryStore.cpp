#include "GlossaryStore.h"
#include "GlossarySeeds.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace glossarystore {

QString storeDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
           + "/glossary";
}

QString globalFile()
{
    return storeDir() + "/global.json";
}

QString fileFor(const QString &gameKey)
{
    return storeDir() + "/" + gameKey + ".json";
}

QJsonObject load(SeedType seed)
{
    QDir().mkpath(storeDir());
    QFile f(globalFile());
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        f.close();
        return doc.object();
    }
    // 首次初始化：注入对应内置种子表（Task 6；自进化此后持续扩充该文件）
    QJsonObject seeds;
    QStringList pairs;
    if (seed == SeedType::Adult)
        pairs = glossaryseeds::adult();
    else if (seed == SeedType::Standard)
        pairs = glossaryseeds::standard();
    else
        pairs = glossaryseeds::galgame();
    for (const QString &pair : pairs)
        seeds.insert(pair.section('=', 0, 0), pair.section('=', 1));
    save(seeds); // 种子落盘为全局起点
    return seeds;
}

void save(const QJsonObject &terms)
{
    QDir().mkpath(storeDir());
    QFile f(globalFile());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    f.write(QJsonDocument(terms).toJson(QJsonDocument::Indented));
    f.close();
}

int compileToXuat(SeedType seed, const QString &gameDir, QString *err)
{
    const QJsonObject terms = load(seed);
    QDir().mkpath(gameDir + "/BepInEx/plugins/XUnity.AutoTranslator/Translation");
    const QString path = gameDir
        + "/BepInEx/plugins/XUnity.AutoTranslator/Translation/_Substitutions.txt";

    // 读取用户既有条目（保留，不清空）
    QStringList userLines;
    QFile existing(path);
    if (existing.open(QIODevice::ReadOnly)) {
        const QStringList all = QString::fromUtf8(existing.readAll()).split('\n');
        existing.close();
        for (const QString &line : all) {
            const QString trimmed = line.trimmed();
            if (!trimmed.isEmpty() && !trimmed.startsWith("//") && trimmed.contains('='))
                userLines << trimmed;
        }
    }

    // 全局术语 → 条目（跳过用户词典已有的原文，不重复）
    QStringList out;
    QSet<QString> seenSources;
    for (const QString &l : userLines) {
        out << l;
        seenSources.insert(l.section('=', 0, 0));
    }
    int added = 0;
    for (auto it = terms.begin(); it != terms.end(); ++it) {
        if (seenSources.contains(it.key())) continue;
        out << it.key() + "=" + it.value().toString();
        ++added;
    }

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (err) *err = f.errorString();
        return -1;
    }
    f.write(("// unityTools glossary (auto-compiled; user entries preserved)\n"
             + out.join('\n')).toUtf8());
    f.close();
    return added;
}

QStringList parseEvolutionOutput(const QString &modelOutput)
{
    QStringList out;
    for (const QString &raw : modelOutput.split('\n')) {
        const QString line = raw.trimmed();
        if (!line.contains('=')) continue;
        const QString src = line.section('=', 0, 0).trimmed();
        const QString dst = line.section('=', 1).trimmed();
        if (src.isEmpty() || dst.isEmpty()) continue;
        out << (src + "=" + dst);
    }
    return out;
}


// 一次性迁移：把旧的 per-game JSON 合并进 global.json（Task B）
void migrateFromPerGame()
{
    QDir dir(storeDir());
    if (!dir.exists()) return;
    QJsonObject global = load();
    bool changed = false;
    for (const QFileInfo &fi : dir.entryInfoList(QStringList() << "*.json", QDir::Files)) {
        if (fi.fileName() == "global.json") continue;
        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
        f.close();
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            if (!global.contains(it.key())) {
                global.insert(it.key(), it.value());
                changed = true;
            }
        }
        fi.dir().remove(fi.fileName());
    }
    if (changed) save(global);
}
} // namespace glossarystore
