#include "FloatingVideoPlayer.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QFileDialog>
#include <QPainter>
#include <QApplication>
#include <QScreen>
#include <QPushButton>
#include <QStyle>
#include <QGraphicsDropShadowEffect>
#include <QActionGroup>
#include <QMessageBox>

FloatingVideoPlayer::FloatingVideoPlayer(QWidget *parent)
    : QWidget(parent)
    , m_player(nullptr)
    , m_videoWidget(nullptr)
    , m_audioOutput(nullptr)
    , m_controlBar(nullptr)
    , m_progressSlider(nullptr)
    , m_volumeSlider(nullptr)
    , m_timeLabel(nullptr)
    , m_hideControlTimer(nullptr)
    , m_contextMenu(nullptr)
    , m_isDragging(false)
    , m_isResizing(false)
    , m_resizeEdge(None)
    , m_isFullScreen(false)
    , m_duration(0)
    , m_loopCount(-1)  // 默认无限循环
{
    // 设置窗口属性
    setWindowFlags(Qt::FramelessWindowHint |      // 无边框
                   Qt::WindowStaysOnTopHint |     // 置顶
                   Qt::Tool);                      // 工具窗口，不在任务栏显示
    
    setAttribute(Qt::WA_TranslucentBackground);   // 支持透明背景
    setMouseTracking(true);                       // 追踪鼠标移动

    // 设置默认大小和位置
    resize(400, 300);
    
    // 移动到屏幕右下角
    if (auto *screen = QApplication::primaryScreen()) {
        QRect screenGeometry = screen->availableGeometry();
        move(screenGeometry.right() - width() - 20,
             screenGeometry.bottom() - height() - 20);
    }

    setupUI();
    setupPlayer();
    createContextMenu();
    
    // 设置默认透明度
    setWindowOpacity(0.95);
}

FloatingVideoPlayer::~FloatingVideoPlayer()
{
    if (m_player) {
        m_player->stop();
    }
}

void FloatingVideoPlayer::setupUI()
{
    // 主布局
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(EDGE_MARGIN, EDGE_MARGIN, EDGE_MARGIN, EDGE_MARGIN);
    mainLayout->setSpacing(0);

    // 视频显示组件
    m_videoWidget = new QVideoWidget(this);
    m_videoWidget->setStyleSheet("background-color: #1a1a2e; border-radius: 8px;");
    m_videoWidget->setMouseTracking(true);
    mainLayout->addWidget(m_videoWidget);

    // 创建控制栏
    createControlBar();
}

void FloatingVideoPlayer::setupPlayer()
{
    // 创建播放器
    m_player = new QMediaPlayer(this);
    m_audioOutput = new QAudioOutput(this);
    
    // 设置音频输出
    m_player->setAudioOutput(m_audioOutput);
    m_audioOutput->setVolume(0.5f);  // 默认 50% 音量
    
    // 设置视频输出
    m_player->setVideoOutput(m_videoWidget);
    
    // 设置循环播放
    m_player->setLoops(QMediaPlayer::Infinite);

    // 连接信号槽
    connect(m_player, &QMediaPlayer::mediaStatusChanged,
            this, &FloatingVideoPlayer::onMediaStatusChanged);
    connect(m_player, &QMediaPlayer::positionChanged,
            this, &FloatingVideoPlayer::onPositionChanged);
    connect(m_player, &QMediaPlayer::durationChanged,
            this, &FloatingVideoPlayer::onDurationChanged);
    connect(m_player, &QMediaPlayer::errorOccurred,
            this, &FloatingVideoPlayer::onErrorOccurred);
}

