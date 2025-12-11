#include "VideoWidget.h"
#include <QPainter>
#include <QResizeEvent>
#include <QDebug>
#include <QElapsedTimer>

// 渲染性能监控
static qint64 g_renderScaleTime = 0;
static qint64 g_renderPaintTime = 0;
static int g_renderFrameCount = 0;
static QElapsedTimer g_renderTimer;

VideoWidget::VideoWidget(QWidget *parent)
    : QWidget(parent)
    , m_player(new FFmpegPlayer(this))
{
    // 设置背景色
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(26, 26, 46));
    setPalette(pal);

    // 启用双缓冲，减少闪烁
    setAttribute(Qt::WA_OpaquePaintEvent);
    
    // 连接播放器信号
    connect(m_player, &FFmpegPlayer::frameReady, this, &VideoWidget::onFrameReady);
    connect(m_player, &FFmpegPlayer::stateChanged, this, &VideoWidget::onStateChanged);
    connect(m_player, &FFmpegPlayer::positionChanged, this, &VideoWidget::onPositionChanged);
    connect(m_player, &FFmpegPlayer::durationChanged, this, &VideoWidget::onDurationChanged);
    connect(m_player, &FFmpegPlayer::fileLoaded, this, &VideoWidget::onFileLoaded);
    connect(m_player, &FFmpegPlayer::endOfFile, this, &VideoWidget::onEndOfFile);
    connect(m_player, &FFmpegPlayer::errorOccurred, this, &VideoWidget::onErrorOccurred);
    
    g_renderTimer.start();
}

VideoWidget::~VideoWidget() = default;

void VideoWidget::loadFile(const QString &filename)
{
    m_player->loadFile(filename);
    // 自动开始播放
    m_player->play();
}

void VideoWidget::play()
{
    m_player->play();
}

void VideoWidget::pause()
{
    m_player->pause();
}

void VideoWidget::togglePause()
{
    m_player->togglePause();
}

void VideoWidget::stop()
{
    m_player->stop();
    m_currentFrame = QImage();
    m_scaledFrame = QImage();
    update();
}

void VideoWidget::setVolume(int volume)
{
    m_player->setVolume(volume);
}

int VideoWidget::volume() const
{
    return m_player->volume();
}

void VideoWidget::setLoop(bool loop)
{
    m_player->setLoop(loop);
}

void VideoWidget::seek(double seconds)
{
    m_player->seek(seconds);
}

double VideoWidget::position() const
{
    return m_player->position();
}

double VideoWidget::duration() const
{
    return m_player->duration();
}

bool VideoWidget::isPlaying() const
{
    return m_player->isPlaying();
}

bool VideoWidget::isPaused() const
{
    return m_player->isPaused();
}

void VideoWidget::onFrameReady(const QImage &frame)
{
    // 直接存储帧，不做预缩放
    // 让 paintEvent 中的 QPainter::drawImage 自动缩放
    m_currentFrame = frame;
    
    // 计算视频显示区域（保持宽高比）
    if (!m_currentFrame.isNull() && m_keepAspectRatio) {
        QSize frameSize = m_currentFrame.size();
        frameSize.scale(width(), height(), Qt::KeepAspectRatio);
        int x = (width() - frameSize.width()) / 2;
        int y = (height() - frameSize.height()) / 2;
        m_videoRect = QRect(x, y, frameSize.width(), frameSize.height());
    } else {
        m_videoRect = rect();
    }
    
    // 触发重绘
    update();
}

void VideoWidget::onStateChanged(FFmpegPlayer::PlaybackState state)
{
    emit playbackStateChanged(state == FFmpegPlayer::PlayingState);
}

void VideoWidget::onPositionChanged(double seconds)
{
    emit positionChanged(seconds);
}

void VideoWidget::onDurationChanged(double seconds)
{
    emit durationChanged(seconds);
}

void VideoWidget::onFileLoaded()
{
    qDebug() << "Video loaded:" << m_player->videoWidth() << "x" << m_player->videoHeight();
    emit fileLoaded();
}

void VideoWidget::onEndOfFile()
{
    emit endOfFile();
}

void VideoWidget::onErrorOccurred(const QString &error)
{
    qWarning() << "Video error:" << error;
    emit errorOccurred(error);
}

void VideoWidget::updateScaledFrame()
{
    // 不再预缩放，直接在 paintEvent 中绘制
    // 这样 QPainter 可以利用硬件加速
}

void VideoWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    
    qint64 t0 = g_renderTimer.nsecsElapsed();
    
    QPainter painter(this);
    
    // 【优化】关闭平滑变换，使用快速绘制
    // painter.setRenderHint(QPainter::SmoothPixmapTransform);  // 关闭！太慢
    
    // 绘制背景
    painter.fillRect(rect(), QColor(26, 26, 46));
    
    if (!m_currentFrame.isNull()) {
        // 【优化】直接绘制原图到目标区域
        // QPainter::drawImage 会自动缩放，比 QImage::scaled 快很多
        // 因为 QPainter 可能使用硬件加速
        painter.drawImage(m_videoRect, m_currentFrame);
    } else {
        // 显示提示信息
        painter.setPen(QColor(100, 100, 140));
        painter.setFont(QFont("Microsoft YaHei", 12));
        painter.drawText(rect(), Qt::AlignCenter, "拖放视频文件或右键打开");
    }
    
    qint64 t1 = g_renderTimer.nsecsElapsed();
    g_renderPaintTime += (t1 - t0);
    g_renderFrameCount++;
    
    // 每 100 帧输出渲染性能
    if (g_renderFrameCount % 100 == 0) {
        double avgPaintMs = g_renderPaintTime / 1000000.0 / 100.0;
        qDebug() << "========== 渲染性能 (100帧) ==========";
        qDebug() << "平均绘制时间:" << QString::number(avgPaintMs, 'f', 2) << "ms/帧";
        qDebug() << "渲染 FPS 上限:" << QString::number(1000.0 / avgPaintMs, 'f', 1);
        qDebug() << "=======================================";
        g_renderPaintTime = 0;
        g_renderFrameCount = 0;
    }
}

void VideoWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    
    // 窗口大小改变时，重新计算视频区域
    if (!m_currentFrame.isNull() && m_keepAspectRatio) {
        QSize frameSize = m_currentFrame.size();
        frameSize.scale(width(), height(), Qt::KeepAspectRatio);
        int x = (width() - frameSize.width()) / 2;
        int y = (height() - frameSize.height()) / 2;
        m_videoRect = QRect(x, y, frameSize.width(), frameSize.height());
    } else {
        m_videoRect = rect();
    }
}
