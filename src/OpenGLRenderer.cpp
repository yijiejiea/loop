/**
 * @file OpenGLRenderer.cpp
 * @brief OpenGL 视频渲染器实现（跨平台）
 */

#include "OpenGLRenderer.h"
#include <QDebug>
#include <QAudioFormat>

// YUV → RGB 顶点着色器
static const char* g_vertexShader = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord;
out vec2 TexCoord;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    TexCoord = aTexCoord;
}
)";

// YUV → RGB 片段着色器
static const char* g_fragmentShader = R"(
#version 330 core
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D textureY;
uniform sampler2D textureU;
uniform sampler2D textureV;
void main() {
    float y = texture(textureY, TexCoord).r;
    float u = texture(textureU, TexCoord).r - 0.5;
    float v = texture(textureV, TexCoord).r - 0.5;
    
    // BT.709 YUV to RGB
    float r = y + 1.5748 * v;
    float g = y - 0.1873 * u - 0.4681 * v;
    float b = y + 1.8556 * u;
    
    FragColor = vec4(clamp(r, 0.0, 1.0), clamp(g, 0.0, 1.0), clamp(b, 0.0, 1.0), 1.0);
}
)";

// 顶点数据（位置 + 纹理坐标）
static const float g_vertices[] = {
    // 位置      // 纹理坐标
    -1.0f,  1.0f,  0.0f, 0.0f,  // 左上
    -1.0f, -1.0f,  0.0f, 1.0f,  // 左下
     1.0f,  1.0f,  1.0f, 0.0f,  // 右上
     1.0f, -1.0f,  1.0f, 1.0f,  // 右下
};

OpenGLRenderer::OpenGLRenderer(QWidget *parent)
    : QOpenGLWidget(parent)
{
    m_renderTimer = new QTimer(this);
    m_audioTimer = new QTimer(this);
    
    connect(m_renderTimer, &QTimer::timeout, this, &OpenGLRenderer::onRenderTimer);
    connect(m_audioTimer, &QTimer::timeout, this, &OpenGLRenderer::onAudioTimer);
    
    m_volume = 50;
    qDebug() << "OpenGLRenderer 创建";
}

OpenGLRenderer::~OpenGLRenderer()
{
    closeFile();
    
    makeCurrent();
    if (m_textureY) glDeleteTextures(1, &m_textureY);
    if (m_textureU) glDeleteTextures(1, &m_textureU);
    if (m_textureV) glDeleteTextures(1, &m_textureV);
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
    doneCurrent();
}

void OpenGLRenderer::initializeGL()
{
    initializeOpenGLFunctions();
    
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    
    // 创建着色器程序
    m_shader = std::make_unique<QOpenGLShaderProgram>();
    m_shader->addShaderFromSourceCode(QOpenGLShader::Vertex, g_vertexShader);
    m_shader->addShaderFromSourceCode(QOpenGLShader::Fragment, g_fragmentShader);
    m_shader->link();
    
    // 创建 VAO 和 VBO
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(g_vertices), g_vertices, GL_STATIC_DRAW);
    
    // 位置属性
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // 纹理坐标属性
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    
    // 创建 YUV 纹理
    glGenTextures(1, &m_textureY);
    glGenTextures(1, &m_textureU);
    glGenTextures(1, &m_textureV);
    
    auto setupTexture = [this](GLuint tex) {
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    };
    
    setupTexture(m_textureY);
    setupTexture(m_textureU);
    setupTexture(m_textureV);
    
    m_glInitialized = true;
    qDebug() << "OpenGL 初始化完成，版本:" << (const char*)glGetString(GL_VERSION);
}

void OpenGLRenderer::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}

void OpenGLRenderer::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT);
    
    if (!m_hasNewFrame || m_currentFrame.width == 0) return;
    
    // 上传纹理数据
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_textureY);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, m_currentFrame.yLinesize, m_currentFrame.height,
                 0, GL_RED, GL_UNSIGNED_BYTE, m_currentFrame.yPlane.data());
    
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_textureU);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, m_currentFrame.uLinesize, m_currentFrame.height / 2,
                 0, GL_RED, GL_UNSIGNED_BYTE, m_currentFrame.uPlane.data());
    
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_textureV);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, m_currentFrame.vLinesize, m_currentFrame.height / 2,
                 0, GL_RED, GL_UNSIGNED_BYTE, m_currentFrame.vPlane.data());
    
    // 渲染
    m_shader->bind();
    m_shader->setUniformValue("textureY", 0);
    m_shader->setUniformValue("textureU", 1);
    m_shader->setUniformValue("textureV", 2);
    
    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