void FloatingVideoPlayer::createControlBar()
{
    // 控制栏容器
    m_controlBar = new QWidget(this);
    m_controlBar->setFixedHeight(50);
    m_controlBar->setStyleSheet(R"(
        QWidget {
            background-color: rgba(26, 26, 46, 0.9);
            border-bottom-left-radius: 8px;
            border-bottom-right-radius: 8px;
        }
        QSlider::groove:horizontal {
            height: 4px;
            background: #3a3a5a;
            border-radius: 2px;
        }
        QSlider::handle:horizontal {
            width: 12px;
            height: 12px;
            margin: -4px 0;
            background: #e94560;
            border-radius: 6px;
        }
        QSlider::sub-page:horizontal {
            background: #e94560;
            border-radius: 2px;
        }
        QPushButton {
            background: transparent;
            color: white;
            border: none;
            padding: 5px;
            font-size: 14px;
        }
        QPushButton:hover {
            background-color: rgba(233, 69, 96, 0.3);
            border-radius: 4px;
        }
        QLabel {
            color: #ffffff;
            font-size: 11px;
        }
    )");

    auto *controlLayout = new QVBoxLayout(m_controlBar);
    controlLayout->setContentsMargins(10, 5, 10, 5);
    controlLayout->setSpacing(5);

    // 进度条
    m_progressSlider = new QSlider(Qt::Horizontal, m_controlBar);
    m_progressSlider->setRange(0, 0);
    connect(m_progressSlider, &QSlider::sliderMoved, [this](int position) {
        m_player->setPosition(position);
    });
    controlLayout->addWidget(m_progressSlider);

    // 控制按钮行
    auto *buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(5);

    // 播放/暂停按钮
    auto *playPauseBtn = new QPushButton("▶", m_controlBar);
    playPauseBtn->setFixedSize(30, 30);
    connect(playPauseBtn, &QPushButton::clicked, this, &FloatingVideoPlayer::togglePlayPause);
    connect(m_player, &QMediaPlayer::playbackStateChanged, [playPauseBtn](QMediaPlayer::PlaybackState state) {
        playPauseBtn->setText(state == QMediaPlayer::PlayingState ? "⏸" : "▶");
    });
    buttonLayout->addWidget(playPauseBtn);

    // 停止按钮
    auto *stopBtn = new QPushButton("⏹", m_controlBar);
    stopBtn->setFixedSize(30, 30);
    connect(stopBtn, &QPushButton::clicked, this, &FloatingVideoPlayer::stop);
    buttonLayout->addWidget(stopBtn);

    // 时间标签
    m_timeLabel = new QLabel("00:00 / 00:00", m_controlBar);
    buttonLayout->addWidget(m_timeLabel);

    buttonLayout->addStretch();

    // 音量图标和滑块
    auto *volumeLabel = new QLabel("🔊", m_controlBar);
    buttonLayout->addWidget(volumeLabel);

    m_volumeSlider = new QSlider(Qt::Horizontal, m_controlBar);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(50);
    m_volumeSlider->setFixedWidth(60);
    connect(m_volumeSlider, &QSlider::valueChanged, [this](int value) {
        setVolume(value / 100.0f);
    });
    buttonLayout->addWidget(m_volumeSlider);

    // 关闭按钮
    auto *closeBtn = new QPushButton("✕", m_controlBar);
    closeBtn->setFixedSize(30, 30);
    closeBtn->setStyleSheet("QPushButton:hover { background-color: rgba(255, 0, 0, 0.5); border-radius: 4px; }");
    connect(closeBtn, &QPushButton::clicked, this, &QWidget::close);
    buttonLayout->addWidget(closeBtn);

    controlLayout->addLayout(buttonLayout);

    // 将控制栏放置在视频底部
    m_controlBar->setParent(m_videoWidget);
    m_controlBar->move(0, m_videoWidget->height() - m_controlBar->height());
    m_controlBar->resize(m_videoWidget->width(), m_controlBar->height());

    // 隐藏控制栏定时器
    m_hideControlTimer = new QTimer(this);
    m_hideControlTimer->setSingleShot(true);
    m_hideControlTimer->setInterval(3000);
    connect(m_hideControlTimer, &QTimer::timeout, this, &FloatingVideoPlayer::hideControlBar);

    // 初始隐藏控制栏
    m_controlBar->hide();
}

