#include "FFmpegPlayer.h"
#include <QTimer>
#include <QDebug>
#include <QElapsedTimer>
#include <cmath>
#include <QDateTime>

// 性能监控
static qint64 g_decodeTime = 0;
static qint64 g_transferTime = 0;
static qint64 g_scaleTime = 0;
static qint64 g_copyTime = 0;
static int g_frameCount = 0;
static QElapsedTimer g_perfTimer;

// ============================================
// DecodeThread 实现
// ============================================

DecodeThread::DecodeThread(QObject *parent)
    : QThread(parent)
{
}

DecodeThread::~DecodeThread()
{
    stopDecoding();
    closeFile();
}

/**
 * @brief 初始化硬件解码器
 * @param codec 解码器
 * @return 是否成功初始化硬件解码
 */
bool DecodeThread::initHardwareDecoder(const AVCodec *codec)
{
#if FFMPEG_AVAILABLE
    // 支持的硬件加速类型（按优先级排序）
    const AVHWDeviceType hwTypes[] = {
        AV_HWDEVICE_TYPE_D3D11VA,   // Windows Direct3D 11 (推荐)
        AV_HWDEVICE_TYPE_DXVA2,     // Windows Direct3D 9
        AV_HWDEVICE_TYPE_CUDA,      // NVIDIA
        AV_HWDEVICE_TYPE_QSV,       // Intel Quick Sync
        AV_HWDEVICE_TYPE_VAAPI,     // Linux VA-API
        AV_HWDEVICE_TYPE_VDPAU,     // Linux VDPAU
        AV_HWDEVICE_TYPE_VIDEOTOOLBOX, // macOS
    };
    
    // 遍历编解码器支持的硬件配置
    for (int i = 0;; i++) {
        const AVCodecHWConfig *config = avcodec_get_hw_config(codec, i);
        if (!config) break;
        
        // 检查是否支持通过 hw_device_ctx 使用
        if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) {
            // 尝试创建硬件设备上下文
            for (AVHWDeviceType hwType : hwTypes) {
                if (config->device_type == hwType) {
                    int ret = av_hwdevice_ctx_create(&m_hwDeviceCtx, hwType, nullptr, nullptr, 0);
                    if (ret >= 0) {
                        m_hwPixFmt = config->pix_fmt;
                        m_useHwDecode = true;
                        
                        // 设置硬件设备上下文到解码器
                        m_videoCodecCtx->hw_device_ctx = av_buffer_ref(m_hwDeviceCtx);
                        
                        const char *hwName = av_hwdevice_get_type_name(hwType);
                        qDebug() << "✓ 启用硬件解码:" << hwName;
                        return true;
                    }
                }
            }
        }
    }
    
    qDebug() << "✗ 硬件解码不可用，使用软件解码";
    return false;
#else
    Q_UNUSED(codec)
    return false;
#endif
}

/**
 * @brief 将硬件帧从 GPU 传输到 CPU
 * @param hwFrame GPU 中的硬件帧
 * @return CPU 中的软件帧（调用者负责释放）
 */
AVFrame* DecodeThread::transferHwFrame(AVFrame *hwFrame)
{
#if FFMPEG_AVAILABLE
    if (!m_useHwDecode || hwFrame->format != m_hwPixFmt) {
        // 不是硬件帧，直接返回 nullptr 表示使用原帧
        return nullptr;
    }
    
    // 分配 CPU 帧
    AVFrame *swFrame = av_frame_alloc();
    if (!swFrame) {
        qWarning() << "无法分配软件帧";
        return nullptr;
    }
    
    // 从 GPU 传输到 CPU
    int ret = av_hwframe_transfer_data(swFrame, hwFrame, 0);
    if (ret < 0) {
        char errBuf[256];
        av_strerror(ret, errBuf, sizeof(errBuf));
        qWarning() << "硬件帧传输失败:" << errBuf;
        av_frame_free(&swFrame);
        return nullptr;
    }
    
    // 复制时间戳等元数据
    swFrame->pts = hwFrame->pts;
    swFrame->pkt_dts = hwFrame->pkt_dts;
    
    return swFrame;
#else
    Q_UNUSED(hwFrame)
    return nullptr;
#endif
}

