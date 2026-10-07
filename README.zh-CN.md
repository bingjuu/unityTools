# UnityTools

[English](README.md) | 简体中文

MTool 式 Unity 游戏翻译与运行时游戏控制台。支持 Unity Mono / IL2CPP 双引擎，内置基于工具调用（tool calling）的 Unity Agent，可用自然语言读写游戏数值。

> Fork 自 [Moil's XUnity-LLMTranslateGUI](https://github.com/sorrowmoil/Moil-s-XUnity-LLMTranslateGUI)（MIT）。详见 [NOTICE.md](XUnity-Moil-LLMTranslateGUI-C++/NOTICE.md)。

## 仓库内容

- Qt 6 / C++17 桌面应用源码
- Mono（BepInEx 5）与 IL2CPP（BepInEx 6）双 Bridge 源码
- Unity 运行时对象 find / inspect / read / write / invoke 协议
- 会话级游戏控制台窗口与 Unity Agent 编排
- 翻译端点源码与 Passthrough / 下次启动偏好处理
- MIT 许可证与第三方源码声明

内部测试源码、设计文档、游戏 fixture、编译产物、私有引用程序集与本地构建元数据有意不包含在本公开源码快照中。Windows 可执行程序通过 [GitHub Release](https://github.com/bingjuu/unityTools/releases) 资产单独分发。

## 运行边界

- 游戏控制台只暴露 Unity 运行时对象与已观察的成员/方法；不实现 Cheat Engine、进程内存扫描、地址读写、指针链、任意 C#、Shell 执行或动态程序集加载
- 写入只有独立读回一致才算成功；未知、过期、只读、歧义或不支持的目标都会如实报告，绝不自动重放修改
- 不存在通用游戏命令语言；命令适配器只在发现并选定具体 API 时可用
- XUAT 5.6.2 的关闭翻译保留注入，采用下次启动 Passthrough 偏好，不做不安全的运行时可逆切换

## 构建前提

- Windows 10/11、Visual Studio C++ 工具链、CMake、Qt 6（Widgets/Network/Core/Concurrent）
- IL2CPP Bridge 变体需要 .NET 6 SDK
- Mono Bridge 编译需要某个 Unity 游戏的 `Managed` 目录
- IL2CPP Bridge 编译需要目标游戏的 `BepInEx/core` 与 `BepInEx/interop` 目录
- 官方引用程序集由使用者本地放置在 `XUnity-Moil-LLMTranslateGUI-C++/bridge/refs/`（本仓库不分发）：BepInEx 与 XUnity.AutoTranslator 的引用 DLL
- 官方部署包通过 `UNITYTOOLS_VENDOR_DIR` 本地提供；应用也可使用内置的版本锚定官方下载直链

配置/构建示例：

```powershell
cmake -S XUnity-Moil-LLMTranslateGUI-C++ -B XUnity-Moil-LLMTranslateGUI-C++/build `
  -G "Visual Studio 18 2026" -A x64 `
  -DQt6_DIR="C:/Qt/6.12.0/msvc2022_64/lib/cmake/Qt6" `
  -DGAME_MANAGED="C:/Games/MyUnityGame/MyUnityGame_Data/Managed" `
  -DIL2CPP_INTEROP="C:/Games/MyIl2CppGame/BepInEx/interop"
cmake --build XUnity-Moil-LLMTranslateGUI-C++/build --config Release
```

未提供引用程序集时，Qt 应用仍可构建；可选的 Bridge 目标会跳过并给出配置警告。

## Release 包

终端用户请使用 [Windows Release 资产](https://github.com/bingjuu/unityTools/releases)。包内含编译好的 `unityTools.exe`、Qt 运行库、Bridge/翻译端点二进制和可选内置字体；不含游戏、存档、API 密钥、BepInEx/XUAT 运行时包或私有引用程序集。

## 许可证

MIT。参见 [LICENSE](LICENSE) 与 [NOTICE.md](XUnity-Moil-LLMTranslateGUI-C++/NOTICE.md)。