bool OpenGLRenderer::openFile(const QString &filename)
{
#if FFMPEG_AVAILABLE
    closeFile();
    
    if (avformat_open_input(&m_formatCtx, filename.toUtf8().constData(), nullptr, nullptr) < 0) {
        emit errorOccurred("无法打开文件");
        return false;
    }
    
    if (avformat_find_stream_info(m_formatCtx, nullptr) < 0) {
        emit errorOccurred("无法获取流信息");
        closeFile();
        return false;
    }
    
    // 查找视频流
    for (unsigned int i = 0; i < m_formatCtx->nb_streams; i++) {
        if (m_formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            m_videoStreamIndex = i;
        } else if (m_formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            m_audioStreamIndex = i;
        }
    }
    
    if (m_videoStreamIndex < 0) {
        emit errorOccurred("未找到视频流");
        closeFile();
        return false;
    }
    
    m_duration = m_formatCtx->duration / (double)AV_TIME_BASE;
    
    // 初始化视频解码器
    AVCodecParameters *codecpar = m_formatCtx->streams[m_videoStreamIndex]->codecpar;
    const AVCodec *codec = avcodec_find_decoder(codecpar->codec_id);
    if (!codec) {
        emit errorOccurred("未找到解码器");
        closeFile();
        return false;
    }
    
    m_videoCodecCtx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(m_videoCodecCtx, codecpar);
    
    // 尝试硬件解码
    if (m_decodeMode != Software) {
        if (!initHardwareDecoder(codec)) {
            if (m_decodeMode == Hardware) {
                emit errorOccurred("硬件解码初始化失败");
                closeFile();
                return false;
            }
            qWarning() << "硬件解码不可用，使用软件解码";
        }
    }
    
    if (avcodec_open2(m_videoCodecCtx, codec, nullptr) < 0) {
        emit errorOccurred("无法打开视频解码器");
        closeFile();
        return false;
    }
    
    m_videoWidth = m_videoCodecCtx->width;
    m_videoHeight = m_videoCodecCtx->height;
    
    // 初始化音频解码器
    if (m_audioStreamIndex >= 0) {
        AVCodecParameters *audioCodecpar = m_formatCtx->streams[m_audioStreamIndex]->codecpar;
        const AVCodec *audioCodec = avcodec_find_decoder(audioCodecpar->codec_id);
        if (audioCodec) {
            m_audioCodecCtx = avcodec_alloc_context3(audioCodec);
            avcodec_parameters_to_context(m_audioCodecCtx, audioCodecpar);
            
            if (avcodec_open2(m_audioCodecCtx, audioCodec, nullptr) == 0) {
                m_swrCtx = swr_alloc();
                AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_STEREO;
                AVChannelLayout inLayout = m_audioCodecCtx->ch_layout;
                
                swr_alloc_set_opts2(&m_swrCtx,
                    &outLayout, AV_SAMPLE_FMT_S16, 44100,
                    &inLayout, m_audioCodecCtx->sample_fmt, m_audioCodecCtx->sample_rate,
                    0, nullptr);
                swr_init(m_swrCtx);
            }
        }
    }
    
    qDebug() << "========================================";
    qDebug() << "OpenGL 播放器 - 文件已打开:" << filename;
    qDebug() << "时长:" << m_duration << "秒";
    qDebug() << "视频:" << m_videoWidth << "x" << m_videoHeight;
    qDebug() << "硬件解码:" << (m_hwDeviceCtx ? "是" : "否");
    qDebug() << "========================================";
    
    m_currentFile = filename;
    emit fileLoaded();
    return true;
#else
    emit errorOccurred("FFmpeg 未配置");
    return false;
#endif
}