bool DecodeThread::openFile(const QString &filename)
{
#if FFMPEG_AVAILABLE
    closeFile();
    
    // 打开文件
    m_formatCtx = avformat_alloc_context();
    if (avformat_open_input(&m_formatCtx, filename.toUtf8().constData(), nullptr, nullptr) != 0) {
        emit errorOccurred("无法打开文件: " + filename);
        return false;
    }
    
    // 获取流信息
    if (avformat_find_stream_info(m_formatCtx, nullptr) < 0) {
        emit errorOccurred("无法获取流信息");
        closeFile();
        return false;
    }
    
    // 获取时长
    if (m_formatCtx->duration != AV_NOPTS_VALUE) {
        m_duration = static_cast<double>(m_formatCtx->duration) / AV_TIME_BASE;
    }
    
    // 查找视频流和音频流
    for (unsigned int i = 0; i < m_formatCtx->nb_streams; i++) {
        if (m_formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            m_videoStreamIndex = i;
        } else if (m_formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            m_audioStreamIndex = i;
        }
    }
    
    // ========================================
    // 初始化视频解码器（支持硬件加速）
    // ========================================
    if (m_videoStreamIndex >= 0) {
        AVCodecParameters *codecpar = m_formatCtx->streams[m_videoStreamIndex]->codecpar;
        const AVCodec *codec = avcodec_find_decoder(codecpar->codec_id);
        if (!codec) {
            emit errorOccurred("找不到视频解码器");
            closeFile();
            return false;
        }
        
        // 分配解码器上下文
        m_videoCodecCtx = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(m_videoCodecCtx, codecpar);
        
        // 【重要】在 avcodec_open2 之前尝试初始化硬件解码
        initHardwareDecoder(codec);
        
        // 打开解码器
        if (avcodec_open2(m_videoCodecCtx, codec, nullptr) < 0) {
            emit errorOccurred("无法打开视频解码器");
            closeFile();
            return false;
        }
        
        m_videoWidth = m_videoCodecCtx->width;
        m_videoHeight = m_videoCodecCtx->height;
        
        // 注意：sws_scale 上下文将在解码时根据实际帧格式创建
        // 因为硬件解码和软件解码的源格式不同
    }
    
    // ========================================
    // 初始化音频解码器
    // ========================================
    if (m_audioStreamIndex >= 0) {
        AVCodecParameters *codecpar = m_formatCtx->streams[m_audioStreamIndex]->codecpar;
        const AVCodec *codec = avcodec_find_decoder(codecpar->codec_id);
        if (codec) {
            m_audioCodecCtx = avcodec_alloc_context3(codec);
            avcodec_parameters_to_context(m_audioCodecCtx, codecpar);
            
            if (avcodec_open2(m_audioCodecCtx, codec, nullptr) == 0) {
                m_audioSampleRate = m_audioCodecCtx->sample_rate;
                m_audioChannels = m_audioCodecCtx->ch_layout.nb_channels;
                
                // 初始化音频重采样器
                m_swrCtx = swr_alloc();
                AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_STEREO;
                AVChannelLayout inLayout = m_audioCodecCtx->ch_layout;
                
                swr_alloc_set_opts2(&m_swrCtx,
                    &outLayout, AV_SAMPLE_FMT_S16, 44100,
                    &inLayout, m_audioCodecCtx->sample_fmt, m_audioCodecCtx->sample_rate,
                    0, nullptr);
                swr_init(m_swrCtx);
                
                m_audioSampleRate = 44100;
                m_audioChannels = 2;
            }
        }
    }
    
    qDebug() << "========================================";
    qDebug() << "文件已打开:" << filename;
    qDebug() << "时长:" << m_duration << "秒";
    qDebug() << "视频:" << m_videoWidth << "x" << m_videoHeight;
    qDebug() << "音频:" << m_audioSampleRate << "Hz," << m_audioChannels << "声道";
    qDebug() << "硬件解码:" << (m_useHwDecode ? "是" : "否");
    qDebug() << "========================================";
    
    emit fileOpened();
    return true;
#else
    Q_UNUSED(filename)
    emit errorOccurred("FFmpeg 未配置");
    return false;
#endif
}

