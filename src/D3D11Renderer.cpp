#include "D3D11Renderer.h"
#include <QDebug>
#include <QResizeEvent>
#include <QPainter>
#include <QDateTime>
#include <d3dcompiler.h>
#include <d3d10.h>  // ID3D10Multithread

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

// NV12 → RGB 像素着色器
static const char* g_pixelShaderNV12 = R"(
Texture2D texY : register(t0);
Texture2D texUV : register(t1);
SamplerState samp : register(s0);

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float2 tex : TEXCOORD0;
};

float4 main(PS_INPUT input) : SV_TARGET {
    float y = texY.Sample(samp, input.tex).r;
    float2 uv = texUV.Sample(samp, input.tex).rg;
    
    // YUV (BT.709) → RGB
    float u = uv.r - 0.5;
    float v = uv.g - 0.5;
    
    float r = y + 1.5748 * v;
    float g = y - 0.1873 * u - 0.4681 * v;
    float b = y + 1.8556 * u;
    
    return float4(saturate(r), saturate(g), saturate(b), 1.0);
}
)";

// 顶点着色器
static const char* g_vertexShader = R"(
struct VS_INPUT {
    float3 pos : POSITION;
    float2 tex : TEXCOORD0;
};

struct VS_OUTPUT {
    float4 pos : SV_POSITION;
    float2 tex : TEXCOORD0;
};

VS_OUTPUT main(VS_INPUT input) {
    VS_OUTPUT output;
    output.pos = float4(input.pos, 1.0);
    output.tex = input.tex;
    return output;
}
)";

// 顶点结构
struct Vertex {
    float x, y, z;
    float u, v;
};

D3D11Renderer::D3D11Renderer(QWidget *parent)
    : QWidget(parent)
{
    // 设置窗口属性
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAutoFillBackground(false);
    
    // 初始化 D3D11
    if (!initD3D11()) {
        qCritical() << "Failed to initialize D3D11";
    }
    
    // 渲染定时器
    m_renderTimer = new QTimer(this);
    m_renderTimer->setTimerType(Qt::PreciseTimer);
    connect(m_renderTimer, &QTimer::timeout, this, &D3D11Renderer::onRenderTimer);
    
    // 音频定时器
    m_audioTimer = new QTimer(this);
    m_audioTimer->setTimerType(Qt::PreciseTimer);
    connect(m_audioTimer, &QTimer::timeout, this, &D3D11Renderer::onAudioTimer);
}

D3D11Renderer::~D3D11Renderer()
{
    stop();
    closeFile();
    cleanupD3D11();
}

bool D3D11Renderer::initD3D11()
{
#ifdef _WIN32
    // 创建 D3D11 设备
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };
    
    UINT createFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    createFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    
    D3D_FEATURE_LEVEL featureLevel;
    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        createFlags,
        featureLevels,
        ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        &m_device,
        &featureLevel,
        &m_context
    );
    
    if (FAILED(hr)) {
        qCritical() << "D3D11CreateDevice failed:" << hr;
        return false;
    }
    
    // 【重要】启用 D3D11 多线程保护
    // 解码线程和渲染线程会同时访问 Device/Context
    ComPtr<ID3D10Multithread> multithread;
    if (SUCCEEDED(m_device.As(&multithread))) {
        multithread->SetMultithreadProtected(TRUE);
        qDebug() << "D3D11 multithread protection enabled";
    }
    
    qDebug() << "D3D11 initialized, feature level:" << featureLevel;
    
    // 创建交换链
    if (!createSwapChain()) {
        return false;
    }
    
    // 创建着色器
    if (!createShaders()) {
        return false;
    }
    
    // 创建采样器
    if (!createSamplerState()) {
        return false;
    }
    
    m_d3dInitialized = true;
    return true;
#else
    return false;
#endif
}

