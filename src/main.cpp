#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include "FloatingVideoPlayer.h"

/**
 * @brief 主程序入口
 * 
 * Loop Video Player - 悬浮视频循环播放器
 * 基于 libmpv，支持几乎所有视频格式
 * 
 * 使用方式：
 * - LoopVideoPlayer              启动空白播放器
 * - LoopVideoPlayer video.mp4    启动并播放视频
 */
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("Loop Video Player");
    app.setApplicationVersion("2.0.0");
    app.setOrganizationName("LoopPlayer");
    app.setStyle("Fusion");

    // 全局样式
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
    parser.setApplicationDescription("悬浮视频循环播放器 - 基于 libmpv，支持几乎所有视频格式");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("file", "视频文件路径", "[video file]");
    parser.process(app);

    // 创建播放器
    FloatingVideoPlayer player;
    player.show();

    // 打开命令行指定的文件
    const QStringList args = parser.positionalArguments();
    if (!args.isEmpty()) {
        QFileInfo fileInfo(args.first());
        if (fileInfo.exists() && fileInfo.isFile()) {
            player.openVideo(fileInfo.absoluteFilePath());
        }
    }

    return app.exec();
}