void DecodeThread::closeFile()
{
#if FFMPEG_AVAILABLE
    stopDecoding();
    flushQueues();
    
    if (m_swsCtx) {
        sws_freeContext(m_swsCtx);
        m_swsCtx = nullptr;
    }
    
    if (m_swrCtx) {
        swr_free(&m_swrCtx);
        m_swrCtx = nullptr;
    }
    
    if (m_videoCodecCtx) {
        avcodec_free_context(&m_videoCodecCtx);
        m_videoCodecCtx = nullptr;
    }
    
    if (m_audioCodecCtx) {
        avcodec_free_context(&m_audioCodecCtx);
        m_audioCodecCtx = nullptr;
    }
    
    if (m_hwDeviceCtx) {
        av_buffer_unref(&m_hwDeviceCtx);
        m_hwDeviceCtx = nullptr;
    }
    
    if (m_formatCtx) {
        avformat_close_input(&m_formatCtx);
        m_formatCtx = nullptr;
    }
    
    m_videoStreamIndex = -1;
    m_audioStreamIndex = -1;
    m_duration = 0;
    m_videoWidth = 0;
    m_videoHeight = 0;
    m_useHwDecode = false;
    m_hwPixFmt = AV_PIX_FMT_NONE;
#endif
}

void DecodeThread::startDecoding()
{
    if (!m_running) {
        m_running = true;
        start();
    }
}

void DecodeThread::stopDecoding()
{
    m_running = false;
    m_videoCondition.wakeAll();
    m_audioCondition.wakeAll();
    if (isRunning()) {
        wait(1000);
        if (isRunning()) {
            terminate();
            wait();
        }
    }
}

void DecodeThread::seekTo(double seconds)
{
#if FFMPEG_AVAILABLE
    m_seekTarget = seconds;
    m_seeking = true;
#else
    Q_UNUSED(seconds)
#endif
}

QAudioFormat DecodeThread::audioFormat() const
{
    QAudioFormat format;
    format.setSampleRate(m_audioSampleRate);
    format.setChannelCount(m_audioChannels);
    format.setSampleFormat(QAudioFormat::Int16);
    return format;
}

bool DecodeThread::getVideoFrame(VideoFrame &frame)
{
    QMutexLocker locker(&m_videoMutex);
    if (m_videoQueue.isEmpty()) {
        return false;
    }
    frame = m_videoQueue.dequeue();
    m_videoCondition.wakeOne();
    return true;
}

bool DecodeThread::getAudioFrame(AudioFrame &frame)
{
    QMutexLocker locker(&m_audioMutex);
    if (m_audioQueue.isEmpty()) {
        return false;
    }
    frame = m_audioQueue.dequeue();
    m_audioCondition.wakeOne();
    return true;
}

