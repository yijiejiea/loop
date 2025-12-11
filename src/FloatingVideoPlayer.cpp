#include "FloatingVideoPlayer.h"
#include "D3D11Renderer.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QFileDialog>
#include <QPainter>
#include <QApplication>
#include <QScreen>
#include <QPushButton>
#include <QActionGroup>
#include <QMessageBox>
#include <QFileInfo>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>

FloatingVideoPlayer::FloatingVideoPlayer(QWidget *parent)
    : QWidget(parent)
{
    // 设置窗口属性
    setWindowFlags(Qt::FramelessWindowHint |      // 无边框
                   Qt::WindowStaysOnTopHint |     // 置顶
                   Qt::Tool);                      // 工具窗口
    
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);
    setAcceptDrops(true);  // 启用拖放功能

    // 默认大小和位置
    resize(400, 300);
    
    // 移动到屏幕右下角
    if (auto *screen = QApplication::primaryScreen()) {
        QRect screenGeometry = screen->availableGeometry();
        move(screenGeometry.right() - width() - 20,
             screenGeometry.bottom() - height() - 20);
    }

    setupUI();
    createContextMenu();
    
    setWindowOpacity(0.95);
}

FloatingVideoPlayer::~FloatingVideoPlayer() = default;

void FloatingVideoPlayer::setupUI()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(EDGE_MARGIN, EDGE_MARGIN, EDGE_MARGIN, EDGE_MARGIN);
    mainLayout->setSpacing(0);

    // D3D11 硬件加速视频组件
    m_videoWidget = new D3D11Renderer(this);
    m_videoWidget->setMouseTracking(true);
    mainLayout->addWidget(m_videoWidget);

    // 连接 D3D11 播放器信号
    connect(m_videoWidget, &D3D11Renderer::positionChanged, 
            this, &FloatingVideoPlayer::onPositionChanged);
    connect(m_videoWidget, &D3D11Renderer::durationChanged, 
            this, &FloatingVideoPlayer::onDurationChanged);
    connect(m_videoWidget, &D3D11Renderer::playbackStateChanged, 
            this, &FloatingVideoPlayer::onPlaybackStateChanged);
    connect(m_videoWidget, &D3D11Renderer::fileLoaded, 
            this, &FloatingVideoPlayer::onFileLoaded);
    connect(m_videoWidget, &D3D11Renderer::errorOccurred, 
            this, &FloatingVideoPlayer::onErrorOccurred);

    // 创建控制栏
    createControlBar();
}

