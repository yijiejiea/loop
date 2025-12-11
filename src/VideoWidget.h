#ifndef VIDEOWIDGET_H
#define VIDEOWIDGET_H

#include <QWidget>
#include <QImage>
#include <QTimer>
#include "FFmpegPlayer.h"

/**
 * @brief 视频渲染组件
 * 
 * 使用 FFmpeg 进行视频播放，支持几乎所有视频格式。
 * 通过 QPainter 进行渲染。
 */
class VideoWidget : public QWidget
{
    Q_OBJECT

public:
    explicit VideoWidget(QWidget *parent = nullptr);
    ~VideoWidget();

    /**
     * @brief 加载并播放视频文件
     * @param filename 视频文件路径
     */
    void loadFile(const QString &filename);

    /**
     * @brief 播放
     */
    void play();

    /**
     * @brief 暂停
     */
    void pause();

    /**
     * @brief 切换播放/暂停
     */
    void togglePause();

    /**
     * @brief 停止播放
     */
    void stop();

    /**
     * @brief 设置音量
     * @param volume 音量 (0-100)
     */
    void setVolume(int volume);

    /**
     * @brief 获取音量
     */
    int volume() const;

    /**
     * @brief 设置循环播放
     * @param loop true 为无限循环
     */
    void setLoop(bool loop);

    /**
     * @brief 跳转到指定位置
     * @param seconds 秒数
     */
    void seek(double seconds);

    /**
     * @brief 获取当前播放位置（秒）
     */
    double position() const;

    /**
     * @brief 获取视频总时长（秒）
     */
    double duration() const;

    /**
     * @brief 是否正在播放
     */
    bool isPlaying() const;

    /**
     * @brief 是否已暂停
     */
    bool isPaused() const;

signals:
    /**
     * @brief 播放位置改变
     */
    void positionChanged(double seconds);

    /**
     * @brief 时长改变
     */
    void durationChanged(double seconds);

    /**
     * @brief 播放状态改变
     */
    void playbackStateChanged(bool playing);

    /**
     * @brief 文件加载完成
     */
    void fileLoaded();

    /**
     * @brief 播放结束
     */
    void endOfFile();

    /**
     * @brief 发生错误
     */
    void errorOccurred(const QString &error);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onFrameReady(const QImage &frame);
    void onStateChanged(FFmpegPlayer::PlaybackState state);
    void onPositionChanged(double seconds);
    void onDurationChanged(double seconds);
    void onFileLoaded();
    void onEndOfFile();
    void onErrorOccurred(const QString &error);

private:
    void updateScaledFrame();

    FFmpegPlayer *m_player;
    QImage m_currentFrame;
    QImage m_scaledFrame;
    QRect m_videoRect;
    bool m_keepAspectRatio = true;
};

#endif // VIDEOWIDGET_H