void FloatingVideoPlayer::createContextMenu()
{
    m_contextMenu = new QMenu(this);
    m_contextMenu->setStyleSheet(R"(
        QMenu {
            background-color: #1a1a2e;
            color: white;
            border: 1px solid #3a3a5a;
            border-radius: 8px;
            padding: 5px;
        }
        QMenu::item {
            padding: 8px 25px;
            border-radius: 4px;
        }
        QMenu::item:selected {
            background-color: #e94560;
        }
        QMenu::separator {
            height: 1px;
            background: #3a3a5a;
            margin: 5px 10px;
        }
    )");

    // 打开文件
    auto *openAction = m_contextMenu->addAction("📂 打开视频文件...");
    connect(openAction, &QAction::triggered, this, &FloatingVideoPlayer::openFileDialog);

    m_contextMenu->addSeparator();

    // 播放控制
    auto *playAction = m_contextMenu->addAction("▶ 播放");
    connect(playAction, &QAction::triggered, this, &FloatingVideoPlayer::play);

    auto *pauseAction = m_contextMenu->addAction("⏸ 暂停");
    connect(pauseAction, &QAction::triggered, this, &FloatingVideoPlayer::pause);

    auto *stopAction = m_contextMenu->addAction("⏹ 停止");
    connect(stopAction, &QAction::triggered, this, &FloatingVideoPlayer::stop);

    m_contextMenu->addSeparator();

    // 透明度子菜单
    m_opacityMenu = m_contextMenu->addMenu("🔆 透明度");
    auto *opacityGroup = new QActionGroup(this);
    
    QList<QPair<QString, qreal>> opacityLevels = {
        {"100%", 1.0}, {"90%", 0.9}, {"80%", 0.8}, 
        {"70%", 0.7}, {"60%", 0.6}, {"50%", 0.5}
    };
    
    for (const auto &level : opacityLevels) {
        auto *action = m_opacityMenu->addAction(level.first);
        action->setCheckable(true);
        action->setData(level.second);
        opacityGroup->addAction(action);
        if (level.second == 0.9) action->setChecked(true);
        connect(action, &QAction::triggered, [this, action]() {
            setOpacityLevel(action->data().toReal());
        });
    }

    // 窗口大小子菜单
    m_sizeMenu = m_contextMenu->addMenu("📐 窗口大小");
    QList<QPair<QString, QSize>> sizes = {
        {"小 (320×240)", QSize(320, 240)},
        {"中 (480×360)", QSize(480, 360)},
        {"大 (640×480)", QSize(640, 480)},
        {"更大 (800×600)", QSize(800, 600)}
    };
    
    for (const auto &sizeInfo : sizes) {
        auto *action = m_sizeMenu->addAction(sizeInfo.first);
        connect(action, &QAction::triggered, [this, sizeInfo]() {
            if (!m_isFullScreen) {
                resize(sizeInfo.second);
            }
        });
    }

    m_contextMenu->addSeparator();

    // 置顶开关
    auto *topMostAction = m_contextMenu->addAction("📌 始终置顶");
    topMostAction->setCheckable(true);
    topMostAction->setChecked(true);
    connect(topMostAction, &QAction::triggered, [this](bool checked) {
        Qt::WindowFlags flags = windowFlags();
        if (checked) {
            flags |= Qt::WindowStaysOnTopHint;
        } else {
            flags &= ~Qt::WindowStaysOnTopHint;
        }
        setWindowFlags(flags);
        show();
    });

    m_contextMenu->addSeparator();

    // 退出
    auto *exitAction = m_contextMenu->addAction("❌ 退出");
    connect(exitAction, &QAction::triggered, this, &QWidget::close);
}

void FloatingVideoPlayer::openVideo(const QString &filePath)
{
    if (filePath.isEmpty()) return;
    
    m_player->setSource(QUrl::fromLocalFile(filePath));
    m_player->play();
    
    // 更新窗口标题
    QFileInfo fileInfo(filePath);
    setWindowTitle(QString("Loop - %1").arg(fileInfo.fileName()));
}

void FloatingVideoPlayer::setLoopCount(int loops)
{
    m_loopCount = loops;
    m_player->setLoops(loops == -1 ? QMediaPlayer::Infinite : loops);
}

void FloatingVideoPlayer::play()
{
    m_player->play();
}