void FloatingVideoPlayer::createControlBar()
{
    m_controlBar = new QWidget(this);
    m_controlBar->setFixedHeight(50);
    m_controlBar->setStyleSheet(R"(
        QWidget#controlBar {
            background-color: rgba(26, 26, 46, 0.95);
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
    m_controlBar->setObjectName("controlBar");

    auto *controlLayout = new QVBoxLayout(m_controlBar);
    controlLayout->setContentsMargins(10, 5, 10, 5);
    controlLayout->setSpacing(5);

    // 进度条
    m_progressSlider = new QSlider(Qt::Horizontal, m_controlBar);
    m_progressSlider->setRange(0, 1000);
    connect(m_progressSlider, &QSlider::sliderPressed, [this]() {
        m_isSliderDragging = true;
    });
    connect(m_progressSlider, &QSlider::sliderReleased, [this]() {
        m_isSliderDragging = false;
        if (m_duration > 0) {
            double seekPos = (m_progressSlider->value() / 1000.0) * m_duration;
            m_videoWidget->seek(seekPos);
        }
    });
    connect(m_progressSlider, &QSlider::sliderMoved, [this](int value) {
        if (m_duration > 0) {
            double pos = (value / 1000.0) * m_duration;
            m_timeLabel->setText(QString("%1 / %2")
                .arg(formatTime(pos))
                .arg(formatTime(m_duration)));
        }
    });
    controlLayout->addWidget(m_progressSlider);

    // 按钮行
    auto *buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(5);

    // 播放/暂停
    m_playPauseBtn = new QPushButton("▶", m_controlBar);
    m_playPauseBtn->setFixedSize(30, 30);
    connect(m_playPauseBtn, &QPushButton::clicked, this, &FloatingVideoPlayer::togglePlayPause);
    buttonLayout->addWidget(m_playPauseBtn);

    // 停止
    auto *stopBtn = new QPushButton("⏹", m_controlBar);
    stopBtn->setFixedSize(30, 30);
    connect(stopBtn, &QPushButton::clicked, this, &FloatingVideoPlayer::stop);
    buttonLayout->addWidget(stopBtn);

    // 时间
    m_timeLabel = new QLabel("00:00 / 00:00", m_controlBar);
    buttonLayout->addWidget(m_timeLabel);

    buttonLayout->addStretch();

    // 音量
    auto *volumeLabel = new QLabel("🔊", m_controlBar);
    buttonLayout->addWidget(volumeLabel);

    m_volumeSlider = new QSlider(Qt::Horizontal, m_controlBar);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(50);
    m_volumeSlider->setFixedWidth(60);
    connect(m_volumeSlider, &QSlider::valueChanged, this, &FloatingVideoPlayer::setVolume);
    buttonLayout->addWidget(m_volumeSlider);

    // 关闭
    auto *closeBtn = new QPushButton("✕", m_controlBar);
    closeBtn->setFixedSize(30, 30);
    closeBtn->setStyleSheet("QPushButton:hover { background-color: rgba(255, 0, 0, 0.5); border-radius: 4px; }");
    connect(closeBtn, &QPushButton::clicked, this, &QWidget::close);
    buttonLayout->addWidget(closeBtn);

    controlLayout->addLayout(buttonLayout);

    // 控制栏位置
    m_controlBar->setParent(renderer);
    m_controlBar->move(0, renderer->height() - m_controlBar->height());
    m_controlBar->resize(renderer->width(), m_controlBar->height());

    // 隐藏定时器
    m_hideControlTimer = new QTimer(this);
    m_hideControlTimer->setSingleShot(true);
    m_hideControlTimer->setInterval(3000);
    connect(m_hideControlTimer, &QTimer::timeout, this, &FloatingVideoPlayer::hideControlBar);

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
    connect(m_contextMenu->addAction("▶ 播放"), &QAction::triggered, this, &FloatingVideoPlayer::play);
    connect(m_contextMenu->addAction("⏸ 暂停"), &QAction::triggered, this, &FloatingVideoPlayer::pause);
    connect(m_contextMenu->addAction("⏹ 停止"), &QAction::triggered, this, &FloatingVideoPlayer::stop);

    m_contextMenu->addSeparator();

    // 透明度
    auto *opacityMenu = m_contextMenu->addMenu("🔆 透明度");
    auto *opacityGroup = new QActionGroup(this);
    
    for (auto [name, value] : {
        std::pair{"100%", 1.0}, {"90%", 0.9}, {"80%", 0.8}, 
        {"70%", 0.7}, {"60%", 0.6}, {"50%", 0.5}
    }) {
        auto *action = opacityMenu->addAction(name);
        action->setCheckable(true);
        action->setData(value);
        opacityGroup->addAction(action);
        if (value == 0.9) action->setChecked(true);
        connect(action, &QAction::triggered, [this, value]() {
            setOpacityLevel(value);
        });
    }

    // 窗口大小
    auto *sizeMenu = m_contextMenu->addMenu("📐 窗口大小");
    for (auto [name, size] : {
        std::pair{"小 (320×240)", QSize(320, 240)},
        {"中 (480×360)", QSize(480, 360)},
        {"大 (640×480)", QSize(640, 480)},
        {"更大 (800×600)", QSize(800, 600)}
    }) {
        connect(sizeMenu->addAction(name), &QAction::triggered, [this, size]() {
            if (!m_isFullScreen) resize(size);
        });
    }

    m_contextMenu->addSeparator();

    // 置顶
    auto *topMostAction = m_contextMenu->addAction("📌 始终置顶");
    topMostAction->setCheckable(true);
    topMostAction->setChecked(true);
    connect(topMostAction, &QAction::triggered, [this](bool checked) {
        auto flags = windowFlags();
        if (checked) flags |= Qt::WindowStaysOnTopHint;
        else flags &= ~Qt::WindowStaysOnTopHint;
        setWindowFlags(flags);
        show();
    });

    m_contextMenu->addSeparator();

    connect(m_contextMenu->addAction("❌ 退出"), &QAction::triggered, this, &QWidget::close);
}

void FloatingVideoPlayer::openVideo(const QString &filePath)
{
    if (filePath.isEmpty()) return;
    
    renderer->loadFile(filePath);
    
    QFileInfo fileInfo(filePath);
    setWindowTitle(QString("Loop - %1").arg(fileInfo.fileName()));
}

void FloatingVideoPlayer::play()
{
    renderer->play();
}

void FloatingVideoPlayer::pause()
{
    renderer->pause();
}

void FloatingVideoPlayer::stop()
{
    renderer->stop();
    m_progressSlider->setValue(0);
    m_timeLabel->setText("00:00 / 00:00");
}

void FloatingVideoPlayer::togglePlayPause()
{
    renderer->togglePause();
}

void FloatingVideoPlayer::setVolume(int volume)
{
    renderer->setVolume(volume);
}

void FloatingVideoPlayer::setOpacityLevel(qreal opacity)
{
    setWindowOpacity(qBound(0.3, opacity, 1.0));
}

void FloatingVideoPlayer::openFileDialog()
{
    QString filePath = QFileDialog::getOpenFileName(
        this, "选择视频文件", QString(),
        "视频文件 (*.mp4 *.avi *.mkv *.mov *.wmv *.flv *.webm *.m4v *.ts *.m2ts *.rmvb *.rm *.3gp);;所有文件 (*.*)"
    );
    
    if (!filePath.isEmpty()) {
        openVideo(filePath);
    }
}

void FloatingVideoPlayer::onPositionChanged(double seconds)
{
    if (!m_isSliderDragging && m_duration > 0) {
        int sliderPos = static_cast<int>((seconds / m_duration) * 1000);
        m_progressSlider->setValue(sliderPos);
        m_timeLabel->setText(QString("%1 / %2")
            .arg(formatTime(seconds))
            .arg(formatTime(m_duration)));
    }
}

void FloatingVideoPlayer::onDurationChanged(double seconds)
{
    m_duration = seconds;
}

void FloatingVideoPlayer::onPlaybackStateChanged(bool playing)
{
    m_playPauseBtn->setText(playing ? "⏸" : "▶");
}

void FloatingVideoPlayer::onFileLoaded()
{
    showControlBar();
}

void FloatingVideoPlayer::onErrorOccurred(const QString &error)
{
    QMessageBox::warning(this, "播放错误", error);
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

QString FloatingVideoPlayer::formatTime(double seconds)
{
    int totalSecs = static_cast<int>(seconds);
    int mins = totalSecs / 60;
    int secs = totalSecs % 60;
    int hours = mins / 60;
    mins = mins % 60;
    
    if (hours > 0) {
        return QString("%1:%2:%3")
            .arg(hours, 2, 10, QChar('0'))
            .arg(mins, 2, 10, QChar('0'))
            .arg(secs, 2, 10, QChar('0'));
    }
    return QString("%1:%2").arg(mins, 2, 10, QChar('0')).arg(secs, 2, 10, QChar('0'));
}

// 鼠标事件处理
void FloatingVideoPlayer::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_resizeEdge = detectEdge(event->pos());
        
        if (m_resizeEdge != None) {
            m_isResizing = true;
            m_resizeStartGeometry = geometry();
            m_dragPosition = event->globalPosition().toPoint();
        } else {
            m_isDragging = true;
            m_dragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
        }
        event->accept();
    }
}

