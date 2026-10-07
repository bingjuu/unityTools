#pragma once
#include <QJsonObject>

inline bool sameTypedUnityValue(const QJsonObject &expected, const QJsonObject &actual)
{
    if (!expected["type"].isString() || !actual["type"].isString()
        || !expected["null"].isBool() || !actual["null"].isBool()) return false;
    for (const auto *field : {"type", "projectionType", "bits", "signed", "enum", "null", "identityId"}) {
        if (expected.contains(field) != actual.contains(field) || expected[field] != actual[field]) return false;
    }
    if (expected["null"].toBool()) return true;
    if (expected.contains("identityId")) return !expected["identityId"].toString().isEmpty();
    if (expected.contains("objectId") || actual.contains("objectId"))
        return !expected["objectId"].toString().isEmpty() && expected["objectId"] == actual["objectId"];
    return expected["text"].isString() && expected["text"] == actual["text"];
}
