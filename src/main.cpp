// --- main.cpp ---
// 程序入口：初始化应用并启动主窗口（UnityTools 标准外观）
// 特殊模式：--install-font = 由设置页「管理员安装」以 UAC 提权拉起，装完即退（无窗口）

#include <QApplication>
#include <QCoreApplication>
#include <cstring>
#include "MainWindow.h"
#include "Deployment.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setStyle("Fusion");
    // 源头禁用本土化数字：zh locale 会把 30/1000 渲染成苏州码子 〣〇/〡〇〇〇。
    // 全局默认 C locale，之后新建的任何控件都不必再逐个 setLocale
    QLocale::setDefault(QLocale::c());

    if (argc >= 2 && std::strcmp(argv[1], "--install-font") == 0) {
        deployment::installFontMachineWide();
        return 0;
    }

    MainWindow mainWin;
    mainWin.show();

    return app.exec();
}