bool OpenGLRenderer::initHardwareDecoder(const AVCodec *codec)
{
#if FFMPEG_AVAILABLE
    // 根据平台选择硬件解码器
    AVHWDeviceType hwType = AV_HWDEVICE_TYPE_NONE;
    
#ifdef __APPLE__
    hwType = AV_HWDEVICE_TYPE_VIDEOTOOLBOX;
#elif defined(__linux__)
    // 尝试 VAAPI，如果失败尝试 VDPAU
    hwType = AV_HWDEVICE_TYPE_VAAPI;
#elif defined(_WIN32)
    hwType = AV_HWDEVICE_TYPE_DXVA2;
#endif
    
    if (hwType == AV_HWDEVICE_TYPE_NONE) return false;
    
    // 检查编解码器是否支持该硬件类型
    for (int i = 0;; i++) {
        const AVCodecHWConfig *config = avcodec_get_hw_config(codec, i);
        if (!config) break;
        
        if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
            config->device_type == hwType) {
            
            if (av_hwdevice_ctx_create(&m_hwDeviceCtx, hwType, nullptr, nullptr, 0) == 0) {
                m_videoCodecCtx->hw_device_ctx = av_buffer_ref(m_hwDeviceCtx);
                qDebug() << "✓ 硬件解码已启用:" << av_hwdevice_get_type_name(hwType);
                return true;
            }
        }
    }
    
#ifdef __linux__
    // Linux: 如果 VAAPI 失败，尝试 VDPAU
    for (int i = 0;; i++) {
        const AVCodecHWConfig *config = avcodec_get_hw_config(codec, i);
        if (!config) break;
        
        if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
            config->device_type == AV_HWDEVICE_TYPE_VDPAU) {
            
            if (av_hwdevice_ctx_create(&m_hwDeviceCtx, AV_HWDEVICE_TYPE_VDPAU, nullptr, nullptr, 0) == 0) {
                m_videoCodecCtx->hw_device_ctx = av_buffer_ref(m_hwDeviceCtx);
                qDebug() << "✓ VDPAU 硬件解码已启用";
                return true;
            }
        }
    }
#endif
    
    return false;
#else
    Q_UNUSED(codec)
    return false;
#endif
}