void FloatingVideoPlayer::pause()
{
    m_player->pause();
}

void FloatingVideoPlayer::stop()
{
    m_player->stop();
}

void FloatingVideoPlayer::togglePlayPause()
{
    if (m_player->playbackState() == QMediaPlayer::PlayingState) {
        pause();
    } else {
        play();
    }
}

void FloatingVideoPlayer::setVolume(float volume)
{
    if (m_audioOutput) {
        m_audioOutput->setVolume(qBound(0.0f, volume, 1.0f));
    }
}

void FloatingVideoPlayer::setOpacityLevel(qreal opacity)
{
    setWindowOpacity(qBound(0.3, opacity, 1.0));
}

void FloatingVideoPlayer::openFileDialog()
{
    QString filePath = QFileDialog::getOpenFileName(
        this,
        "选择视频文件",
        QString(),
        "视频文件 (*.mp4 *.avi *.mkv *.mov *.wmv *.flv *.webm);;所有文件 (*.*)"
    );
    
    if (!filePath.isEmpty()) {
        openVideo(filePath);
    }
}

void FloatingVideoPlayer::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_resizeEdge = detectEdge(event->pos());
        
        if (m_resizeEdge != None) {
            // 开始调整大小
            m_isResizing = true;
            m_resizeStartGeometry = geometry();
            m_dragPosition = event->globalPosition().toPoint();
        } else {
            // 开始拖动
            m_isDragging = true;
            m_dragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
        }
        event->accept();
    }
}

void FloatingVideoPlayer::mouseMoveEvent(QMouseEvent *event)
{
    if (m_isDragging) {
        // 拖动窗口
        move(event->globalPosition().toPoint() - m_dragPosition);
        event->accept();
    } else if (m_isResizing) {
        // 调整窗口大小
        QPoint delta = event->globalPosition().toPoint() - m_dragPosition;
        QRect newGeometry = m_resizeStartGeometry;
        
        if (m_resizeEdge & Left) {
            newGeometry.setLeft(newGeometry.left() + delta.x());
        }
        if (m_resizeEdge & Right) {
            newGeometry.setRight(newGeometry.right() + delta.x());
        }
        if (m_resizeEdge & Top) {
            newGeometry.setTop(newGeometry.top() + delta.y());
        }
        if (m_resizeEdge & Bottom) {
            newGeometry.setBottom(newGeometry.bottom() + delta.y());
        }
        
        // 确保最小尺寸
        if (newGeometry.width() >= MIN_WIDTH && newGeometry.height() >= MIN_HEIGHT) {
            setGeometry(newGeometry);
        }
        event->accept();
    } else {
        // 更新光标
        ResizeEdge edge = detectEdge(event->pos());
        updateCursor(edge);
    }
    
    // 显示控制栏
    showControlBar();
}

void FloatingVideoPlayer::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_isDragging = false;
        m_isResizing = false;
        m_resizeEdge = None;
        event->accept();
    }
}

void FloatingVideoPlayer::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        // 双击切换全屏
        if (m_isFullScreen) {
            // 退出全屏
            setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
            setGeometry(m_normalGeometry);
            show();
            m_isFullScreen = false;
        } else {
            // 进入全屏
            m_normalGeometry = geometry();
            setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
            if (auto *screen = QApplication::screenAt(pos())) {
                setGeometry(screen->geometry());
            }
            show();
            m_isFullScreen = true;
        }
        event->accept();
    }
}

void FloatingVideoPlayer::contextMenuEvent(QContextMenuEvent *event)
{
    m_contextMenu->exec(event->globalPos());
}

void FloatingVideoPlayer::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // 绘制圆角矩形背景
    QRect rect = this->rect().adjusted(2, 2, -2, -2);
    painter.setPen(QPen(QColor(58, 58, 90), 2));
    painter.setBrush(QColor(26, 26, 46));
    painter.drawRoundedRect(rect, 10, 10);
}

void FloatingVideoPlayer::enterEvent(QEnterEvent *event)
{
    Q_UNUSED(event)
    showControlBar();
}