void FloatingVideoPlayer::mouseMoveEvent(QMouseEvent *event)
{
    if (m_isDragging) {
        move(event->globalPosition().toPoint() - m_dragPosition);
        event->accept();
    } else if (m_isResizing) {
        QPoint delta = event->globalPosition().toPoint() - m_dragPosition;
        QRect newGeometry = m_resizeStartGeometry;
        
        if (m_resizeEdge & Left) newGeometry.setLeft(newGeometry.left() + delta.x());
        if (m_resizeEdge & Right) newGeometry.setRight(newGeometry.right() + delta.x());
        if (m_resizeEdge & Top) newGeometry.setTop(newGeometry.top() + delta.y());
        if (m_resizeEdge & Bottom) newGeometry.setBottom(newGeometry.bottom() + delta.y());
        
        if (newGeometry.width() >= MIN_WIDTH && newGeometry.height() >= MIN_HEIGHT) {
            setGeometry(newGeometry);
        }
        event->accept();
    } else {
        updateCursor(detectEdge(event->pos()));
    }
    
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
        if (m_isFullScreen) {
            setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
            setGeometry(m_normalGeometry);
            show();
            m_isFullScreen = false;
        } else {
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
    
    if (m_controlBar && renderer) {
        m_controlBar->resize(renderer->width(), m_controlBar->height());
        m_controlBar->move(0, renderer->height() - m_controlBar->height());
    }
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
    case Left: case Right:
        setCursor(Qt::SizeHorCursor); break;
    case Top: case Bottom:
        setCursor(Qt::SizeVerCursor); break;
    case TopLeft: case BottomRight:
        setCursor(Qt::SizeFDiagCursor); break;
    case TopRight: case BottomLeft:
        setCursor(Qt::SizeBDiagCursor); break;
    default:
        setCursor(Qt::ArrowCursor); break;
    }
}

void FloatingVideoPlayer::dragEnterEvent(QDragEnterEvent *event)
{
    // 检查是否包含文件URL
    if (event->mimeData()->hasUrls()) {
        const QList<QUrl> urls = event->mimeData()->urls();
        for (const QUrl &url : urls) {
            if (url.isLocalFile()) {
                QString filePath = url.toLocalFile();
                // 检查是否是支持的视频格式
                QStringList videoExtensions = {
                    "mp4", "avi", "mkv", "mov", "wmv", "flv", "webm",
                    "m4v", "ts", "m2ts", "rmvb", "rm", "3gp", "mpg",
                    "mpeg", "vob", "ogv", "mts"
                };
                QString ext = QFileInfo(filePath).suffix().toLower();
                if (videoExtensions.contains(ext)) {
                    event->acceptProposedAction();
                    return;
                }
            }
        }
    }
    event->ignore();
}

void FloatingVideoPlayer::dropEvent(QDropEvent *event)
{
    const QList<QUrl> urls = event->mimeData()->urls();
    for (const QUrl &url : urls) {
        if (url.isLocalFile()) {
            QString filePath = url.toLocalFile();
            openVideo(filePath);
            event->acceptProposedAction();
            return;
        }
    }
    event->ignore();
}

