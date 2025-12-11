#ifndef FLOATINGVIDEOPLAYER_H
#define FLOATINGVIDEOPLAYER_H

#include <QWidget>
#include <QPoint>
#include <QMenu>
#include <QSlider>
#include <QLabel>
#include <QTimer>
#include <QPushButton>

class D3D11Renderer;

/**
 * @brief 悬浮视频播放器窗口类
 * 
 * 使用 FFmpeg 进行视频播放，功能特性：
 * - 无边框悬浮窗口，始终置顶
 * - 支持视频无限循环播放
 * - 支持几乎所有视频格式（FFmpeg 支持的都能播放）
 * - 可拖动定位
 * - 右键菜单控制（打开文件、播放控制、音量、透明度等）
 * - 支持窗口大小调整
 * - 双击切换全屏
 */
class FloatingVideoPlayer : public QWidget
{
    Q_OBJECT

public:
    explicit FloatingVideoPlayer(QWidget *parent = nullptr);
    ~FloatingVideoPlayer();

    /**
     * @brief 打开并播放视频文件
     * @param filePath 视频文件路径
     */
    void openVideo(const QString &filePath);

public slots:
    void play();
    void pause();
    void stop();
    void togglePlayPause();
    void setVolume(int volume);
    void setOpacityLevel(qreal opacity);
    void openFileDialog();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void onPositionChanged(double seconds);
    void onDurationChanged(double seconds);
    void onPlaybackStateChanged(bool playing);
    void onFileLoaded();
    void onErrorOccurred(const QString &error);
    void hideControlBar();
    void showControlBar();

private:
    void setupUI();
    void createContextMenu();
    void createControlBar();
    QString formatTime(double seconds);

    // 边缘检测（用于调整窗口大小）
    enum ResizeEdge {
        None = 0,
        Left = 1,
        Right = 2,
        Top = 4,
        Bottom = 8,
        TopLeft = Top | Left,
        TopRight = Top | Right,
        BottomLeft = Bottom | Left,
        BottomRight = Bottom | Right
    };
    ResizeEdge detectEdge(const QPoint &pos);
    void updateCursor(ResizeEdge edge);

private:
    // 视频播放器 (硬件加速)
#ifdef _WIN32
    D3D11Renderer* renderer;
#elif defined(__APPLE__)
    // MetalRenderer *renderer = new MetalRenderer(this);  // TODO
    OpenGLRenderer* renderer;
#else
    OpenGLRenderer* renderer;
#endif

    // 控制栏
    QWidget *m_controlBar;
    QSlider *m_progressSlider;
    QSlider *m_volumeSlider;
    QLabel *m_timeLabel;
    QPushButton *m_playPauseBtn;
    QTimer *m_hideControlTimer;

    // 右键菜单
    QMenu *m_contextMenu;

    // 拖动相关
    QPoint m_dragPosition;
    bool m_isDragging = false;
    bool m_isResizing = false;
    ResizeEdge m_resizeEdge = None;
    QRect m_resizeStartGeometry;

    // 状态
    bool m_isFullScreen = false;
    QRect m_normalGeometry;
    double m_duration = 0;
    bool m_isSliderDragging = false;

    // 常量
    static constexpr int EDGE_MARGIN = 8;
    static constexpr int MIN_WIDTH = 200;
    static constexpr int MIN_HEIGHT = 150;
};

#endif // FLOATINGVIDEOPLAYER_H