void FloatingVideoPlayer::leaveEvent(QEvent *event)
{
    Q_UNUSED(event)
    m_hideControlTimer->start();
}

void FloatingVideoPlayer::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    
    // 更新控制栏位置和大小
    if (m_controlBar && m_videoWidget) {
        m_controlBar->resize(m_videoWidget->width(), m_controlBar->height());
        m_controlBar->move(0, m_videoWidget->height() - m_controlBar->height());
    }
}

void FloatingVideoPlayer::onMediaStatusChanged(QMediaPlayer::MediaStatus status)
{
    switch (status) {
    case QMediaPlayer::LoadedMedia:
        qDebug() << "视频加载完成";
        break;
    case QMediaPlayer::BufferedMedia:
        qDebug() << "视频缓冲完成";
        break;
    case QMediaPlayer::EndOfMedia:
        qDebug() << "视频播放结束";
        break;
    case QMediaPlayer::InvalidMedia:
        qDebug() << "无效的媒体文件";
        QMessageBox::warning(this, "错误", "无法播放此视频文件");
        break;
    default:
        break;
    }
}

void FloatingVideoPlayer::onPositionChanged(qint64 position)
{
    if (!m_progressSlider->isSliderDown()) {
        m_progressSlider->setValue(static_cast<int>(position));
    }
    
    // 更新时间显示
    if (m_timeLabel) {
        m_timeLabel->setText(QString("%1 / %2")
            .arg(formatTime(position))
            .arg(formatTime(m_duration)));
    }
}

void FloatingVideoPlayer::onDurationChanged(qint64 duration)
{
    m_duration = duration;
    m_progressSlider->setRange(0, static_cast<int>(duration));
}

void FloatingVideoPlayer::onErrorOccurred(QMediaPlayer::Error error, const QString &errorString)
{
    qDebug() << "播放错误:" << error << errorString;
    QMessageBox::warning(this, "播放错误", errorString);
}

void FloatingVideoPlayer::hideControlBar()
{
    if (m_controlBar && !m_controlBar->underMouse()) {
        m_controlBar->hide();
    }
}

void FloatingVideoPlayer::showControlBar()
{
    if (m_controlBar) {
        m_controlBar->show();
        m_controlBar->raise();
        m_hideControlTimer->start();
    }
}

QString FloatingVideoPlayer::formatTime(qint64 milliseconds)
{
    int seconds = static_cast<int>(milliseconds / 1000);
    int minutes = seconds / 60;
    seconds = seconds % 60;
    int hours = minutes / 60;
    minutes = minutes % 60;
    
    if (hours > 0) {
        return QString("%1:%2:%3")
            .arg(hours, 2, 10, QChar('0'))
            .arg(minutes, 2, 10, QChar('0'))
            .arg(seconds, 2, 10, QChar('0'));
    }
    return QString("%1:%2")
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'));
}

FloatingVideoPlayer::ResizeEdge FloatingVideoPlayer::detectEdge(const QPoint &pos)
{
    ResizeEdge edge = None;
    
    if (pos.x() < EDGE_MARGIN) edge = static_cast<ResizeEdge>(edge | Left);
    if (pos.x() > width() - EDGE_MARGIN) edge = static_cast<ResizeEdge>(edge | Right);
    if (pos.y() < EDGE_MARGIN) edge = static_cast<ResizeEdge>(edge | Top);
    if (pos.y() > height() - EDGE_MARGIN) edge = static_cast<ResizeEdge>(edge | Bottom);
    
    return edge;
}

void FloatingVideoPlayer::updateCursor(ResizeEdge edge)
{
    switch (edge) {
    case Left:
    case Right:
        setCursor(Qt::SizeHorCursor);
        break;
    case Top:
    case Bottom:
        setCursor(Qt::SizeVerCursor);
        break;
    case TopLeft:
    case BottomRight:
        setCursor(Qt::SizeFDiagCursor);
        break;
    case TopRight:
    case BottomLeft:
        setCursor(Qt::SizeBDiagCursor);
        break;
    default:
        setCursor(Qt::ArrowCursor);
        break;
    }
}