void DecodeThread::run()
{
#if FFMPEG_AVAILABLE
    if (!m_formatCtx) return;
    
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    AVFrame *rgbFrame = av_frame_alloc();
    
    // 为 RGB 帧分配缓冲区
    int rgbBufferSize = av_image_get_buffer_size(AV_PIX_FMT_RGB32, m_videoWidth, m_videoHeight, 1);
    uint8_t *rgbBuffer = static_cast<uint8_t*>(av_malloc(rgbBufferSize));
    av_image_fill_arrays(rgbFrame->data, rgbFrame->linesize, rgbBuffer, 
                         AV_PIX_FMT_RGB32, m_videoWidth, m_videoHeight, 1);
    
    // 用于记录上一帧的像素格式，以便在格式变化时重新创建 sws 上下文
    AVPixelFormat lastPixFmt = AV_PIX_FMT_NONE;
    
    // 性能计时
    g_perfTimer.start();
    g_frameCount = 0;
    g_decodeTime = g_transferTime = g_scaleTime = g_copyTime = 0;
    
    while (m_running) {
        // 处理 seek
        if (m_seeking) {
            int64_t timestamp = static_cast<int64_t>(m_seekTarget * AV_TIME_BASE);
            av_seek_frame(m_formatCtx, -1, timestamp, AVSEEK_FLAG_BACKWARD);
            
            if (m_videoCodecCtx) avcodec_flush_buffers(m_videoCodecCtx);
            if (m_audioCodecCtx) avcodec_flush_buffers(m_audioCodecCtx);
            
            flushQueues();
            m_seeking = false;
        }
        
        // 读取数据包
        int ret = av_read_frame(m_formatCtx, packet);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                emit decodingFinished();
            }
            break;
        }
        
        // ========================================
        // 视频解码
        // ========================================
        if (packet->stream_index == m_videoStreamIndex && m_videoCodecCtx) {
            qint64 t0 = g_perfTimer.nsecsElapsed();
            
            ret = avcodec_send_packet(m_videoCodecCtx, packet);
            while (ret >= 0) {
                ret = avcodec_receive_frame(m_videoCodecCtx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                if (ret < 0) break;
                
                qint64 t1 = g_perfTimer.nsecsElapsed();
                g_decodeTime += (t1 - t0);
                
                // 处理帧 - 可能是硬件帧或软件帧
                AVFrame *srcFrame = frame;
                AVFrame *swFrame = nullptr;
                
                // 如果是硬件帧，需要先传输到 CPU
                if (m_useHwDecode && frame->format == m_hwPixFmt) {
                    swFrame = transferHwFrame(frame);
                    if (swFrame) {
                        srcFrame = swFrame;
                    } else {
                        // 传输失败，跳过这一帧
                        continue;
                    }
                }
                
                qint64 t2 = g_perfTimer.nsecsElapsed();
                g_transferTime += (t2 - t1);
                
                // 检查像素格式是否变化，需要重新创建 sws 上下文
                AVPixelFormat pixFmt = static_cast<AVPixelFormat>(srcFrame->format);
                if (pixFmt != lastPixFmt) {
                    if (m_swsCtx) {
                        sws_freeContext(m_swsCtx);
                    }
                    // 使用 SWS_FAST_BILINEAR 提升性能
                    m_swsCtx = sws_getContext(
                        m_videoWidth, m_videoHeight, pixFmt,
                        m_videoWidth, m_videoHeight, AV_PIX_FMT_RGB32,
                        SWS_FAST_BILINEAR, nullptr, nullptr, nullptr
                    );
                    lastPixFmt = pixFmt;
                    qDebug() << "创建 sws 上下文，源格式:" << av_get_pix_fmt_name(pixFmt);
                }
                
                // 转换为 RGB
                if (m_swsCtx) {
                    sws_scale(m_swsCtx, srcFrame->data, srcFrame->linesize, 0, m_videoHeight,
                             rgbFrame->data, rgbFrame->linesize);
                }
                
                qint64 t3 = g_perfTimer.nsecsElapsed();
                g_scaleTime += (t3 - t2);
                
                // 计算 PTS
                double pts = 0;
                AVStream *stream = m_formatCtx->streams[m_videoStreamIndex];
                if (srcFrame->pts != AV_NOPTS_VALUE) {
                    pts = srcFrame->pts * av_q2d(stream->time_base);
                }
                
                // 创建 QImage - 使用浅拷贝 + 移动语义优化
                QImage image(rgbFrame->data[0], m_videoWidth, m_videoHeight, 
                            rgbFrame->linesize[0], QImage::Format_RGB32);
                
                VideoFrame vf;
                vf.image = image.copy();  // 必须深拷贝，因为 rgbBuffer 会被复用
                vf.pts = pts;
                
                qint64 t4 = g_perfTimer.nsecsElapsed();
                g_copyTime += (t4 - t3);
                
                // 释放临时的软件帧
                if (swFrame) {
                    av_frame_free(&swFrame);
                }
                
                g_frameCount++;
                
                // 每 100 帧输出一次性能统计
                if (g_frameCount % 100 == 0) {
                    double totalMs = g_perfTimer.elapsed();
                    double fps = g_frameCount * 1000.0 / totalMs;
                    qDebug() << "========== 性能统计 (100帧) ==========";
                    qDebug() << "FPS:" << QString::number(fps, 'f', 1);
                    qDebug() << "解码:" << (g_decodeTime / 1000000) << "ms";
                    qDebug() << "GPU→CPU:" << (g_transferTime / 1000000) << "ms";
                    qDebug() << "sws_scale:" << (g_scaleTime / 1000000) << "ms";
                    qDebug() << "QImage拷贝:" << (g_copyTime / 1000000) << "ms";
                    qDebug() << "队列大小:" << m_videoQueue.size();
                    qDebug() << "=======================================";
                    // 重置计时
                    g_decodeTime = g_transferTime = g_scaleTime = g_copyTime = 0;
                    g_perfTimer.restart();
                    g_frameCount = 0;
                }
                
                // 加入队列（等待如果队列满）
                {
                    QMutexLocker locker(&m_videoMutex);
                    while (m_videoQueue.size() >= MAX_VIDEO_QUEUE_SIZE && m_running) {
                        m_videoCondition.wait(&m_videoMutex, 10);
                    }
                    if (m_running) {
                        m_videoQueue.enqueue(vf);
                    }
                }
                
                t0 = g_perfTimer.nsecsElapsed();  // 重置解码计时起点
            }
        }
        
        // ========================================
        // 音频解码
        // ========================================
        if (packet->stream_index == m_audioStreamIndex && m_audioCodecCtx && m_swrCtx) {
            ret = avcodec_send_packet(m_audioCodecCtx, packet);
            while (ret >= 0) {
                ret = avcodec_receive_frame(m_audioCodecCtx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                if (ret < 0) break;
                
                // 计算 PTS
                double pts = 0;
                AVStream *stream = m_formatCtx->streams[m_audioStreamIndex];
                if (frame->pts != AV_NOPTS_VALUE) {
                    pts = frame->pts * av_q2d(stream->time_base);
                }
                
                // 重采样
                int outSamples = static_cast<int>(av_rescale_rnd(
                    swr_get_delay(m_swrCtx, m_audioCodecCtx->sample_rate) + frame->nb_samples,
                    44100, m_audioCodecCtx->sample_rate, AV_ROUND_UP));
                
                int bufferSize = outSamples * 2 * 2;  // stereo, 16-bit
                QByteArray audioData(bufferSize, 0);
                uint8_t *outBuffer = reinterpret_cast<uint8_t*>(audioData.data());
                
                int samples = swr_convert(m_swrCtx, &outBuffer, outSamples,
                                         const_cast<const uint8_t**>(frame->data), frame->nb_samples);
                
                if (samples > 0) {
                    audioData.resize(samples * 2 * 2);
                    
                    AudioFrame af;
                    af.data = audioData;
                    af.pts = pts;
                    
                    {
                        QMutexLocker locker(&m_audioMutex);
                        while (m_audioQueue.size() >= MAX_AUDIO_QUEUE_SIZE && m_running) {
                            m_audioCondition.wait(&m_audioMutex, 10);
                        }
                        if (m_running) {
                            m_audioQueue.enqueue(af);
                        }
                    }
                }
            }
        }
        
        av_packet_unref(packet);
    }
    
    av_free(rgbBuffer);
    av_frame_free(&rgbFrame);
    av_frame_free(&frame);
    av_packet_free(&packet);
#endif
}

