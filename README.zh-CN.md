# UnityTools

[English](README.md) | 简体中文

MTool 式 Unity 游戏翻译与运行时游戏控制台。支持 Unity Mono / IL2CPP 双引擎，内置基于工具调用（tool calling）的 Unity Agent，可用自然语言读写游戏数值。

> Fork 自 [Moil's XUnity-LLMTranslateGUI](https://github.com/sorrowmoil/Moil-s-XUnity-LLMTranslateGUI)（MIT）。与上游的差异见下文；许可证与第三方声明见 [NOTICE.md](NOTICE.md)。

## 功能总览

### 翻译

- **一键注入启动**：拖入游戏 exe → 自动识别引擎与架构（读 PE 头判断 Mono/IL2CPP、x86/x64）→ 按勾选部署 BepInEx + XUAT 翻译 + UniversalUnityDemosaics（去马赛克）+ RuntimeUnityEditor → 启动游戏
- **两阶段 IL2CPP 启动**：IL2CPP 游戏首次启动自动生成 interop 互操作组件并自动重启，注入确认成功才进入游戏，失败明确报错
- **真并发翻译代理**：内置 `127.0.0.1:6800` 代理，游戏内 XUAT 的请求统一转发到你配置的任意 OpenAI 兼容大模型（本地 Sakura / DeepSeek / GPT 等）。自研端点插件替换 XUAT 5.6.2 内置 HttpEndpoint 的串行实现（其 `MaxConcurrentTranslations` 键在该版本是死设置），按批次打包请求实现真并发
- **关闭翻译不卸载**：翻译偏好关闭时保留 XUAT/Bridge 注入，写入 `Passthrough` 端点，下次启动生效；多游戏共享代理时按会话归属管理，一个游戏退出不误停其他游戏
- **官方组件自动获取**：部署所需的 BepInEx / XUAT / UUD / RUE 优先从本地 vendor 目录读取，缺失时自动从版本锚定的官方直链下载（带镜像加速）
- **ASCII 启动路径**：中文/日文游戏目录自动解析为纯英文路径启动（8.3 短路径 / junction），原目录不受影响
- **存档定位**：自动扫描游戏目录与 Unity persistentDataPath 的存档位置，一键打开

### 游戏控制台

- **独立非模态窗口**：游戏注入握手（sessionId / PID / runtime / 令牌校验）后自动打开；关闭窗口不结束游戏；游戏退出或切换时自动清理旧会话、拒绝迟到响应
- **Unity 运行时对象访问**：查找场景 GameObject / Component / 已加载游戏静态类型；查看字段、属性、集合条目（List / Dictionary / 数组）与方法描述——inspect 不触发属性 getter
- **验证式写入**：读取 → 带快照写入 → 独立读回确认。精确处理 Int64 / UInt64 / decimal 文本（不经 JSON double 舍入）、null 与空串区分、enum / bool；struct 沿父路径写回而不是改副本；集合按键/索引快照校验，父对象被替换或集合变化时拒绝写入
- **显式数据入口**：数据来源（`GetCurrentPlayData` 提供者）与游戏内命令入口由能力报告列出、由你选定；多候选暂停等待选择，绝不取"第一个同名方法"
- **Mono / IL2CPP 同一套操作**：底层分别是反射与 Il2CppInterop 生成投影，所有 Unity 访问在游戏主线程执行，大集合发现按帧让出

### Unity Agent

- **自然语言改数值**：在控制台输入如"把金钱改到9999"，Agent 走 OpenAI 兼容 `tools/tool_calls` 协议，只调用 UT 声明的工具
- **固定安全流程**：查找 → 查看 → 读原值 → 写入 → 独立读回；`set`（设为）与 `add`（增加）严格区分；读回一致才算成功
- **防假成功**：无 verified 证据的任务一律报告失败；结果未知（超时/断连）时查询状态并重读实际值，绝不重放写入（尤其 add 会重复加钱）
- **多候选暂停**：目标不唯一时列出候选等你选定；禁止内存/地址/Shell/任意代码类工具
- 翻译 API 与 Agent API 独立配置；密钥只保存在本机

## 与上游（fork 对象）的差异

### 新增 / 改进

| 方面 | 上游 | 本项目 |
|---|---|---|
| 作弊能力 | 启动页内嵌面板：playData 表格 + 向游戏控制台 InputField 透传文本，无法证明命令真正执行 | 独立游戏控制台窗口：Unity 运行时对象级 find/inspect/read/write/invoke，全部以独立读回验证为准 |
| Bridge | 单一 BepInEx 5 插件，固定 6801 端口，单会话 | Mono（BepInEx 5）与 IL2CPP（BepInEx 6 + Il2CppInterop）双构建；每会话动态回环端口 + 令牌鉴权 + 迟到响应拒绝，多游戏并行互不干扰 |
| 写入语义 | 直接改值，无验证 | 写前快照校验 + 写后独立读回；`not-executed / executed-unverified / verified / unknown` 四态如实区分 |
| playData | 反射扫到第一个 `GetCurrentPlayData` 就用 | 显式提供者发现与选择（能力报告列出候选），只读快照明确标注，不猜测 |
| 翻译并发 | XUAT 内置 HttpEndpoint 串行 | 自研端点插件 + 批处理实现真并发；代理生命周期加固（绑定先行、安全停止、退出不冻结） |
| 翻译开关 | 无（或直接停代理，误伤其他游戏） | 偏好制：关闭翻译保留注入，下次启动生效；共享代理按 owner 归属管理 |
| Unity Agent | 无 | 完整工具调用循环，中文任务提示词，防假成功语义 |

### 删减 / 清理

- 移除 GoogleTranslate 等端点切换路线：翻译端点锁定为自研 `UnityToolsTranslate` → 本机代理
- 移除旧"命令透传"的 `text-set-only = 成功` 假成功语义
- 移除对 `GetCurrentPlayData` 的首匹配猜测和按名字猜控制台输入框的启发式
- 公开快照不含：内部测试与设计文档、游戏 fixture、编译产物、第三方引用程序集（构建时本地提供）、Python 遗留变体
- Windows 可执行程序不再入库，改为 GitHub Release 资产分发

## 技术栈

- **桌面应用**：Qt 6（Widgets / Network / Concurrent）、C++17、MSVC x64、CMake；测试用 QtTest
- **翻译代理**：[cpp-httplib](https://github.com/yhirose/cpp-httplib)（MIT）线程池 + 请求作用域网络对象生命周期管理
- **JSON**：[nlohmann/json](https://github.com/nlohmann/json)（MIT，C++ 侧）；[SimpleJSON](https://github.com/MParticle-Solutions/SimpleJSON)（MIT，C# 侧，随仓库捆绑源码）
- **Mono Bridge**：C# / BepInEx 5 插件，System.Reflection 对象访问，csc 单文件编译
- **IL2CPP Bridge**：C# / BepInEx 6 + [Il2CppInterop](https://github.com/BepInEx/Il2CppInterop) 生成投影，.NET 6，dotnet 构建
- **Unity Agent**：OpenAI Chat Completions 原生 `tools/tool_calls` 协议；完整保留 assistant 消息（含 DeepSeek `reasoning_content`），工具结果以匹配的 `tool_call_id` 回填
- **Windows 集成**：PE 头解析识别引擎/架构、NTFS 8.3 短路径与 junction 处理非 ASCII 启动路径、System32 bsdtar 解压官方组件包、机器级字体注册（带系统字体兜底链）

## 运行边界

- 只暴露 Unity 运行时对象与已观察的成员/方法；不实现 Cheat Engine、进程内存扫描、地址读写、指针链、任意 C#、Shell 执行或动态程序集加载
- 写入只有独立读回一致才算成功；未知、过期、只读、歧义或不支持的目标都如实报告，绝不自动重放修改
- 不存在通用游戏命令语言；命令适配器只在发现并选定具体 API 时可用，提交不等于业务已验证
- XUAT 5.6.2 的关闭翻译保留注入，采用下次启动 Passthrough 偏好，不做不安全的运行时可逆切换

## 构建前提

- Windows 10/11、Visual Studio C++ 工具链、CMake、Qt 6（Widgets/Network/Core/Concurrent）
- IL2CPP Bridge 变体需要 .NET 6 SDK
- Mono Bridge 编译需要某个 Unity 游戏的 `Managed` 目录；IL2CPP Bridge 编译需要目标游戏的 `BepInEx/core` 与 `BepInEx/interop` 目录
- 官方引用程序集由使用者本地放置在 `bridge/refs/`（本仓库不分发）：BepInEx 与 XUnity.AutoTranslator 的引用 DLL
- 官方部署包通过 `UNITYTOOLS_VENDOR_DIR` 本地提供；应用也可使用内置的版本锚定官方下载直链

配置/构建示例：

```powershell
cmake -S . -B build `
  -G "Visual Studio 18 2026" -A x64 `
  -DQt6_DIR="C:/Qt/6.12.0/msvc2022_64/lib/cmake/Qt6" `
  -DGAME_MANAGED="C:/Games/MyUnityGame/MyUnityGame_Data/Managed" `
  -DIL2CPP_INTEROP="C:/Games/MyIl2CppGame/BepInEx/interop"
cmake --build build --config Release
```

未提供引用程序集时，Qt 应用仍可构建；可选的 Bridge 目标会跳过并给出配置警告。

## Release 包

终端用户请使用 [Windows Release 资产](https://github.com/bingjuu/unityTools/releases)。包内含编译好的 `unityTools.exe`、Qt 与 MSVC 运行库、Bridge/翻译端点二进制和可选内置字体；不含游戏、存档、API 密钥、BepInEx/XUAT 运行时包或私有引用程序集。

## 许可证

MIT。参见 [LICENSE](LICENSE) 与 [NOTICE.md](NOTICE.md)。
