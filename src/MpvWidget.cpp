#include "MpvWidget.h"
#include <QPainter>
#include <QDebug>

#if defined(_WIN32)
#include <windows.h>
#endif

MpvWidget::MpvWidget(QWidget *parent)
    : QWidget(parent)
    , m_positionTimer(new QTimer(this))
{
    // 设置背景色
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(26, 26, 46));
    setPalette(pal);

    // 位置更新定时器
    connect(m_positionTimer, &QTimer::timeout, this, &MpvWidget::updatePosition);

    // 初始化 mpv
    initMpv();
}

MpvWidget::~MpvWidget()
{
#if MPV_AVAILABLE
    if (m_mpv) {
        mpv_terminate_destroy(m_mpv);
        m_mpv = nullptr;
    }
#endif
}

void MpvWidget::initMpv()
{
#if MPV_AVAILABLE
    m_mpv = mpv_create();
    if (!m_mpv) {
        qCritical() << "Failed to create mpv instance";
        emit errorOccurred("无法创建 mpv 实例");
        return;
    }

    // 设置窗口 ID，让 mpv 渲染到这个窗口
#if defined(_WIN32)
    int64_t wid = (int64_t)winId();
    mpv_set_option(m_mpv, "wid", MPV_FORMAT_INT64, &wid);
#else
    int64_t wid = (int64_t)winId();
    mpv_set_option(m_mpv, "wid", MPV_FORMAT_INT64, &wid);
#endif

    // 基本配置
    mpv_set_option_string(m_mpv, "input-default-bindings", "no");
    mpv_set_option_string(m_mpv, "input-vo-keyboard", "no");
    mpv_set_option_string(m_mpv, "osc", "no");  // 禁用 mpv 自带的 OSC
    mpv_set_option_string(m_mpv, "terminal", "no");
    mpv_set_option_string(m_mpv, "keep-open", "yes");  // 播放结束后保持打开
    mpv_set_option_string(m_mpv, "idle", "yes");  // 允许空闲状态
    
    // 硬件解码（自动选择最佳方式）
    mpv_set_option_string(m_mpv, "hwdec", "auto-safe");
    
    // 循环播放
    mpv_set_option_string(m_mpv, "loop-file", "inf");

    // 观察属性变化
    mpv_observe_property(m_mpv, 0, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "eof-reached", MPV_FORMAT_FLAG);

    // 请求事件通知
    mpv_request_log_messages(m_mpv, "warn");

    // 初始化 mpv
    if (mpv_initialize(m_mpv) < 0) {
        qCritical() << "Failed to initialize mpv";
        emit errorOccurred("无法初始化 mpv");
        mpv_terminate_destroy(m_mpv);
        m_mpv = nullptr;
        return;
    }

    // 设置事件回调
    mpv_set_wakeup_callback(m_mpv, [](void *ctx) {
        QMetaObject::invokeMethod(static_cast<MpvWidget*>(ctx), 
                                  "onMpvEvents", Qt::QueuedConnection);
    }, this);

    qDebug() << "mpv initialized successfully";
#else
    qWarning() << "mpv not available, video playback disabled";
    emit errorOccurred("mpv 库未配置，无法播放视频");
#endif
}

void MpvWidget::loadFile(const QString &filename)
{
#if MPV_AVAILABLE
    if (!m_mpv) return;

    const QByteArray utf8 = filename.toUtf8();
    const char *args[] = {"loadfile", utf8.constData(), nullptr};
    mpv_command_async(m_mpv, 0, args);
    
    m_positionTimer->start(100);  // 每 100ms 更新位置
#else
    Q_UNUSED(filename)
#endif
}

void MpvWidget::play()
{
#if MPV_AVAILABLE
    if (!m_mpv) return;
    setPropertyBool("pause", false);
#endif
}

void MpvWidget::pause()
{
#if MPV_AVAILABLE
    if (!m_mpv) return;
    setPropertyBool("pause", true);
#endif
}

