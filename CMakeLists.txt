cmake_minimum_required(VERSION 3.16)

# Project Name and Language
# 项目名称与语言 (C++)
project(unityTools LANGUAGES CXX)

# Generate compile_commands.json (Useful for IDEs like VSCode/Clangd for intellisense)
# 生成 compile_commands.json (对于 VSCode/Clangd 等 IDE 的代码补全非常有用)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ==============================================================================
# ⚠️ CRITICAL CONFIGURATION / 关键配置 ⚠️
#
# Qt Installation Path:
# You must modify this path to match your local Qt installation.
# Ensure you use forward slashes '/', not backslashes '\'.
#
# Qt 安装路径：
# 您必须修改此路径以匹配您本地的 Qt 安装位置。
# 请确保使用正斜杠 '/'，不要使用反斜杠 '\'。
# ==============================================================================
# Qt 路径经命令行 -DQt6_DIR 传入（见 AGENTS.md 构建命令），此处不设默认

# ==============================================================================
# C++ Standard & Qt Build Tools
# C++ 标准与 Qt 构建工具
# ==============================================================================

# Require C++ 17
# 强制要求 C++ 17 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt Meta-Object Compiler (MOC) automation
# Automatically runs moc for files containing Q_OBJECT
# 自动处理元对象编译器 (MOC)，用于处理包含 Q_OBJECT 的文件
set(CMAKE_AUTOMOC ON)

# Qt Resource Compiler (RCC) automation (.qrc files)
# 自动处理资源编译器 (RCC)
set(CMAKE_AUTORCC ON)

# Qt UI Compiler (UIC) automation (.ui files)
# 自动处理 UI 编译器 (UIC)
set(CMAKE_AUTOUIC ON)

# ==============================================================================
# Dependencies / 依赖项
# ==============================================================================

# Find required Qt6 modules: Widgets (GUI), Network (HTTP), Core (Base)
# 查找必要的 Qt6 模块：Widgets (界面), Network (网络), Core (核心)
find_package(Qt6 REQUIRED COMPONENTS Widgets Network Core Concurrent)

# ==============================================================================
# Target Definition / 目标定义
# ==============================================================================

# Define the executable and list all source files
# 定义可执行文件并列出所有源文件
add_executable(unityTools
    src/main.cpp
    src/ConfigManager.h src/ConfigManager.cpp
    src/TranslationServer.h src/TranslationServer.cpp
    src/MainWindow.h src/MainWindow.cpp
    src/httplib.h 
    src/json.hpp
    src/unityTools.ico
    src/GlossaryManager.h
    src/RegexManager.h
    src/HudWindow.h src/HudWindow.cpp
    src/TokenManager.h src/TokenManager.cpp
    src/LoadingOverlay.h
    src/LogManager.h
    src/XuaConfigHijacker.h
    src/Deployment.h src/Deployment.cpp
    src/GameLauncherWidget.h src/GameLauncherWidget.cpp
    src/GlossaryStore.h src/GlossaryStore.cpp
    src/CheatPanelWidget.h src/CheatPanelWidget.cpp
    src/CheatCenterWindow.h src/CheatCenterWindow.cpp
    src/CheatBridgeClient.h src/CheatBridgeClient.cpp
    src/UnityAgentController.h src/UnityAgentController.cpp
    logo.rc
)

# Add 'src' directory to include headers
# Essential for verifying #include "json.hpp" works correctly
# 将 'src' 目录添加到头文件搜索路径
# 这对于确保 #include "json.hpp" 能被正确找到至关重要
target_include_directories(unityTools PRIVATE ${CMAKE_SOURCE_DIR}/src)

# Define Windows 10 target for httplib compatibility
# 为 httplib 兼容性定义 Windows 10 目标
if(WIN32)
    target_compile_definitions(unityTools PRIVATE _WIN32_WINNT=0x0A00)
endif()

# Link against Qt libraries
# 链接 Qt 库
target_link_libraries(unityTools PRIVATE
    Qt6::Widgets
    Qt6::Network
    Qt6::Core
    Qt6::Concurrent
    version
    shell32
)





# ==============================================================================
# Platform Specific Settings / 平台特定设置
# ==============================================================================

# Windows: Console Window Settings
# Windows 下控制台窗口设置
if(WIN32)
    # Uncomment the following line to HIDE the console window (Production Mode)
    # Keep commented to SEE logs in the console (Debug Mode)
    # 取消下方注释以“隐藏”控制台黑框 (发布模式)
    # 保持注释状态以便在黑框中查看日志 (调试模式)
    
    set_target_properties(unityTools PROPERTIES WIN32_EXECUTABLE ON)
endif()


# Optional Unity runtime bridge builds.
# Reference assemblies are intentionally supplied from outside this repository.
set(GAME_MANAGED "" CACHE PATH "Unity Managed directory for Mono bridge compilation")
set(IL2CPP_INTEROP "" CACHE PATH "Unity BepInEx/interop directory for IL2CPP bridge compilation")
find_program(CSC_EXE csc.exe
    PATHS "C:/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/Roslyn"
          "C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/Roslyn")
