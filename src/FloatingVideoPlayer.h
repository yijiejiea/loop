#ifndef FLOATINGVIDEOPLAYER_H
#define FLOATINGVIDEOPLAYER_H

#include <QWidget>
#include <QMediaPlayer>
#include <QVideoWidget>
#include <QAudioOutput>
#include <QPoint>
#include <QMenu>
#include <QSlider>
#include <QLabel>
#include <QTimer>
#include <QPropertyAnimation>

/**
 * @brief 悬浮视频播放器窗口类
 * 
 * 功能特性：
 * - 无边框悬浮窗口，始终置顶
 * - 支持视频循环播放
 * - 可拖动定位
 * - 右键菜单控制（打开文件、播放控制、音量、透明度等）
 * - 支持窗口大小调整
 * - 双击切换全屏
 */
class FloatingVideoPlayer : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal windowOpacity READ windowOpacity WRITE setWindowOpacity)

public:
    /**
     * @brief 构造函数
     * @param parent 父窗口指针
     */
    explicit FloatingVideoPlayer(QWidget *parent = nullptr);
    
    /**
     * @brief 析构函数
     */
    ~FloatingVideoPlayer();

    /**
     * @brief 打开并播放视频文件
     * @param filePath 视频文件路径
     */
    void openVideo(const QString &filePath);

    /**
     * @brief 设置循环播放次数
     * @param loops 循环次数，-1 表示无限循环
     */
    void setLoopCount(int loops);

public slots:
    /**
     * @brief 播放视频
     */
    void play();
    
    /**
     * @brief 暂停视频
     */
    void pause();
    
    /**
     * @brief 停止视频
     */
    void stop();
    
    /**
     * @brief 切换播放/暂停状态
     */
    void togglePlayPause();

    /**
     * @brief 设置音量
     * @param volume 音量值 (0.0 - 1.0)
     */
    void setVolume(float volume);

    /**
     * @brief 设置窗口透明度
     * @param opacity 透明度值 (0.0 - 1.0)
     */
    void setOpacityLevel(qreal opacity);

    /**
     * @brief 打开文件对话框选择视频
     */
    void openFileDialog();

protected:
    // 鼠标事件处理 - 实现拖动和调整大小
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    
    // 右键菜单
    void contextMenuEvent(QContextMenuEvent *event) override;
    
    // 绘制事件 - 绘制边框
    void paintEvent(QPaintEvent *event) override;
    
    // 进入/离开事件 - 显示/隐藏控制条
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    
    // 窗口大小改变
    void resizeEvent(QResizeEvent *event) override;

private slots:
    /**
     * @brief 处理媒体状态改变
     */
    void onMediaStatusChanged(QMediaPlayer::MediaStatus status);
    
    /**
     * @brief 处理播放位置改变
     */
    void onPositionChanged(qint64 position);
    
    /**
     * @brief 处理时长改变
     */
    void onDurationChanged(qint64 duration);
    
    /**
     * @brief 处理播放错误
     */
    void onErrorOccurred(QMediaPlayer::Error error, const QString &errorString);
    
    /**
     * @brief 隐藏控制栏
     */
    void hideControlBar();
    
    /**
     * @brief 显示控制栏
     */
    void showControlBar();

private:
    /**
     * @brief 初始化 UI 组件
     */
    void setupUI();
    
    /**
     * @brief 初始化播放器
     */
    void setupPlayer();
    
    /**
     * @brief 创建右键菜单
     */
    void createContextMenu();
    
    /**
     * @brief 创建控制栏
     */
    void createControlBar();
    
    /**
     * @brief 格式化时间显示
     */
    QString formatTime(qint64 milliseconds);
    
    /**
     * @brief 检测鼠标位置是否在边缘（用于调整大小）
     */
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
    // 播放器组件
    QMediaPlayer *m_player;          ///< 媒体播放器
    QVideoWidget *m_videoWidget;     ///< 视频显示组件
    QAudioOutput *m_audioOutput;     ///< 音频输出

    // 控制栏组件
    QWidget *m_controlBar;           ///< 控制栏容器
    QSlider *m_progressSlider;       ///< 进度条
    QSlider *m_volumeSlider;         ///< 音量滑块
    QLabel *m_timeLabel;             ///< 时间显示标签
    QTimer *m_hideControlTimer;      ///< 隐藏控制栏定时器

    // 右键菜单
    QMenu *m_contextMenu;            ///< 右键菜单
    QMenu *m_opacityMenu;            ///< 透明度子菜单
    QMenu *m_sizeMenu;               ///< 大小子菜单

    // 窗口拖动相关
    QPoint m_dragPosition;           ///< 拖动起始位置
    bool m_isDragging;               ///< 是否正在拖动
    bool m_isResizing;               ///< 是否正在调整大小
    ResizeEdge m_resizeEdge;         ///< 当前调整的边缘
    QRect m_resizeStartGeometry;     ///< 调整大小前的几何信息

    // 状态
    bool m_isFullScreen;             ///< 是否全屏
    QRect m_normalGeometry;          ///< 正常状态下的几何信息
    qint64 m_duration;               ///< 视频总时长
    int m_loopCount;                 ///< 循环次数

    // 边缘检测阈值
    static constexpr int EDGE_MARGIN = 8;
    static constexpr int MIN_WIDTH = 200;
    static constexpr int MIN_HEIGHT = 150;
};

#endif // FLOATINGVIDEOPLAYER_H

