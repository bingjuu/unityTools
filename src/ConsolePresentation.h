#pragma once
#include <QJsonObject>
#include <QStringList>

namespace consoleui {
inline QString text(int language, const char *chinese, const char *english)
{
    return QString::fromUtf8(language == 1 ? chinese : english);
}
inline QString state(const QString &value, int language = 1)
{
    if (value == "planning") return text(language, "正在确定目标", "Planning");
    if (value == "queued") return text(language, "等待执行", "Queued");
    if (value == "executing") return text(language, "正在执行", "Executing");
    if (value == "reading" || value == "read") return text(language, "正在读取数据", "Reading data");
    if (value == "awaiting-selection") return text(language, "请先选择目标", "Select a target");
    if (value == "writing") return text(language, "正在修改数值", "Writing value");
    if (value == "verifying") return text(language, "正在重新读取确认", "Verifying readback");
    if (value == "verified") return text(language, "重新读取一致，确认生效", "Readback matches");
    if (value == "completed") return text(language, "已完成", "Completed");
    if (value == "submitted") return text(language, "已提交给游戏，尚未确认结果", "Submitted; outcome unverified");
    if (value == "text-set-only") return text(language, "只设置了文本，未执行命令", "Text was set; command not executed");
    if (value == "no-runtime-target-found") return text(language, "没有找到运行时目标", "No runtime target found");
    if (value == "no-identified-game-console") return text(language, "没有找到可确认的游戏命令入口", "No identified game command entry");
    if (value == "executed") return text(language, "游戏方法已返回，业务结果尚未确认", "Method returned; outcome unverified");
    if (value == "cancelled") return text(language, "已停止后续操作", "Cancelled");
    if (value == "not-executed") return text(language, "未执行修改", "Not executed");
    if (value == "executed-unverified") return text(language, "修改已执行，但未能确认结果", "Executed but unverified");
    if (value == "unknown") return text(language, "结果暂时无法确认，不会重复修改", "Unknown; mutation will not be replayed");
    if (value == "failed") return text(language, "操作未完成", "Failed");
    return text(language, "等待任务", "Ready");
}
inline QString error(const QString &value, int language = 1)
{
    if (language != 1) return value;
    if (value.contains("stale-reference") || value.contains("stale-parent") || value.contains("stale-collection")) return QStringLiteral("目标或集合已变化，请重新查找并读取。");
    if (value.contains("readonly") || value.contains("member-not-observed")) return QStringLiteral("该成员尚未确认可写，请重新查看目标。");
    if (value.contains("no-runtime-target") || value.contains("no-playdata")) return QStringLiteral("没找到对应的游戏数据。");
    if (value.contains("session") || value.contains("bridge-closed")) return QStringLiteral("游戏控制连接已变化，请重新连接。");
    if (value.contains("no-identified") || value.contains("identified-console")) return QStringLiteral("没找到游戏内命令入口，不影响数值读取和修改。");
    if (value.contains("deadline") || value.contains("still-executing") || value.contains("write-result-unknown")) return QStringLiteral("结果暂时无法确认，不会重复修改。");
    if (value.contains("read-back-mismatch")) return QStringLiteral("重新读取与目标值不同，不能确认修改成功。");
    if (value.contains("overflow") || value.contains("precision") || value.contains("value-type") || value.contains("type-mismatch")) return QStringLiteral("这个值不符合目标类型，未执行修改。");
    if (value.contains("tool-not-allowed") || value.contains("inspection-required") || value.contains("not-observed")) return QStringLiteral("目标或操作尚未确认，不会猜测执行。");
    if (value.contains("invalid-model") || value.contains("invalid-tool")) return QStringLiteral("模型返回的工具调用格式不正确。");
    if (value.contains("Error transferring") || value.contains("Connection") || value.contains("Host")) return QStringLiteral("模型服务请求失败，详细原因可在详情中查看。");
    return QStringLiteral("操作未完成，详细原因可在详情中查看。");
}
inline QString capabilitySummary(const QJsonObject &capabilities, int language = 1)
{
    QStringList lines;
    const auto available = [&](const char *name) { return capabilities.value(name).toObject().value("status") == "available"; };
    if (available("members.read") && available("members.write")) lines << text(language, "可以读取和修改游戏数据", "Game data can be read and changed");
    else if (available("members.read")) lines << text(language, "可以读取游戏数据", "Game data can be read");
    else lines << text(language, "正在确认可用的数据操作", "Checking data access");
    lines << (available("gameConsole") ? text(language, "游戏内命令入口可用", "Game command entry available") : text(language, "没找到游戏内命令入口，不影响数值操作", "No game command entry; data access remains available"));
    return lines.join(QStringLiteral("；"));
}
inline QString valueText(const QJsonObject &value, int language = 1)
{
    if (value.value("null").toBool()) return text(language, "空值", "null");
    if (value.contains("objectId")) return text(language, "对象引用", "Object reference");
    return value.value("text").toString(text(language, "尚未读取", "Not read"));
}
inline QString evidenceSummary(const QJsonObject &evidence, int language = 1)
{
    if (evidence.contains("target")) return text(language, "原值：%1 · 目标：%2 · 重新读取：%3。%4", "Before: %1 · target: %2 · readback: %3. %4")
        .arg(valueText(evidence.value("before").toObject(), language), valueText(evidence.value("target").toObject(), language),
             valueText(evidence.value("after").toObject(), language), state(evidence.value("state").toString(), language));
    if (evidence.contains("error")) return error(evidence.value("error").toString(), language);
    if (evidence.contains("value")) return text(language, "读取到的值：%1", "Read value: %1").arg(valueText(evidence.value("value").toObject(), language));
    if (evidence.contains("before")) return text(language, "读取到的值：%1", "Read value: %1").arg(valueText(evidence.value("before").toObject(), language));
    return state(evidence.value("state").toString(), language);
}
}