if(CSC_EXE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/bridge/refs/BepInEx.dll" AND EXISTS "${GAME_MANAGED}/UnityEngine.dll")
    add_custom_target(bridge ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/bridge
        COMMAND ${CSC_EXE} -nologo -target:library -out:${CMAKE_CURRENT_BINARY_DIR}/bridge/UnityToolsBridge.dll
                -r:${CMAKE_CURRENT_SOURCE_DIR}/bridge/refs/BepInEx.dll
                -r:${GAME_MANAGED}/UnityEngine.dll
                $<$<NOT:$<STREQUAL:"${GAME_MANAGED}","">>:-r:${GAME_MANAGED}/UnityEngine.CoreModule.dll>
                ${CMAKE_CURRENT_SOURCE_DIR}/bridge/UnityToolsBridge.cs
                ${CMAKE_CURRENT_SOURCE_DIR}/bridge/BridgeProtocol.cs
                ${CMAKE_CURRENT_SOURCE_DIR}/bridge/ObjectAccess.cs
                ${CMAKE_CURRENT_SOURCE_DIR}/bridge/NumericValue.cs
                ${CMAKE_CURRENT_SOURCE_DIR}/bridge/TranslationState.cs
                ${CMAKE_CURRENT_SOURCE_DIR}/bridge/GameEntries.cs
                ${CMAKE_CURRENT_SOURCE_DIR}/bridge/vendor/SimpleJson.cs
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/bridge/UnityToolsBridge.cs ${CMAKE_CURRENT_SOURCE_DIR}/bridge/BridgeProtocol.cs ${CMAKE_CURRENT_SOURCE_DIR}/bridge/ObjectAccess.cs ${CMAKE_CURRENT_SOURCE_DIR}/bridge/NumericValue.cs ${CMAKE_CURRENT_SOURCE_DIR}/bridge/TranslationState.cs ${CMAKE_CURRENT_SOURCE_DIR}/bridge/GameEntries.cs ${CMAKE_CURRENT_SOURCE_DIR}/bridge/vendor/SimpleJson.cs)
    add_dependencies(unityTools bridge)
endif()
if(CSC_EXE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/bridge/refs/XUnity.AutoTranslator.Plugin.Core.dll" AND EXISTS "${GAME_MANAGED}/UnityEngine.dll")
    add_custom_target(unityToolsTranslateMono ALL
        COMMAND ${CSC_EXE} -nologo -target:library -out:${CMAKE_CURRENT_BINARY_DIR}/bridge/UnityToolsTranslate.dll
                -r:${CMAKE_CURRENT_SOURCE_DIR}/bridge/refs/XUnity.AutoTranslator.Plugin.Core.dll
                -r:${GAME_MANAGED}/UnityEngine.dll
                $<$<NOT:$<STREQUAL:"${GAME_MANAGED}","">>:-r:${GAME_MANAGED}/UnityEngine.CoreModule.dll>
                ${CMAKE_CURRENT_SOURCE_DIR}/bridge/UnityToolsTranslate.cs
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/bridge/UnityToolsTranslate.cs)
    add_dependencies(unityTools unityToolsTranslateMono)
endif()
find_program(DOTNET_EXE dotnet PATHS "C:/Program Files/dotnet")
get_filename_component(IL2CPP_BEPINEX "${IL2CPP_INTEROP}" DIRECTORY)
set(IL2CPP_CORE "${IL2CPP_BEPINEX}/core" CACHE PATH "BepInEx 6 IL2CPP core directory")
if(DOTNET_EXE AND EXISTS "${IL2CPP_CORE}/BepInEx.Unity.IL2CPP.dll" AND EXISTS "${IL2CPP_INTEROP}/UnityEngine.CoreModule.dll")
    add_custom_target(unityToolsBridgeIl2cpp ALL
        COMMAND ${DOTNET_EXE} build ${CMAKE_CURRENT_SOURCE_DIR}/bridge/UnityToolsBridge.IL2CPP.csproj -c Release -o ${CMAKE_CURRENT_BINARY_DIR}/bridge -p:BaseIntermediateOutputPath=${CMAKE_CURRENT_BINARY_DIR}/bridge/obj/ -p:IL2CPP_INTEROP=${IL2CPP_INTEROP} -p:IL2CPP_CORE=${IL2CPP_CORE}
        DEPENDS bridge/UnityToolsBridgeIl2Cpp.cs bridge/Il2CppObjectAccess.cs bridge/NumericValue.cs bridge/GameEntries.cs bridge/TranslationState.cs bridge/BridgeProtocol.cs bridge/HttpListenerHost.cs bridge/vendor/SimpleJson.cs)
    add_dependencies(unityTools unityToolsBridgeIl2cpp)
endif()
if(DOTNET_EXE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/bridge/refs/il2cpp/XUnity.AutoTranslator.Plugin.Core.dll" AND EXISTS "${IL2CPP_INTEROP}/UnityEngine.CoreModule.dll")
    add_custom_target(unityToolsTranslateIl2cpp ALL
        COMMAND ${DOTNET_EXE} build ${CMAKE_CURRENT_SOURCE_DIR}/bridge/UnityToolsTranslate.IL2CPP.csproj -c Release -o ${CMAKE_CURRENT_BINARY_DIR}/bridge -p:BaseIntermediateOutputPath=${CMAKE_CURRENT_BINARY_DIR}/bridge/translate-obj/ -p:MSBuildProjectExtensionsPath=${CMAKE_CURRENT_BINARY_DIR}/bridge/translate-obj/ -p:IL2CPP_INTEROP=${IL2CPP_INTEROP}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/bridge/UnityToolsTranslate.cs)
    add_dependencies(unityTools unityToolsTranslateIl2cpp)
endif()
