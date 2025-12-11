#ifndef MPVWIDGET_H
#define MPVWIDGET_H

#include <QWidget>
#include <QTimer>

#if MPV_AVAILABLE
#include <mpv/client.h>
#include <mpv/render_gl.h>
#endif

/**
 * @brief MPV 视频渲染组件
 * 
 * 使用 libmpv 进行视频播放，支持几乎所有视频格式。
 * 通过 OpenGL 进行硬件加速渲染。
 */
class MpvWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MpvWidget(QWidget *parent = nullptr);
    ~MpvWidget();

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
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;

private slots:
    void onMpvEvents();
    void updatePosition();

private:
    void initMpv();
    void handleMpvEvent(void *event);
    void setProperty(const char *name, const char *value);
    void setPropertyBool(const char *name, bool value);
    void setPropertyDouble(const char *name, double value);
    void setPropertyInt(const char *name, int value);
    void command(const QStringList &args);

#if MPV_AVAILABLE
    mpv_handle *m_mpv = nullptr;
#else
    void *m_mpv = nullptr;
#endif

    QTimer *m_positionTimer;
    double m_duration = 0;
    bool m_playing = false;
};

#endif // MPVWIDGET_H