void OpenGLRenderer::closeFile()
{
#if FFMPEG_AVAILABLE
    m_running = false;
    m_frameCondition.wakeAll();
    
    if (m_decodeThread && m_decodeThread->isRunning()) {
        m_decodeThread->quit();
        m_decodeThread->wait(1000);
    }
    m_decodeThread.reset();
    
    {
        QMutexLocker locker(&m_frameMutex);
        m_frameQueue.clear();
    }
    {
        QMutexLocker locker(&m_audioMutex);
        m_audioQueue.clear();
    }
    
    if (m_swrCtx) {
        swr_free(&m_swrCtx);
        m_swrCtx = nullptr;
    }
    
    if (m_swsCtx) {
        sws_freeContext(m_swsCtx);
        m_swsCtx = nullptr;
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
#endif
}

void OpenGLRenderer::play()
{
#if FFMPEG_AVAILABLE
    if (!m_formatCtx) return;
    
    if (!m_decodeThread) {
        m_running = true;
        m_decodeThread = std::make_unique<QThread>();
        
        connect(m_decodeThread.get(), &QThread::started, this, &OpenGLRenderer::decodeThread);
        m_decodeThread->start();
    }
    
    setupAudio();
    
    m_playing = true;
    m_paused = false;
    
    m_renderTimer->start(16);  // ~60fps
    m_audioTimer->start(10);
    
    emit playbackStateChanged(true);
#endif
}

void OpenGLRenderer::pause()
{
    m_paused = true;
    emit playbackStateChanged(false);
}

void OpenGLRenderer::stop()
{
    m_playing = false;
    m_paused = false;
    m_currentPts = 0;
    m_audioClock = 0;
    
    m_renderTimer->stop();
    m_audioTimer->stop();
    
    m_running = false;
    m_frameCondition.wakeAll();
    
    if (m_decodeThread && m_decodeThread->isRunning()) {
        m_decodeThread->quit();
        m_decodeThread->wait(1000);
    }
    
    cleanupAudio();
    
    {
        QMutexLocker locker(&m_frameMutex);
        m_frameQueue.clear();
    }
    
    emit positionChanged(0);
    emit playbackStateChanged(false);
}

void OpenGLRenderer::togglePause()
{
    if (m_playing && !m_paused) {
        pause();
    } else {
        play();
    }
}

void OpenGLRenderer::seek(double seconds)
{
    seconds = qBound(0.0, seconds, m_duration);
    m_seekTarget = seconds;
    m_seeking = true;
    m_currentPts = seconds;
    m_audioClock = seconds;
    emit positionChanged(seconds);
}

void OpenGLRenderer::setVolume(int volume)
{
    m_volume = qBound(0, volume, 100);
    if (m_audioSink) {
        m_audioSink->setVolume(m_volume / 100.0f);
    }
}

void OpenGLRenderer::setupAudio()
{
    cleanupAudio();
    
    QAudioFormat format;
    format.setSampleRate(44100);
    format.setChannelCount(2);
    format.setSampleFormat(QAudioFormat::Int16);
    
    m_audioSink = std::make_unique<QAudioSink>(format);
    m_audioSink->setVolume(m_volume / 100.0f);
    m_audioDevice = m_audioSink->start();
}

void OpenGLRenderer::cleanupAudio()
{
    if (m_audioSink) {
        m_audioSink->stop();
        m_audioSink.reset();
    }
    m_audioDevice = nullptr;
}

void OpenGLRenderer::decodeThread()
{
#if FFMPEG_AVAILABLE
    if (!m_formatCtx) return;
    
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    AVFrame *swFrame = av_frame_alloc();  // 用于硬件解码时的软件帧
    
    while (m_running) {
        // 处理 seek
        if (m_seeking) {
            int64_t timestamp = static_cast<int64_t>(m_seekTarget * AV_TIME_BASE);
            av_seek_frame(m_formatCtx, -1, timestamp, AVSEEK_FLAG_BACKWARD);
            
            if (m_videoCodecCtx) avcodec_flush_buffers(m_videoCodecCtx);
            if (m_audioCodecCtx) avcodec_flush_buffers(m_audioCodecCtx);
            
            {
                QMutexLocker locker(&m_frameMutex);
                m_frameQueue.clear();
            }
            {
                QMutexLocker locker(&m_audioMutex);
                m_audioQueue.clear();
            }
            m_seeking = false;
        }
        
        int ret = av_read_frame(m_formatCtx, packet);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                if (m_loop) {
                    av_seek_frame(m_formatCtx, -1, 0, AVSEEK_FLAG_BACKWARD);
                    if (m_videoCodecCtx) avcodec_flush_buffers(m_videoCodecCtx);
                    if (m_audioCodecCtx) avcodec_flush_buffers(m_audioCodecCtx);
                    continue;
                }
                emit endOfFile();
            }
            break;
        }
        
        // 视频解码
        if (packet->stream_index == m_videoStreamIndex && m_videoCodecCtx) {
            ret = avcodec_send_packet(m_videoCodecCtx, packet);
            while (ret >= 0) {
                ret = avcodec_receive_frame(m_videoCodecCtx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                if (ret < 0) break;
                
                AVFrame *srcFrame = frame;
                
                // 硬件解码：传输到软件帧
                if (m_hwDeviceCtx && frame->format != AV_PIX_FMT_YUV420P) {
                    if (av_hwframe_transfer_data(swFrame, frame, 0) < 0) {
                        continue;
                    }
                    srcFrame = swFrame;
                }
                
                double pts = 0;
                AVStream *stream = m_formatCtx->streams[m_videoStreamIndex];
                if (frame->pts != AV_NOPTS_VALUE) {
                    pts = frame->pts * av_q2d(stream->time_base);
                }
                
                // 转换到 YUV420P
                AVPixelFormat srcFmt = static_cast<AVPixelFormat>(srcFrame->format);
                if (srcFmt != AV_PIX_FMT_YUV420P) {
                    if (!m_swsCtx) {
                        m_swsCtx = sws_getContext(
                            m_videoWidth, m_videoHeight, srcFmt,
                            m_videoWidth, m_videoHeight, AV_PIX_FMT_YUV420P,
                            SWS_FAST_BILINEAR, nullptr, nullptr, nullptr
                        );
                    }
                }
                
                FrameData fd;
                fd.width = m_videoWidth;
                fd.height = m_videoHeight;
                fd.pts = pts;
                
                if (m_swsCtx) {
                    // 需要转换
                    fd.yLinesize = m_videoWidth;
                    fd.uLinesize = m_videoWidth / 2;
                    fd.vLinesize = m_videoWidth / 2;
                    fd.yPlane.resize(fd.yLinesize * m_videoHeight);
                    fd.uPlane.resize(fd.uLinesize * m_videoHeight / 2);
                    fd.vPlane.resize(fd.vLinesize * m_videoHeight / 2);
                    
                    uint8_t *dstData[3] = {fd.yPlane.data(), fd.uPlane.data(), fd.vPlane.data()};
                    int dstLinesize[3] = {fd.yLinesize, fd.uLinesize, fd.vLinesize};
                    
                    sws_scale(m_swsCtx, srcFrame->data, srcFrame->linesize, 0, m_videoHeight,
                             dstData, dstLinesize);
                } else {
                    // 直接复制 YUV420P
                    fd.yLinesize = srcFrame->linesize[0];
                    fd.uLinesize = srcFrame->linesize[1];
                    fd.vLinesize = srcFrame->linesize[2];
                    
                    fd.yPlane.assign(srcFrame->data[0], srcFrame->data[0] + fd.yLinesize * m_videoHeight);
                    fd.uPlane.assign(srcFrame->data[1], srcFrame->data[1] + fd.uLinesize * m_videoHeight / 2);
                    fd.vPlane.assign(srcFrame->data[2], srcFrame->data[2] + fd.vLinesize * m_videoHeight / 2);
                }
                
                // 加入队列
                {
                    QMutexLocker locker(&m_frameMutex);
                    while (m_frameQueue.size() >= MAX_FRAME_QUEUE && m_running) {
                        m_frameCondition.wait(&m_frameMutex, 10);
                    }
                    if (m_running) {
                        m_frameQueue.enqueue(fd);
                    }
                }
            }
        }
        
        // 音频解码
        if (packet->stream_index == m_audioStreamIndex && m_audioCodecCtx && m_swrCtx) {
            ret = avcodec_send_packet(m_audioCodecCtx, packet);
            while (ret >= 0) {
                ret = avcodec_receive_frame(m_audioCodecCtx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                if (ret < 0) break;
                
                double pts = 0;
                AVStream *stream = m_formatCtx->streams[m_audioStreamIndex];
                if (frame->pts != AV_NOPTS_VALUE) {
                    pts = frame->pts * av_q2d(stream->time_base);
                }
                
                int outSamples = static_cast<int>(av_rescale_rnd(
                    swr_get_delay(m_swrCtx, m_audioCodecCtx->sample_rate) + frame->nb_samples,
                    44100, m_audioCodecCtx->sample_rate, AV_ROUND_UP));
                
                int bufferSize = outSamples * 2 * 2;
                QByteArray audioData(bufferSize, 0);
                uint8_t *outBuffer = reinterpret_cast<uint8_t*>(audioData.data());
                
                int samples = swr_convert(m_swrCtx, &outBuffer, outSamples,
                                         const_cast<const uint8_t**>(frame->data), frame->nb_samples);
                
                if (samples > 0) {
                    audioData.resize(samples * 2 * 2);
                    
                    AudioData ad;
                    ad.data = audioData;
                    ad.pts = pts;
                    
                    QMutexLocker locker(&m_audioMutex);
                    if (m_audioQueue.size() < 100) {
                        m_audioQueue.enqueue(ad);
                    }
                }
            }
        }
        
        av_packet_unref(packet);
    }
    
    av_frame_free(&swFrame);
    av_frame_free(&frame);
    av_packet_free(&packet);
#endif
}

void OpenGLRenderer::onRenderTimer()
{
    if (!m_glInitialized || !m_playing || m_paused) return;
    
    FrameData frame;
    bool hasFrame = false;
    
    {
        QMutexLocker locker(&m_frameMutex);
        while (!m_frameQueue.isEmpty()) {
            frame = m_frameQueue.dequeue();
            m_frameCondition.wakeOne();
            
            if (frame.pts < m_audioClock - 0.1) {
                continue;
            }
            hasFrame = true;
            break;
        }
    }
    
    if (hasFrame && frame.width > 0) {
        m_currentFrame = std::move(frame);
        m_hasNewFrame = true;
        m_currentPts = m_currentFrame.pts;
        emit positionChanged(m_currentPts);
        update();  // 触发 paintGL
    }
}

void OpenGLRenderer::onAudioTimer()
{
    processAudio();
}

void OpenGLRenderer::processAudio()
{
    if (!m_audioDevice || !m_playing || m_paused) return;
    
    QMutexLocker locker(&m_audioMutex);
    while (!m_audioQueue.isEmpty() && m_audioSink->bytesFree() > 0) {
        AudioData &ad = m_audioQueue.head();
        
        qint64 written = m_audioDevice->write(ad.data);
        if (written > 0) {
            m_audioClock = ad.pts + (written / 4.0 / 44100.0);
            ad.data.remove(0, written);
            
            if (ad.data.isEmpty()) {
                m_audioQueue.dequeue();
            }
        } else {
            break;
        }
    }
}