void MpvWidget::togglePause()
{
#if MPV_AVAILABLE
    if (!m_mpv) return;
    
    int pause = 0;
    mpv_get_property(m_mpv, "pause", MPV_FORMAT_FLAG, &pause);
    setPropertyBool("pause", !pause);
#endif
}

void MpvWidget::stop()
{
#if MPV_AVAILABLE
    if (!m_mpv) return;
    
    const char *args[] = {"stop", nullptr};
    mpv_command_async(m_mpv, 0, args);
    m_positionTimer->stop();
    m_playing = false;
    emit playbackStateChanged(false);
#endif
}

void MpvWidget::setVolume(int volume)
{
#if MPV_AVAILABLE
    if (!m_mpv) return;
    setPropertyInt("volume", qBound(0, volume, 100));
#else
    Q_UNUSED(volume)
#endif
}

int MpvWidget::volume() const
{
#if MPV_AVAILABLE
    if (!m_mpv) return 0;
    
    int64_t vol = 0;
    mpv_get_property(m_mpv, "volume", MPV_FORMAT_INT64, &vol);
    return static_cast<int>(vol);
#else
    return 0;
#endif
}

void MpvWidget::setLoop(bool loop)
{
#if MPV_AVAILABLE
    if (!m_mpv) return;
    setProperty("loop-file", loop ? "inf" : "no");
#else
    Q_UNUSED(loop)
#endif
}

void MpvWidget::seek(double seconds)
{
#if MPV_AVAILABLE
    if (!m_mpv) return;
    
    QByteArray timeStr = QString::number(seconds, 'f', 2).toUtf8();
    const char *args[] = {"seek", timeStr.constData(), "absolute", nullptr};
    mpv_command_async(m_mpv, 0, args);
#else
    Q_UNUSED(seconds)
#endif
}

double MpvWidget::position() const
{
#if MPV_AVAILABLE
    if (!m_mpv) return 0;
    
    double pos = 0;
    mpv_get_property(m_mpv, "time-pos", MPV_FORMAT_DOUBLE, &pos);
    return pos;
#else
    return 0;
#endif
}

double MpvWidget::duration() const
{
    return m_duration;
}

bool MpvWidget::isPlaying() const
{
    return m_playing && !isPaused();
}

bool MpvWidget::isPaused() const
{
#if MPV_AVAILABLE
    if (!m_mpv) return true;
    
    int pause = 1;
    mpv_get_property(m_mpv, "pause", MPV_FORMAT_FLAG, &pause);
    return pause != 0;
#else
    return true;
#endif
}

void MpvWidget::onMpvEvents()
{
#if MPV_AVAILABLE
    if (!m_mpv) return;

    while (true) {
        mpv_event *event = mpv_wait_event(m_mpv, 0);
        if (event->event_id == MPV_EVENT_NONE) break;
        handleMpvEvent(event);
    }
#endif
}