void DecodeThread::flushQueues()
{
    {
        QMutexLocker locker(&m_videoMutex);
        m_videoQueue.clear();
    }
    {
        QMutexLocker locker(&m_audioMutex);
        m_audioQueue.clear();
    }
}

// ============================================
// FFmpegPlayer 实现
// ============================================

FFmpegPlayer::FFmpegPlayer(QObject *parent)
    : QObject(parent)
    , m_decodeThread(new DecodeThread(this))
{
    connect(m_decodeThread, &DecodeThread::fileOpened, this, &FFmpegPlayer::onFileOpened);
    connect(m_decodeThread, &DecodeThread::decodingFinished, this, &FFmpegPlayer::onDecodingFinished);
    connect(m_decodeThread, &DecodeThread::errorOccurred, this, &FFmpegPlayer::onDecodeError);
    
    // 视频定时器
    m_videoTimer = new QTimer(this);
    m_videoTimer->setTimerType(Qt::PreciseTimer);
    connect(m_videoTimer, &QTimer::timeout, this, &FFmpegPlayer::processVideo);
    
    // 音频定时器
    m_audioTimer = new QTimer(this);
    m_audioTimer->setTimerType(Qt::PreciseTimer);
    connect(m_audioTimer, &QTimer::timeout, this, &FFmpegPlayer::processAudio);
}

FFmpegPlayer::~FFmpegPlayer()
{
    stop();
    cleanupAudio();
}

void FFmpegPlayer::loadFile(const QString &filename)
{
    stop();
    m_currentFile = filename;
    
    if (m_decodeThread->openFile(filename)) {
        m_duration = m_decodeThread->duration();
        emit durationChanged(m_duration);
    }
}

void FFmpegPlayer::play()
{
    if (m_state == PlayingState) return;
    
    if (m_state == StoppedState && !m_currentFile.isEmpty()) {
        if (m_duration == 0) {
            // 重新打开文件
            m_decodeThread->openFile(m_currentFile);
        }
        setupAudio();
        m_decodeThread->startDecoding();
    }
    
    m_startTime = QDateTime::currentMSecsSinceEpoch() - static_cast<qint64>(m_currentPosition * 1000);
    
    // 启动定时器
    m_videoTimer->start(10);  // ~100 fps 检查
    m_audioTimer->start(5);   // 音频处理更频繁
    
    setState(PlayingState);
}