bool D3D11Renderer::createSwapChain()
{
#ifdef _WIN32
    // 获取 DXGI 工厂
    ComPtr<IDXGIDevice> dxgiDevice;
    m_device.As(&dxgiDevice);
    
    ComPtr<IDXGIAdapter> adapter;
    dxgiDevice->GetAdapter(&adapter);
    
    ComPtr<IDXGIFactory2> factory;
    adapter->GetParent(IID_PPV_ARGS(&factory));
    
    // 交换链描述
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.Width = width() > 0 ? width() : 400;
    swapChainDesc.Height = height() > 0 ? height() : 300;
    swapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = 2;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    
    HWND hwnd = reinterpret_cast<HWND>(winId());
    HRESULT hr = factory->CreateSwapChainForHwnd(
        m_device.Get(),
        hwnd,
        &swapChainDesc,
        nullptr,
        nullptr,
        &m_swapChain
    );
    
    if (FAILED(hr)) {
        qCritical() << "CreateSwapChainForHwnd failed:" << hr;
        return false;
    }
    
    // 创建渲染目标视图
    ComPtr<ID3D11Texture2D> backBuffer;
    m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, &m_renderTarget);
    
    return true;
#else
    return false;
#endif
}

bool D3D11Renderer::createShaders()
{
#ifdef _WIN32
    HRESULT hr;
    ComPtr<ID3DBlob> vsBlob, psBlob, errorBlob;
    
    // 编译顶点着色器
    hr = D3DCompile(g_vertexShader, strlen(g_vertexShader), nullptr, nullptr, nullptr,
                    "main", "vs_5_0", 0, 0, &vsBlob, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) {
            qCritical() << "VS compile error:" << (char*)errorBlob->GetBufferPointer();
        }
        return false;
    }
    
    hr = m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                                       nullptr, &m_vertexShader);
    if (FAILED(hr)) return false;
    
    // 创建输入布局
    D3D11_INPUT_ELEMENT_DESC inputDesc[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    hr = m_device->CreateInputLayout(inputDesc, 2, vsBlob->GetBufferPointer(),
                                      vsBlob->GetBufferSize(), &m_inputLayout);
    if (FAILED(hr)) return false;
    
    // 编译像素着色器
    hr = D3DCompile(g_pixelShaderNV12, strlen(g_pixelShaderNV12), nullptr, nullptr, nullptr,
                    "main", "ps_5_0", 0, 0, &psBlob, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) {
            qCritical() << "PS compile error:" << (char*)errorBlob->GetBufferPointer();
        }
        return false;
    }
    
    hr = m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
                                      nullptr, &m_pixelShader);
    if (FAILED(hr)) return false;
    
    // 创建顶点缓冲（全屏四边形）
    Vertex vertices[] = {
        {-1.0f,  1.0f, 0.0f, 0.0f, 0.0f},  // 左上
        { 1.0f,  1.0f, 0.0f, 1.0f, 0.0f},  // 右上
        {-1.0f, -1.0f, 0.0f, 0.0f, 1.0f},  // 左下
        { 1.0f, -1.0f, 0.0f, 1.0f, 1.0f},  // 右下
    };
    
    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.ByteWidth = sizeof(vertices);
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    
    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = vertices;
    
    hr = m_device->CreateBuffer(&bufferDesc, &initData, &m_vertexBuffer);
    if (FAILED(hr)) return false;
    
    qDebug() << "D3D11 shaders created successfully";
    return true;
#else
    return false;
#endif
}

bool D3D11Renderer::createSamplerState()
{
#ifdef _WIN32
    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    
    return SUCCEEDED(m_device->CreateSamplerState(&samplerDesc, &m_sampler));
#else
    return false;
#endif
}

void D3D11Renderer::resizeSwapChain()
{
#ifdef _WIN32
    if (!m_swapChain || width() <= 0 || height() <= 0) return;
    
    // 锁定 D3D11 上下文
    QMutexLocker d3dLock(&m_d3dMutex);
    
    m_context->OMSetRenderTargets(0, nullptr, nullptr);
    m_renderTarget.Reset();
    
    HRESULT hr = m_swapChain->ResizeBuffers(0, width(), height(), DXGI_FORMAT_UNKNOWN, 
                                             DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING);
    if (FAILED(hr)) {
        qWarning() << "ResizeBuffers failed:" << hr;
        return;
    }
    
    ComPtr<ID3D11Texture2D> backBuffer;
    m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, &m_renderTarget);
#endif
}

void D3D11Renderer::cleanupD3D11()
{
#ifdef _WIN32
    m_sampler.Reset();
    m_textureSRV_Y.Reset();
    m_textureSRV_UV.Reset();
    m_vertexBuffer.Reset();
    m_inputLayout.Reset();
    m_pixelShader.Reset();
    m_vertexShader.Reset();
    m_renderTarget.Reset();
    m_swapChain.Reset();
    m_context.Reset();
    m_device.Reset();
    m_d3dInitialized = false;
#endif
}

