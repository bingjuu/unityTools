#pragma once

// 应用版本号单一来源：设置页「检测更新」显示与在线更新比较都使用此值。
// 发版清单（online-update spec §8）：发版前更新此处，打 tag 并上传
// zip + SHA256SUMS.txt + latest.json（含同一版本号）。
inline const char *const kAppVersion = "1.0.2";