void FFmpegPlayer::pause()
{
    if (m_state != PlayingState) return;
    
    m_videoTimer->stop();
    m_audioTimer->stop();
    
    setState(PausedState);
}

void FFmpegPlayer::stop()
{
    m_videoTimer->stop();
    m_audioTimer->stop();
    
    m_decodeThread->stopDecoding();
    cleanupAudio();
    
    m_currentPosition = 0;
    m_audioClock = 0;
    emit positionChanged(0);
    
    setState(StoppedState);
}

void FFmpegPlayer::togglePause()
{
    if (m_state == PlayingState) {
        pause();
    } else {
        play();
    }
}

void FFmpegPlayer::seek(double seconds)
{
    seconds = qBound(0.0, seconds, m_duration);
    m_currentPosition = seconds;
    m_audioClock = seconds;
    m_startTime = QDateTime::currentMSecsSinceEpoch() - static_cast<qint64>(seconds * 1000);
    
    m_decodeThread->seekTo(seconds);
    emit positionChanged(seconds);
}

void FFmpegPlayer::setVolume(int volume)
{
    m_volume = qBound(0, volume, 100);
    if (m_audioSink) {
        m_audioSink->setVolume(m_volume / 100.0);
    }
}

int FFmpegPlayer::videoWidth() const
{
    return m_decodeThread->videoWidth();
}

int FFmpegPlayer::videoHeight() const
{
    return m_decodeThread->videoHeight();
}

void FFmpegPlayer::onFileOpened()
{
    m_duration = m_decodeThread->duration();
    emit durationChanged(m_duration);
    emit fileLoaded();
}

void FFmpegPlayer::onDecodingFinished()
{
    qDebug() << "解码完成, 循环播放:" << m_loop;
    
    if (m_loop && m_state == PlayingState) {
        // 循环播放
        seek(0);
        m_decodeThread->startDecoding();
    } else {
        stop();
        emit endOfFile();
    }
}

void FFmpegPlayer::onDecodeError(const QString &error)
{
    stop();
    emit errorOccurred(error);
}

void FFmpegPlayer::processVideo()
{
    if (m_state != PlayingState) return;
    
    VideoFrame frame;
    while (m_decodeThread->getVideoFrame(frame)) {
        // 使用音频时钟进行同步
        double targetTime = (m_audioClock > 0) ? m_audioClock : m_currentPosition;
        
        // 如果帧太旧，跳过
        if (frame.pts < targetTime - 0.1) {
            continue;
        }
        
        // 如果帧太新，放回队列（这里简化处理，直接显示）
        if (frame.pts > targetTime + 0.05) {
            // 稍后再处理
            break;
        }
        
        m_currentPosition = frame.pts;
        emit positionChanged(m_currentPosition);
        emit frameReady(frame.image);
        break;
    }
}

void FFmpegPlayer::processAudio()
{
    if (m_state != PlayingState || !m_audioDevice) return;
    
    AudioFrame frame;
    while (m_decodeThread->getAudioFrame(frame)) {
        // 应用音量
        if (m_volume < 100) {
            int16_t *samples = reinterpret_cast<int16_t*>(frame.data.data());
            int count = frame.data.size() / 2;
            for (int i = 0; i < count; i++) {
                samples[i] = static_cast<int16_t>(samples[i] * m_volume / 100);
            }
        }
        
        // 写入音频设备
        m_audioDevice->write(frame.data);
        
        // 更新音频时钟
        m_audioClock = frame.pts + static_cast<double>(frame.data.size()) / (44100 * 2 * 2);
    }
}

void FFmpegPlayer::setupAudio()
{
    cleanupAudio();
    
    QAudioFormat format = m_decodeThread->audioFormat();
    if (!format.isValid()) {
        qWarning() << "Invalid audio format";
        return;
    }
    
    m_audioSink = std::make_unique<QAudioSink>(format);
    m_audioSink->setVolume(m_volume / 100.0);
    m_audioDevice = m_audioSink->start();
}

void FFmpegPlayer::cleanupAudio()
{
    if (m_audioSink) {
        m_audioSink->stop();
        m_audioSink.reset();
    }
    m_audioDevice = nullptr;
}

void FFmpegPlayer::setState(PlaybackState state)
{
    if (m_state != state) {
        m_state = state;
        emit stateChanged(state);
    }
}