bool D3D11Renderer::openFile(const QString &filename)
{
#if FFMPEG_AVAILABLE
    closeFile();
    
    m_formatCtx = avformat_alloc_context();
    if (avformat_open_input(&m_formatCtx, filename.toUtf8().constData(), nullptr, nullptr) != 0) {
        emit errorOccurred("无法打开文件: " + filename);
        return false;
    }
    
    if (avformat_find_stream_info(m_formatCtx, nullptr) < 0) {
        emit errorOccurred("无法获取流信息");
        closeFile();
        return false;
    }
    
    if (m_formatCtx->duration != AV_NOPTS_VALUE) {
        m_duration = static_cast<double>(m_formatCtx->duration) / AV_TIME_BASE;
        emit durationChanged(m_duration);
    }
    
    // 查找视频流和音频流
    for (unsigned int i = 0; i < m_formatCtx->nb_streams; i++) {
        if (m_formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            m_videoStreamIndex = i;
        } else if (m_formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            m_audioStreamIndex = i;
        }
    }
    
    // 初始化视频解码器（D3D11VA 硬件加速）
    if (m_videoStreamIndex >= 0) {
        AVCodecParameters *codecpar = m_formatCtx->streams[m_videoStreamIndex]->codecpar;
        const AVCodec *codec = avcodec_find_decoder(codecpar->codec_id);
        if (!codec) {
            emit errorOccurred("找不到视频解码器");
            closeFile();
            return false;
        }
        
        m_videoCodecCtx = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(m_videoCodecCtx, codecpar);
        
        // 初始化 D3D11VA 硬件解码
        if (!initHardwareDecoder(codec)) {
            qWarning() << "D3D11VA 硬件解码初始化失败，使用软件解码";
        }
        
        if (avcodec_open2(m_videoCodecCtx, codec, nullptr) < 0) {
            emit errorOccurred("无法打开视频解码器");
            closeFile();
            return false;
        }
        
        m_videoWidth = m_videoCodecCtx->width;
        m_videoHeight = m_videoCodecCtx->height;
    }
    
    // 初始化音频解码器
    if (m_audioStreamIndex >= 0) {
        AVCodecParameters *codecpar = m_formatCtx->streams[m_audioStreamIndex]->codecpar;
        const AVCodec *codec = avcodec_find_decoder(codecpar->codec_id);
        if (codec) {
            m_audioCodecCtx = avcodec_alloc_context3(codec);
            avcodec_parameters_to_context(m_audioCodecCtx, codecpar);
            
            if (avcodec_open2(m_audioCodecCtx, codec, nullptr) == 0) {
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
    qDebug() << "D3D11 播放器 - 文件已打开:" << filename;
    qDebug() << "时长:" << m_duration << "秒";
    qDebug() << "视频:" << m_videoWidth << "x" << m_videoHeight;
    qDebug() << "硬件解码:" << (m_hwDeviceCtx ? "D3D11VA" : "软件");
    qDebug() << "========================================";
    
    m_currentFile = filename;
    emit fileLoaded();
    return true;
#else
    emit errorOccurred("FFmpeg 未配置");
    return false;
#endif
}

bool D3D11Renderer::initHardwareDecoder(const AVCodec *codec)
{
#if FFMPEG_AVAILABLE && defined(_WIN32)
    // 查找 D3D11VA 配置
    for (int i = 0;; i++) {
        const AVCodecHWConfig *config = avcodec_get_hw_config(codec, i);
        if (!config) break;
        
        if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
            config->device_type == AV_HWDEVICE_TYPE_D3D11VA) {
            
            // 使用已有的 D3D11 设备创建 FFmpeg 硬件上下文
            AVBufferRef *hwDeviceCtx = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
            if (!hwDeviceCtx) return false;
            
            AVHWDeviceContext *deviceCtx = (AVHWDeviceContext*)hwDeviceCtx->data;
            AVD3D11VADeviceContext *d3d11Ctx = (AVD3D11VADeviceContext*)deviceCtx->hwctx;
            
            // 使用我们创建的 D3D11 设备
            d3d11Ctx->device = m_device.Get();
            d3d11Ctx->device_context = m_context.Get();
            
            // 增加引用计数
            m_device->AddRef();
            m_context->AddRef();
            
            if (av_hwdevice_ctx_init(hwDeviceCtx) < 0) {
                av_buffer_unref(&hwDeviceCtx);
                return false;
            }
            
            m_hwDeviceCtx = hwDeviceCtx;
            m_videoCodecCtx->hw_device_ctx = av_buffer_ref(m_hwDeviceCtx);
            
            qDebug() << "✓ D3D11VA 硬件解码已启用（共享设备）";
            return true;
        }
    }
    return false;
#else
    Q_UNUSED(codec)
    return false;
#endif
}

void D3D11Renderer::closeFile()
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

void D3D11Renderer::loadFile(const QString &filename)
{
    stop();
    if (openFile(filename)) {
        play();
    }
}

void D3D11Renderer::play()
{
    if (m_playing && !m_paused) return;
    
    if (!m_playing) {
        setupAudio();
        
        // 启动解码线程
        m_running = true;
        m_decodeThread = std::make_unique<QThread>();
        QThread *thread = m_decodeThread.get();
        
        connect(thread, &QThread::started, [this]() {
            decodeThread();
        });
        
        thread->start();
    }
    
    m_playing = true;
    m_paused = false;
    
    m_renderTimer->start(16);  // ~60 fps
    m_audioTimer->start(5);
    
    emit playbackStateChanged(true);
}

void D3D11Renderer::pause()
{
    if (!m_playing) return;
    
    m_paused = true;
    m_renderTimer->stop();
    m_audioTimer->stop();
    
    emit playbackStateChanged(false);
}

void D3D11Renderer::stop()
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

void D3D11Renderer::togglePause()
{
    if (m_playing && !m_paused) {
        pause();
    } else {
        play();
    }
}

void D3D11Renderer::seek(double seconds)
{
    seconds = qBound(0.0, seconds, m_duration);
    m_seekTarget = seconds;
    m_seeking = true;
    m_currentPts = seconds;
    m_audioClock = seconds;
    emit positionChanged(seconds);
}

void D3D11Renderer::setVolume(int volume)
{
    m_volume = qBound(0, volume, 100);
    if (m_audioSink) {
        m_audioSink->setVolume(m_volume / 100.0f);
    }
}

void D3D11Renderer::decodeThread()
{
#if FFMPEG_AVAILABLE && defined(_WIN32)
    if (!m_formatCtx) return;
    
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    
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
                
                // D3D11VA 解码：frame->data[0] 是 ID3D11Texture2D
                // frame->data[1] 是 texture array index
                if (m_hwDeviceCtx && frame->format == AV_PIX_FMT_D3D11) {
                    ID3D11Texture2D *texture = reinterpret_cast<ID3D11Texture2D*>(frame->data[0]);
                    int textureIndex = reinterpret_cast<intptr_t>(frame->data[1]);
                    
                    double pts = 0;
                    AVStream *stream = m_formatCtx->streams[m_videoStreamIndex];
                    if (frame->pts != AV_NOPTS_VALUE) {
                        pts = frame->pts * av_q2d(stream->time_base);
                    }
                    
                    VideoFrame vf;
                    // 复制纹理（因为解码器会复用）
                    D3D11_TEXTURE2D_DESC desc;
                    texture->GetDesc(&desc);
                    
                    desc.Usage = D3D11_USAGE_DEFAULT;
                    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                    desc.MiscFlags = 0;
                    desc.ArraySize = 1;
                    
                    ComPtr<ID3D11Texture2D> copyTexture;
                    
                    // 【重要】锁定 D3D11 上下文进行纹理复制
                    {
                        QMutexLocker d3dLock(&m_d3dMutex);
                        if (SUCCEEDED(m_device->CreateTexture2D(&desc, nullptr, &copyTexture))) {
                            m_context->CopySubresourceRegion(
                                copyTexture.Get(), 0, 0, 0, 0,
                                texture, textureIndex, nullptr
                            );
                        }
                    }
                    
                    if (copyTexture) {
                        vf.texture = copyTexture;
                        vf.textureIndex = 0;
                        vf.pts = pts;
                        
                        // 加入队列
                        {
                            QMutexLocker locker(&m_frameMutex);
                            while (m_frameQueue.size() >= MAX_FRAME_QUEUE && m_running) {
                                m_frameCondition.wait(&m_frameMutex, 10);
                            }
                            if (m_running) {
                                m_frameQueue.enqueue(vf);
                            }
                        }
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
    
    av_frame_free(&frame);
    av_packet_free(&packet);
#endif
}

void D3D11Renderer::onRenderTimer()
{
#ifdef _WIN32
    if (!m_d3dInitialized || !m_playing || m_paused) return;
    
    VideoFrame frame;
    bool hasFrame = false;
    
    {
        QMutexLocker locker(&m_frameMutex);
        while (!m_frameQueue.isEmpty()) {
            frame = m_frameQueue.dequeue();
            m_frameCondition.wakeOne();
            
            // 同步：跳过太旧的帧
            if (frame.pts < m_audioClock - 0.1) {
                continue;
            }
            hasFrame = true;
            break;
        }
    }
    
    if (hasFrame && frame.texture) {
        renderNV12Frame(frame.texture.Get(), frame.textureIndex);
        m_currentPts = frame.pts;
        emit positionChanged(m_currentPts);
    }
#endif
}

void D3D11Renderer::renderNV12Frame(ID3D11Texture2D *texture, int textureIndex)
{
#ifdef _WIN32
    if (!m_device || !m_context || !m_swapChain) return;
    
    // 【重要】锁定 D3D11 上下文进行渲染
    QMutexLocker d3dLock(&m_d3dMutex);
    
    // 创建 SRV
    D3D11_TEXTURE2D_DESC texDesc;
    texture->GetDesc(&texDesc);
    
    // Y 平面 SRV
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R8_UNORM;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    
    ComPtr<ID3D11ShaderResourceView> srvY, srvUV;
    m_device->CreateShaderResourceView(texture, &srvDesc, &srvY);
    
    // UV 平面 SRV
    srvDesc.Format = DXGI_FORMAT_R8G8_UNORM;
    m_device->CreateShaderResourceView(texture, &srvDesc, &srvUV);
    
    // 设置渲染状态
    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(width());
    viewport.Height = static_cast<float>(height());
    viewport.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &viewport);
    
    m_context->OMSetRenderTargets(1, m_renderTarget.GetAddressOf(), nullptr);
    
    float clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    m_context->ClearRenderTargetView(m_renderTarget.Get(), clearColor);
    
    // 设置着色器
    m_context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
    m_context->PSSetShader(m_pixelShader.Get(), nullptr, 0);
    m_context->IASetInputLayout(m_inputLayout.Get());
    
    UINT stride = sizeof(Vertex);
    UINT offset = 0;
    m_context->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &stride, &offset);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    
    // 设置纹理
    ID3D11ShaderResourceView* srvs[] = {srvY.Get(), srvUV.Get()};
    m_context->PSSetShaderResources(0, 2, srvs);
    m_context->PSSetSamplers(0, 1, m_sampler.GetAddressOf());
    
    // 绘制
    m_context->Draw(4, 0);
    
    // 呈现
    m_swapChain->Present(1, 0);
#else
    Q_UNUSED(texture)
    Q_UNUSED(textureIndex)
#endif
}

void D3D11Renderer::onAudioTimer()
{
    processAudio();
}

void D3D11Renderer::setupAudio()
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

void D3D11Renderer::cleanupAudio()
{
    if (m_audioSink) {
        m_audioSink->stop();
        m_audioSink.reset();
    }
    m_audioDevice = nullptr;
}

void D3D11Renderer::processAudio()
{
    if (!m_playing || m_paused || !m_audioDevice) return;
    
    AudioData ad;
    while (true) {
        {
            QMutexLocker locker(&m_audioMutex);
            if (m_audioQueue.isEmpty()) break;
            ad = m_audioQueue.dequeue();
        }
        
        if (m_volume < 100) {
            int16_t *samples = reinterpret_cast<int16_t*>(ad.data.data());
            int count = ad.data.size() / 2;
            for (int i = 0; i < count; i++) {
                samples[i] = static_cast<int16_t>(samples[i] * m_volume / 100);
            }
        }
        
        m_audioDevice->write(ad.data);
        m_audioClock = ad.pts + static_cast<double>(ad.data.size()) / (44100 * 2 * 2);
    }
}

void D3D11Renderer::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    // D3D11 接管渲染，不使用 Qt 绘制
}

void D3D11Renderer::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    resizeSwapChain();
}