void MpvWidget::handleMpvEvent(void *eventPtr)
{
#if MPV_AVAILABLE
    mpv_event *event = static_cast<mpv_event*>(eventPtr);

    switch (event->event_id) {
    case MPV_EVENT_PROPERTY_CHANGE: {
        mpv_event_property *prop = static_cast<mpv_event_property*>(event->data);
        
        if (strcmp(prop->name, "duration") == 0 && prop->format == MPV_FORMAT_DOUBLE) {
            m_duration = *static_cast<double*>(prop->data);
            emit durationChanged(m_duration);
        }
        else if (strcmp(prop->name, "time-pos") == 0 && prop->format == MPV_FORMAT_DOUBLE) {
            double pos = *static_cast<double*>(prop->data);
            emit positionChanged(pos);
        }
        else if (strcmp(prop->name, "pause") == 0 && prop->format == MPV_FORMAT_FLAG) {
            bool paused = *static_cast<int*>(prop->data);
            emit playbackStateChanged(!paused);
        }
        else if (strcmp(prop->name, "eof-reached") == 0 && prop->format == MPV_FORMAT_FLAG) {
            bool eof = *static_cast<int*>(prop->data);
            if (eof) emit endOfFile();
        }
        break;
    }
    case MPV_EVENT_FILE_LOADED:
        m_playing = true;
        emit fileLoaded();
        emit playbackStateChanged(true);
        qDebug() << "File loaded successfully";
        break;
        
    case MPV_EVENT_END_FILE: {
        mpv_event_end_file *ef = static_cast<mpv_event_end_file*>(event->data);
        if (ef->reason == MPV_END_FILE_REASON_ERROR) {
            QString error = mpv_error_string(ef->error);
            qWarning() << "Playback error:" << error;
            emit errorOccurred(QString("播放错误: %1").arg(error));
        }
        break;
    }
    case MPV_EVENT_LOG_MESSAGE: {
        mpv_event_log_message *msg = static_cast<mpv_event_log_message*>(event->data);
        qDebug() << "[mpv]" << msg->prefix << ":" << msg->text;
        break;
    }
    case MPV_EVENT_SHUTDOWN:
        qDebug() << "mpv shutdown";
        break;
        
    default:
        break;
    }
#else
    Q_UNUSED(eventPtr)
#endif
}

void MpvWidget::updatePosition()
{
#if MPV_AVAILABLE
    if (!m_mpv || !m_playing) return;
    
    double pos = position();
    emit positionChanged(pos);
#endif
}

void MpvWidget::setProperty(const char *name, const char *value)
{
#if MPV_AVAILABLE
    if (!m_mpv) return;
    mpv_set_property_string(m_mpv, name, value);
#else
    Q_UNUSED(name)
    Q_UNUSED(value)
#endif
}

void MpvWidget::setPropertyBool(const char *name, bool value)
{
#if MPV_AVAILABLE
    if (!m_mpv) return;
    int v = value ? 1 : 0;
    mpv_set_property(m_mpv, name, MPV_FORMAT_FLAG, &v);
#else
    Q_UNUSED(name)
    Q_UNUSED(value)
#endif
}

void MpvWidget::setPropertyDouble(const char *name, double value)
{
#if MPV_AVAILABLE
    if (!m_mpv) return;
    mpv_set_property(m_mpv, name, MPV_FORMAT_DOUBLE, &value);
#else
    Q_UNUSED(name)
    Q_UNUSED(value)
#endif
}

void MpvWidget::setPropertyInt(const char *name, int value)
{
#if MPV_AVAILABLE
    if (!m_mpv) return;
    int64_t v = value;
    mpv_set_property(m_mpv, name, MPV_FORMAT_INT64, &v);
#else
    Q_UNUSED(name)
    Q_UNUSED(value)
#endif
}

void MpvWidget::command(const QStringList &args)
{
#if MPV_AVAILABLE
    if (!m_mpv) return;
    
    QVector<QByteArray> utf8Args;
    QVector<const char*> cArgs;
    
    for (const QString &arg : args) {
        utf8Args.append(arg.toUtf8());
    }
    for (const QByteArray &arg : utf8Args) {
        cArgs.append(arg.constData());
    }
    cArgs.append(nullptr);
    
    mpv_command_async(m_mpv, 0, cArgs.data());
#else
    Q_UNUSED(args)
#endif
}

void MpvWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    
#if !MPV_AVAILABLE
    // 如果 mpv 不可用，显示提示信息
    QPainter painter(this);
    painter.fillRect(rect(), QColor(26, 26, 46));
    painter.setPen(QColor(233, 69, 96));
    painter.setFont(QFont("Microsoft YaHei", 12));
    painter.drawText(rect(), Qt::AlignCenter, 
                     "mpv 库未配置\n请参考 README.md 配置 libmpv");
#endif
}

bool MpvWidget::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
    Q_UNUSED(eventType)
    Q_UNUSED(message)
    Q_UNUSED(result)
    return false;
}

