#include <QApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QFileInfo>
#include "FloatingVideoPlayer.h"

/**
 * @brief 主程序入口
 * 
 * 支持命令行参数：
 * - 直接传入视频文件路径进行播放
 * - --help 显示帮助信息
 * 
 * 使用示例：
 * - LoopVideoPlayer                    启动空白播放器
 * - LoopVideoPlayer video.mp4          启动并播放 video.mp4
 */
int main(int argc, char *argv[])
{
    // 创建应用程序
    QApplication app(argc, argv);
    app.setApplicationName("Loop Video Player");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("LoopPlayer");

    // 设置应用程序样式
    app.setStyle("Fusion");

    // 全局样式表
    app.setStyleSheet(R"(
        QToolTip {
            background-color: #1a1a2e;
            color: white;
            border: 1px solid #3a3a5a;
            border-radius: 4px;
            padding: 5px;
        }
    )");

    // 命令行解析
    QCommandLineParser parser;
    parser.setApplicationDescription("悬浮视频循环播放器 - 一个轻量级的视频循环播放工具");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("file", "要播放的视频文件路径", "[video file]");

    parser.process(app);

    // 创建播放器窗口
    FloatingVideoPlayer player;
    player.show();

    // 如果提供了文件参数，打开视频
    const QStringList args = parser.positionalArguments();
    if (!args.isEmpty()) {
        QString filePath = args.first();
        QFileInfo fileInfo(filePath);
        
        if (fileInfo.exists() && fileInfo.isFile()) {
            player.openVideo(fileInfo.absoluteFilePath());
        } else {
            qWarning() << "文件不存在:" << filePath;
        }
    }

    return app.exec();
}

