#include "D3D11Renderer.h"
#include <QDebug>
#include <QResizeEvent>
#include <QPainter>
#include <QDateTime>
#include <d3dcompiler.h>
#include <d3d10.h>  // ID3D10Multithread
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

// NV12 → RGB 像素着色器（硬件解码用）
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

// BGRA 直接采样着色器（软件解码用）
static const char* g_pixelShaderBGRA = R"(
Texture2D tex : register(t0);
SamplerState samp : register(s0);

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float2 tex : TEXCOORD0;
};

float4 main(PS_INPUT input) : SV_TARGET {
    // BGRA 需要交换 R 和 B
    float4 color = tex.Sample(samp, input.tex);
    return float4(color.b, color.g, color.r, color.a);
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
    : VideoRendererBase(parent)
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
    
    // 编译 NV12 像素着色器
    hr = D3DCompile(g_pixelShaderNV12, strlen(g_pixelShaderNV12), nullptr, nullptr, nullptr,
                    "main", "ps_5_0", 0, 0, &psBlob, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) {
            qCritical() << "PS NV12 compile error:" << (char*)errorBlob->GetBufferPointer();
        }
        return false;
    }
    
    hr = m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
                                      nullptr, &m_pixelShader);
    if (FAILED(hr)) return false;
    
    // 编译 BGRA 像素着色器（软件解码用）
    psBlob.Reset();
    errorBlob.Reset();
    hr = D3DCompile(g_pixelShaderBGRA, strlen(g_pixelShaderBGRA), nullptr, nullptr, nullptr,
                    "main", "ps_5_0", 0, 0, &psBlob, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) {
            qCritical() << "PS BGRA compile error:" << (char*)errorBlob->GetBufferPointer();
        }
        return false;
    }
    
    hr = m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
                                      nullptr, &m_pixelShaderBGRA);
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
    m_pixelShaderBGRA.Reset();
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
        
        // 根据解码模式初始化
        if (m_decodeMode == Software) {
            qDebug() << "强制使用软件解码";
        } else {
            // Auto 或 Hardware 模式，尝试硬件解码
            if (!initHardwareDecoder(codec)) {
                if (m_decodeMode == Hardware) {
                    emit errorOccurred("硬件解码初始化失败，且设置为强制硬件模式");
                    closeFile();
                    return false;
                }
                qWarning() << "D3D11VA 硬件解码初始化失败，回退到软件解码";
            }
        }
        
        // 软件解码时创建颜色转换上下文
        if (!m_hwDeviceCtx) {
            // 将在解码时根据实际格式创建 SwsContext
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
    // 停止所有线程
    m_running = false;
    m_frameCondition.wakeAll();
    m_videoPacketCondition.wakeAll();
    m_audioPacketCondition.wakeAll();
    
    // 等待线程结束
    if (m_demuxThread && m_demuxThread->isRunning()) {
        m_demuxThread->quit();
        m_demuxThread->wait(1000);
    }
    m_demuxThread.reset();
    
    if (m_videoDecodeThread && m_videoDecodeThread->isRunning()) {
        m_videoDecodeThread->quit();
        m_videoDecodeThread->wait(1000);
    }
    m_videoDecodeThread.reset();
    
    if (m_audioDecodeThread && m_audioDecodeThread->isRunning()) {
        m_audioDecodeThread->quit();
        m_audioDecodeThread->wait(1000);
    }
    m_audioDecodeThread.reset();
    
    // 清空所有队列
    {
        QMutexLocker locker(&m_frameMutex);
        m_frameQueue.clear();
    }
    {
        QMutexLocker locker(&m_videoPacketMutex);
        while (!m_videoPacketQueue.isEmpty()) {
            AVPacket *pkt = m_videoPacketQueue.dequeue();
            if (pkt) av_packet_free(&pkt);
        }
    }
    {
        QMutexLocker locker(&m_audioPacketMutex);
        while (!m_audioPacketQueue.isEmpty()) {
            AVPacket *pkt = m_audioPacketQueue.dequeue();
            if (pkt) av_packet_free(&pkt);
        }
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
        
#if FFMPEG_AVAILABLE
        // 启动三线程架构
        m_running = true;
        
        // 1. Demux 线程
        m_demuxThread = std::make_unique<QThread>();
        connect(m_demuxThread.get(), &QThread::started, [this]() {
            demuxThread();
        });
        m_demuxThread->start();
        
        // 2. 视频解码线程
        if (m_videoCodecCtx) {
            m_videoDecodeThread = std::make_unique<QThread>();
            connect(m_videoDecodeThread.get(), &QThread::started, [this]() {
                videoDecodeThread();
            });
            m_videoDecodeThread->start();
        }
        
        // 3. 音频解码线程
        if (m_audioCodecCtx && m_swrCtx) {
            m_audioDecodeThread = std::make_unique<QThread>();
            connect(m_audioDecodeThread.get(), &QThread::started, [this]() {
                audioDecodeThread();
            });
            m_audioDecodeThread->start();
        }
        
        qDebug() << "========================================";
        qDebug() << "三线程架构已启动:";
        qDebug() << "  - Demux 线程: 读取 Packet";
        qDebug() << "  - 视频解码线程: D3D11VA 硬件解码";
        qDebug() << "  - 音频解码线程: FFmpeg 软解码";
        qDebug() << "========================================";
#endif
    }
    
    m_playing = true;
    m_paused = false;
    
    // 重置同步状态
    m_audioClockValid = false;
    m_videoClockValid = false;
    m_audioStartPts = 0;
    m_videoStartPts = 0;
    m_avSyncOffset = 0;
    m_audioClock = 0;
    m_audioWrittenBytes = 0;
    m_skipRenderCount = 0;
    m_frameTimer = 0;
    m_lastFramePts = 0;
    m_lastDelay = 0.033;
    m_consecutiveFastRender = 0;
    
    m_renderTimer->start(8);  // ~120 fps 检查（实际帧率由 delay 控制）
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
    m_audioClockValid = false;
    m_videoClockValid = false;
    m_audioStartPts = 0;
    m_videoStartPts = 0;
    m_avSyncOffset = 0;
    m_audioWrittenBytes = 0;
    m_skipRenderCount = 0;
    m_frameTimer = 0;
    m_lastFramePts = 0;
    m_lastDelay = 0.033;
    m_consecutiveFastRender = 0;
    
    m_renderTimer->stop();
    m_audioTimer->stop();
    
    // 停止三线程
    m_running = false;
    
    // 唤醒所有等待的线程
    m_frameCondition.wakeAll();
    m_videoPacketCondition.wakeAll();
    m_audioPacketCondition.wakeAll();
    
    // 等待线程结束
    if (m_demuxThread && m_demuxThread->isRunning()) {
        m_demuxThread->quit();
        m_demuxThread->wait(1000);
    }
    m_demuxThread.reset();
    
    if (m_videoDecodeThread && m_videoDecodeThread->isRunning()) {
        m_videoDecodeThread->quit();
        m_videoDecodeThread->wait(1000);
    }
    m_videoDecodeThread.reset();
    
    if (m_audioDecodeThread && m_audioDecodeThread->isRunning()) {
        m_audioDecodeThread->quit();
        m_audioDecodeThread->wait(1000);
    }
    m_audioDecodeThread.reset();
    
    cleanupAudio();
    
    // 清空所有队列
#if FFMPEG_AVAILABLE
    {
        QMutexLocker locker(&m_frameMutex);
        m_frameQueue.clear();
    }
    {
        QMutexLocker locker(&m_videoPacketMutex);
        while (!m_videoPacketQueue.isEmpty()) {
            AVPacket *pkt = m_videoPacketQueue.dequeue();
            if (pkt) av_packet_free(&pkt);
        }
    }
    {
        QMutexLocker locker(&m_audioPacketMutex);
        while (!m_audioPacketQueue.isEmpty()) {
            AVPacket *pkt = m_audioPacketQueue.dequeue();
            if (pkt) av_packet_free(&pkt);
        }
    }
    {
        QMutexLocker locker(&m_audioMutex);
        m_audioQueue.clear();
    }
#endif
    
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
    
    // 重置同步状态
    m_audioClockValid = false;
    m_videoClockValid = false;
    m_audioStartPts = 0;
    m_videoStartPts = 0;
    m_avSyncOffset = 0;
    m_audioClock = 0;
    m_audioWrittenBytes = 0;
    m_skipRenderCount = 0;
    m_frameTimer = 0;
    m_lastFramePts = 0;
    m_lastDelay = 0.033;
    m_consecutiveFastRender = 0;
    
    // 唤醒可能在等待的线程
    m_videoPacketCondition.wakeAll();
    m_audioPacketCondition.wakeAll();
    m_frameCondition.wakeAll();
    
#if SDL3_AVAILABLE
    // 清空 SDL 音频队列
    if (m_sdlAudioStream) {
        SDL_ClearAudioStream(m_sdlAudioStream);
    }
#endif
    
    emit positionChanged(seconds);
}

void D3D11Renderer::setVolume(int volume)
{
    m_volume = qBound(0, volume, 100);
#if !SDL3_AVAILABLE
    if (m_audioSink) {
        m_audioSink->setVolume(m_volume / 100.0f);
    }
#endif
    // SDL3: 音量在 processAudio() 中处理
}

// ========================================
// Demux 线程：读取 Packet 并分发到音视频队列
// 不做任何解码，只负责 I/O 和分发
// ========================================
void D3D11Renderer::demuxThread()
{
#if FFMPEG_AVAILABLE && defined(_WIN32)
    if (!m_formatCtx) return;
    
    qDebug() << "[Demux] 线程启动";
    
    while (m_running) {
        // 处理 seek
        if (m_seeking) {
            int64_t timestamp = static_cast<int64_t>(m_seekTarget * AV_TIME_BASE);
            av_seek_frame(m_formatCtx, -1, timestamp, AVSEEK_FLAG_BACKWARD);
            
            // 清空 Packet 队列
            {
                QMutexLocker locker(&m_videoPacketMutex);
                while (!m_videoPacketQueue.isEmpty()) {
                    AVPacket *pkt = m_videoPacketQueue.dequeue();
                    if (pkt) av_packet_free(&pkt);
                }
            }
            {
                QMutexLocker locker(&m_audioPacketMutex);
                while (!m_audioPacketQueue.isEmpty()) {
                    AVPacket *pkt = m_audioPacketQueue.dequeue();
                    if (pkt) av_packet_free(&pkt);
                }
            }
            
            m_seeking = false;
            
            // 唤醒解码线程
            m_videoPacketCondition.wakeAll();
            m_audioPacketCondition.wakeAll();
        }
        
        // 读取 Packet
        AVPacket *packet = av_packet_alloc();
        int ret = av_read_frame(m_formatCtx, packet);
        
        if (ret < 0) {
            av_packet_free(&packet);
            
            if (ret == AVERROR_EOF) {
                if (m_loop) {
                    // 循环播放：回到开头
                    av_seek_frame(m_formatCtx, -1, 0, AVSEEK_FLAG_BACKWARD);
                    
                    // 通知解码线程 flush
                    {
                        QMutexLocker locker(&m_videoPacketMutex);
                        // 插入 nullptr 作为 flush 信号
                        m_videoPacketQueue.enqueue(nullptr);
                        m_videoPacketCondition.wakeOne();
                    }
                    {
                        QMutexLocker locker(&m_audioPacketMutex);
                        m_audioPacketQueue.enqueue(nullptr);
                        m_audioPacketCondition.wakeOne();
                    }
                    continue;
                }
                emit endOfFile();
            }
            break;
        }
        
        // 分发到对应队列
        if (packet->stream_index == m_videoStreamIndex) {
            QMutexLocker locker(&m_videoPacketMutex);
            
            // 队列满时等待（不阻塞音频！）
            while (m_videoPacketQueue.size() >= MAX_VIDEO_PACKET_QUEUE && m_running && !m_seeking) {
                m_videoPacketCondition.wait(&m_videoPacketMutex, 10);
            }
            
            if (m_running && !m_seeking) {
                m_videoPacketQueue.enqueue(packet);
                m_videoPacketCondition.wakeOne();
            } else {
                av_packet_free(&packet);
            }
        }
        else if (packet->stream_index == m_audioStreamIndex) {
            QMutexLocker locker(&m_audioPacketMutex);
            
            // 队列满时等待（不阻塞视频！）
            while (m_audioPacketQueue.size() >= MAX_AUDIO_PACKET_QUEUE && m_running && !m_seeking) {
                m_audioPacketCondition.wait(&m_audioPacketMutex, 10);
            }
            
            if (m_running && !m_seeking) {
                m_audioPacketQueue.enqueue(packet);
                m_audioPacketCondition.wakeOne();
            } else {
                av_packet_free(&packet);
            }
        }
        else {
            // 其他流（字幕等），丢弃
            av_packet_free(&packet);
        }
    }
    
    // 通知解码线程结束
    m_videoPacketCondition.wakeAll();
    m_audioPacketCondition.wakeAll();
    
    qDebug() << "[Demux] 线程结束";
#endif
}

// ========================================
// 视频解码线程：独立解码，不受音频影响
// ========================================
void D3D11Renderer::videoDecodeThread()
{
#if FFMPEG_AVAILABLE && defined(_WIN32)
    if (!m_videoCodecCtx) return;
    
    qDebug() << "[视频解码] 线程启动";
    
    AVFrame *frame = av_frame_alloc();
    
    while (m_running) {
        // 从 Packet 队列取出
        AVPacket *packet = nullptr;
        {
            QMutexLocker locker(&m_videoPacketMutex);
            
            while (m_videoPacketQueue.isEmpty() && m_running) {
                m_videoPacketCondition.wait(&m_videoPacketMutex, 50);
            }
            
            if (!m_running) break;
            if (m_videoPacketQueue.isEmpty()) continue;
            
            packet = m_videoPacketQueue.dequeue();  // 取出指针，由此函数负责释放
            
            m_videoPacketCondition.wakeOne();  // 通知 Demux 线程
        }
        
        // 空 Packet = flush 信号
        if (!packet) {
            avcodec_flush_buffers(m_videoCodecCtx);
            
            // 清空帧队列
            {
                QMutexLocker locker(&m_frameMutex);
                m_frameQueue.clear();
            }
            
            // 重置视频时钟
            m_videoClockValid = false;
            m_videoStartPts = 0;
            continue;
        }
        
        // 解码
        int ret = avcodec_send_packet(m_videoCodecCtx, packet);
        av_packet_free(&packet);
        
        while (ret >= 0 && m_running) {
            ret = avcodec_receive_frame(m_videoCodecCtx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
            if (ret < 0) break;
            
            double pts = 0;
            AVStream *stream = m_formatCtx->streams[m_videoStreamIndex];
            if (frame->pts != AV_NOPTS_VALUE) {
                pts = frame->pts * av_q2d(stream->time_base);
            }
            
            VideoFrame vf;
            vf.pts = pts;
            
            // ========================================
            // 硬件解码路径：D3D11VA
            // ========================================
            if (m_hwDeviceCtx && frame->format == AV_PIX_FMT_D3D11) {
                ID3D11Texture2D *texture = reinterpret_cast<ID3D11Texture2D*>(frame->data[0]);
                int textureIndex = reinterpret_cast<intptr_t>(frame->data[1]);
                
                // 复制纹理（因为解码器会复用）
                D3D11_TEXTURE2D_DESC desc;
                texture->GetDesc(&desc);
                
                desc.Usage = D3D11_USAGE_DEFAULT;
                desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                desc.MiscFlags = 0;
                desc.ArraySize = 1;
                
                ComPtr<ID3D11Texture2D> copyTexture;
                
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
                }
            }
            // ========================================
            // 软件解码路径：CPU → BGRA → D3D11 Texture
            // ========================================
            else {
                AVPixelFormat srcFmt = static_cast<AVPixelFormat>(frame->format);
                if (!m_swsCtx) {
                    m_swsCtx = sws_getContext(
                        m_videoWidth, m_videoHeight, srcFmt,
                        m_videoWidth, m_videoHeight, AV_PIX_FMT_BGRA,
                        SWS_FAST_BILINEAR, nullptr, nullptr, nullptr
                    );
                    qDebug() << "软件解码: 创建颜色转换，格式:" << av_get_pix_fmt_name(srcFmt) << "→ BGRA";
                }
                
                if (m_swsCtx) {
                    int bgraLinesize = m_videoWidth * 4;
                    std::vector<uint8_t> bgraBuffer(bgraLinesize * m_videoHeight);
                    uint8_t *bgraData[1] = {bgraBuffer.data()};
                    int bgraLinesizes[1] = {bgraLinesize};
                    
                    sws_scale(m_swsCtx, frame->data, frame->linesize, 0, m_videoHeight,
                             bgraData, bgraLinesizes);
                    
                    D3D11_TEXTURE2D_DESC desc = {};
                    desc.Width = m_videoWidth;
                    desc.Height = m_videoHeight;
                    desc.MipLevels = 1;
                    desc.ArraySize = 1;
                    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
                    desc.SampleDesc.Count = 1;
                    desc.Usage = D3D11_USAGE_DEFAULT;
                    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                    
                    D3D11_SUBRESOURCE_DATA initData = {};
                    initData.pSysMem = bgraBuffer.data();
                    initData.SysMemPitch = bgraLinesize;
                    
                    ComPtr<ID3D11Texture2D> softTexture;
                    {
                        QMutexLocker d3dLock(&m_d3dMutex);
                        m_device->CreateTexture2D(&desc, &initData, &softTexture);
                    }
                    
                    if (softTexture) {
                        vf.texture = softTexture;
                        vf.textureIndex = 0;
                        vf.isBGRA = true;
                    }
                }
            }
            
            // 加入帧队列
            if (vf.texture) {
                QMutexLocker locker(&m_frameMutex);
                
                // 等待队列有空间
                while (m_frameQueue.size() >= MAX_FRAME_QUEUE && m_running) {
                    m_frameCondition.wait(&m_frameMutex, 10);
                }
                
                if (m_running) {
                    m_frameQueue.enqueue(vf);
                }
            }
        }
    }
    
    av_frame_free(&frame);
    qDebug() << "[视频解码] 线程结束";
#endif
}

// ========================================
// 音频解码线程：独立解码，不受视频影响
// ========================================
void D3D11Renderer::audioDecodeThread()
{
#if FFMPEG_AVAILABLE && defined(_WIN32)
    if (!m_audioCodecCtx || !m_swrCtx) return;
    
    qDebug() << "[音频解码] 线程启动";
    
    AVFrame *frame = av_frame_alloc();
    
    while (m_running) {
        // 从 Packet 队列取出
        AVPacket *packet = nullptr;
        {
            QMutexLocker locker(&m_audioPacketMutex);
            
            while (m_audioPacketQueue.isEmpty() && m_running) {
                m_audioPacketCondition.wait(&m_audioPacketMutex, 50);
            }
            
            if (!m_running) break;
            if (m_audioPacketQueue.isEmpty()) continue;
            
            packet = m_audioPacketQueue.dequeue();  // 取出指针，由此函数负责释放
            
            m_audioPacketCondition.wakeOne();
        }
        
        // 空 Packet = flush 信号
        if (!packet) {
            avcodec_flush_buffers(m_audioCodecCtx);
            
            // 清空音频队列
            {
                QMutexLocker locker(&m_audioMutex);
                m_audioQueue.clear();
            }
            
            // 重置音频时钟
            m_audioClockValid = false;
            m_audioStartPts = 0;
            m_audioClock = 0;
            m_audioWrittenBytes = 0;
            continue;
        }
        
        // 解码
        int ret = avcodec_send_packet(m_audioCodecCtx, packet);
        av_packet_free(&packet);
        
        while (ret >= 0 && m_running) {
            ret = avcodec_receive_frame(m_audioCodecCtx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
            if (ret < 0) {
                qDebug() << "[音频解码] 错误:" << ret;
                break;
            }
            
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
                ad.volumeAdjusted = false;
                
                QMutexLocker locker(&m_audioMutex);
                
                // 等待队列有空间
                while (m_audioQueue.size() >= 100 && m_running) {
                    // 音频队列满时，短暂等待
                    locker.unlock();
                    QThread::msleep(5);
                    locker.relock();
                }
                
                if (m_running) {
                    m_audioQueue.enqueue(ad);
                }
            }
        }
    }
    
    av_frame_free(&frame);
    qDebug() << "[音频解码] 线程结束";
#endif
}

void D3D11Renderer::onRenderTimer()
{
#ifdef _WIN32
    if (!m_d3dInitialized || !m_playing || m_paused) return;
    
    // 获取当前时间（秒）
    double currentTime = QDateTime::currentMSecsSinceEpoch() / 1000.0;
    
    // 如果还没到显示时间，跳过（实现延迟渲染）
    if (m_frameTimer > 0 && currentTime < m_frameTimer) {
        return;
    }
    
    VideoFrame frame;
    bool hasFrame = false;
    double framePts = 0;
    
    {
        QMutexLocker locker(&m_frameMutex);
        
        if (m_frameQueue.isEmpty()) return;
        
        framePts = m_frameQueue.head().pts;
        
        // 记录视频首帧 PTS
        if (!m_videoClockValid) {
            m_videoStartPts = framePts;
            m_videoClockValid = true;
            m_frameTimer = currentTime;  // 初始化 frame timer
            m_lastFramePts = framePts;
            qDebug() << "[视频] 首帧 PTS:" << m_videoStartPts;
            
            if (m_audioClockValid) {
                m_avSyncOffset = m_videoStartPts - m_audioStartPts;
                qDebug() << "[同步] 音视频偏移:" << m_avSyncOffset << "秒";
            }
        }
        
        // 获取音频主时钟
        double refClock = m_audioClock + m_avSyncOffset;
        double diff = framePts - refClock;  // diff > 0: 视频快, diff < 0: 视频慢
        
        // 同步阈值
        const double MIN_SYNC_THRESHOLD = 0.01;   // 10ms
        const double MAX_SYNC_THRESHOLD = 0.1;    // 100ms
        const double NOSYNC_THRESHOLD = 10.0;     // 10秒：超过这个值不同步
        const double FRAMEDUP_THRESHOLD = 0.1;    // 100ms：帧显示时间超过这个值认为是"长帧"
        
        // 计算本帧的基础 delay（与上一帧的时间差）
        double delay = framePts - m_lastFramePts;
        if (delay <= 0 || delay > 1.0) {
            delay = m_lastDelay;  // 异常情况，使用上次的 delay
        }
        
        // 动态同步阈值（根据帧率自适应）
        double syncThreshold = qMax(MIN_SYNC_THRESHOLD, qMin(MAX_SYNC_THRESHOLD, delay));
        
        // ========================================
        // 动态 delay 同步策略
        // ========================================
        if (m_audioClockValid && qAbs(diff) < NOSYNC_THRESHOLD) {
            if (diff <= -syncThreshold) {
                // 【视频落后于音频】：加快，减小 delay
                delay = qMax(0.0, delay + diff);
                m_consecutiveFastRender++;
                
                // 如果连续 10 次快速渲染且落后超过 1 秒，需要丢帧
                if (m_consecutiveFastRender >= 10 && diff < -1.0) {
                    int dropped = 0;
                    while (m_frameQueue.size() > 1 && dropped < 5) {
                        double nextPts = m_frameQueue.at(1).pts;
                        if (nextPts < refClock) {
                            m_frameQueue.dequeue();
                            m_frameCondition.wakeOne();
                            dropped++;
                            framePts = m_frameQueue.head().pts;
                        } else {
                            break;
                        }
                    }
                    if (dropped > 0) {
                        qDebug() << "[AVSync] 视频落后严重，丢帧追赶 dropped=" << dropped
                                 << "diff(ms)=" << diff * 1000;
                    }
                    m_consecutiveFastRender = 0;
                }
            }
            else if (diff >= syncThreshold) {
                // 【视频快于音频】：减慢，增大 delay
                m_consecutiveFastRender = 0;
                
                if (m_lastDelay > FRAMEDUP_THRESHOLD) {
                    // 上一帧显示时间很长，直接一步到位
                    delay = delay + diff;
                } else {
                    // 慢慢调整，避免画面突然卡顿
                    delay = 2 * delay;
                    // 但不要超过差值，避免过度等待
                    delay = qMin(delay, delay + diff);
                }
            }
            else {
                // 在阈值范围内，正常播放
                m_consecutiveFastRender = 0;
            }
        } else {
            m_consecutiveFastRender = 0;
        }
        
        // 限制 delay 范围
        const double MIN_DELAY = 0.001;   // 最小 1ms
        const double MAX_DELAY = 0.5;     // 最大 500ms
        delay = qBound(MIN_DELAY, delay, MAX_DELAY);
        
        // 记录本帧信息
        m_lastFramePts = framePts;
        m_lastDelay = delay;
        
        // 取出帧
        frame = m_frameQueue.dequeue();
        m_frameCondition.wakeOne();
        hasFrame = true;
        
        // 计算下一帧的显示时间
        m_frameTimer = currentTime + delay;
        
        // 日志（每 2 秒）
        static int syncLogCounter = 0;
        if (++syncLogCounter >= 125) {  // ~16ms * 125 = 2s
            syncLogCounter = 0;
            qDebug() << "[AVSync] diff(ms)=" << QString::number(diff * 1000, 'f', 1)
                     << "delay(ms)=" << QString::number(delay * 1000, 'f', 1)
                     << "audio=" << QString::number(refClock, 'f', 2)
                     << "video=" << QString::number(framePts, 'f', 2)
                     << "vq=" << m_frameQueue.size();
        }
    }
    
    // 渲染
    if (hasFrame && frame.texture) {
        if (frame.isBGRA) {
            renderBGRAFrame(frame.texture.Get());
        } else {
            renderNV12Frame(frame.texture.Get(), frame.textureIndex);
        }
        m_currentPts = frame.pts;
        emit positionChanged(m_currentPts);
    }
#endif
}

void D3D11Renderer::renderBGRAFrame(ID3D11Texture2D *texture)
{
#ifdef _WIN32
    if (!m_device || !m_context || !m_swapChain) return;
    
    QMutexLocker d3dLock(&m_d3dMutex);
    
    // 创建 SRV（BGRA 只需要一个）
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    
    ComPtr<ID3D11ShaderResourceView> srv;
    m_device->CreateShaderResourceView(texture, &srvDesc, &srv);
    
    // 设置渲染状态
    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(width());
    viewport.Height = static_cast<float>(height());
    viewport.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &viewport);
    
    m_context->OMSetRenderTargets(1, m_renderTarget.GetAddressOf(), nullptr);
    
    float clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    m_context->ClearRenderTargetView(m_renderTarget.Get(), clearColor);
    
    // 使用 BGRA 着色器（简单直接采样）
    m_context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
    m_context->PSSetShader(m_pixelShaderBGRA.Get(), nullptr, 0);
    m_context->IASetInputLayout(m_inputLayout.Get());
    
    UINT stride = sizeof(Vertex);
    UINT offset = 0;
    m_context->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &stride, &offset);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    
    m_context->PSSetShaderResources(0, 1, srv.GetAddressOf());
    m_context->PSSetSamplers(0, 1, m_sampler.GetAddressOf());
    
    m_context->Draw(4, 0);
    m_swapChain->Present(1, 0);
#else
    Q_UNUSED(texture)
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
    
#if SDL3_AVAILABLE
    // 初始化 SDL3 音频
    if (!SDL_Init(SDL_INIT_AUDIO)) {
        qWarning() << "SDL3 音频初始化失败:" << SDL_GetError();
        return;
    }
    
    // SDL3 使用 AudioStream API
    SDL_AudioSpec spec;
    spec.freq = 44100;
    spec.format = SDL_AUDIO_S16;
    spec.channels = 2;
    
    m_sdlAudioStream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
        &spec,
        nullptr,  // SDL3 不用回调，用流式 API
        nullptr
    );
    
    if (!m_sdlAudioStream) {
        qWarning() << "SDL3 打开音频设备失败:" << SDL_GetError();
        return;
    }
    
    // 开始播放
    SDL_ResumeAudioStreamDevice(m_sdlAudioStream);
    m_audioWrittenBytes = 0;
    qDebug() << "SDL3 音频初始化成功";
    
#else
    // Qt 音频备用方案
    QAudioFormat format;
    format.setSampleRate(44100);
    format.setChannelCount(2);
    format.setSampleFormat(QAudioFormat::Int16);
    
    m_audioSink = std::make_unique<QAudioSink>(format);
    m_audioSink->setBufferSize(44100 * 2 * 2 / 5);
    m_audioSink->setVolume(m_volume / 100.0f);
    m_audioDevice = m_audioSink->start();
#endif
}

void D3D11Renderer::cleanupAudio()
{
#if SDL3_AVAILABLE
    if (m_sdlAudioStream) {
        SDL_DestroyAudioStream(m_sdlAudioStream);
        m_sdlAudioStream = nullptr;
    }
    m_audioWrittenBytes = 0;
#else
    if (m_audioSink) {
        m_audioSink->stop();
        m_audioSink.reset();
    }
    m_audioDevice = nullptr;
#endif
}

void D3D11Renderer::processAudio()
{
    if (!m_playing || m_paused) return;
    
#if SDL3_AVAILABLE
    if (!m_sdlAudioStream) return;
    
    QMutexLocker locker(&m_audioMutex);
    
    // 获取 SDL 音频流中排队的数据量
    int queued = SDL_GetAudioStreamQueued(m_sdlAudioStream);
    
    // 日志：每 2 秒输出一次
    static int audioLogCounter = 0;
    if (++audioLogCounter >= 400) {  // 5ms * 400 = 2秒
        audioLogCounter = 0;
        qDebug() << "[状态] 音频队列:" << m_audioQueue.size() 
                 << "SDL:" << queued / 1000 << "KB"
                 << "时钟:" << QString::number(m_audioClock, 'f', 2);
    }
    
    // 如果队列太满（超过 200ms 的数据），等待
    const int maxQueued = 44100 * 2 * 2 / 5;  // 200ms
    if (queued > maxQueued) {
        // 不写入更多数据，让 SDL 先消费
    } else {
        // 向 SDL 写入音频数据
        while (!m_audioQueue.isEmpty() && queued < maxQueued) {
            AudioData &ad = m_audioQueue.head();
            
            // 记录第一帧音频 PTS
            if (!m_audioClockValid) {
                m_audioStartPts = ad.pts;
                m_audioClockValid = true;
                qDebug() << "[音频] 首帧 PTS:" << m_audioStartPts;
                
                // 如果视频已经开始，计算偏移
                if (m_videoClockValid) {
                    m_avSyncOffset = m_videoStartPts - m_audioStartPts;
                    qDebug() << "[同步] 音视频偏移:" << m_avSyncOffset << "秒";
                }
            }
            
            // 音量调整（只处理一次，避免重复缩放失真）
            if (m_volume < 100 && !ad.volumeAdjusted) {
                int16_t *samples = reinterpret_cast<int16_t*>(ad.data.data());
                int count = ad.data.size() / 2;
                float volumeScale = m_volume / 100.0f;
                for (int i = 0; i < count; i++) {
                    samples[i] = static_cast<int16_t>(samples[i] * volumeScale);
                }
                ad.volumeAdjusted = true;
            }
            
            // 写入 SDL 音频流
            if (SDL_PutAudioStreamData(m_sdlAudioStream, ad.data.constData(), ad.data.size())) {
                m_audioWrittenBytes += ad.data.size();
                m_audioQueue.dequeue();
                queued = SDL_GetAudioStreamQueued(m_sdlAudioStream);
            } else {
                qWarning() << "SDL 音频写入失败:" << SDL_GetError();
                break;
            }
        }
    }
    
    // 【关键】计算音频时钟
    // 已写入字节数 - SDL队列中的字节数 = 已播放字节数
    if (m_audioClockValid) {
        qint64 playedBytes = m_audioWrittenBytes - queued;
        if (playedBytes < 0) playedBytes = 0;
        // 44100 采样率，2 通道，2 字节/样本 = 176400 字节/秒
        double playedSeconds = static_cast<double>(playedBytes) / 176400.0;
        m_audioClock = m_audioStartPts + playedSeconds;
    }
    
    // 每 2 秒输出同步状态
    static int logCounter = 0;
    if (++logCounter >= 400) {  // 5ms * 400 = 2秒
        logCounter = 0;
        double correctedClock = m_audioClock + m_avSyncOffset;
        double diff = m_currentPts - correctedClock;
        qDebug() << "[同步] 音频:" << QString::number(correctedClock, 'f', 2)
                 << "视频:" << QString::number(m_currentPts, 'f', 2)
                 << "差:" << QString::number(diff * 1000, 'f', 0) << "ms";
    }

    // 额外的“断粮”监测日志：如果音频队列空且 SDL 缓冲也很小，定期提示
    if (m_audioQueue.isEmpty() && queued < 4096) {
        static int starvingLogCounter = 0;
        if (++starvingLogCounter >= 200) { // ~1s
            starvingLogCounter = 0;
            qDebug() << "[音频] 可能断粮: audioQueue=0, SDLqKB=" << queued / 1024
                     << "audioClock=" << m_audioClock;
        }
    }
    
#else
    // Qt 音频备用方案
    if (!m_audioDevice) return;
    
    QMutexLocker locker(&m_audioMutex);
    
    QAudio::State state = m_audioSink->state();
    if (state == QAudio::SuspendedState) {
        m_audioSink->resume();
    }
    
    while (!m_audioQueue.isEmpty()) {
        qint64 bytesFree = m_audioSink->bytesFree();
        if (bytesFree < 1024) break; // 避免反复调用 write 占满事件循环
        
        AudioData &ad = m_audioQueue.head();
        
        if (!m_audioClockValid) {
            m_audioStartPts = ad.pts;
            m_audioClockValid = true;
        }
        
        // 仅在第一次写入前调整音量，避免重复缩放
        if (m_volume < 100 && !ad.volumeAdjusted) {
            int16_t *samples = reinterpret_cast<int16_t*>(ad.data.data());
            int count = ad.data.size() / 2;
            float volumeScale = m_volume / 100.0f;
            for (int i = 0; i < count; i++) {
                samples[i] = static_cast<int16_t>(samples[i] * volumeScale);
            }
            ad.volumeAdjusted = true;
        }
        
        // 处理可能的部分写入，避免截断导致失真
        qint64 offset = 0;
        qint64 remaining = ad.data.size();
        while (remaining > 0) {
            bytesFree = m_audioSink->bytesFree();
            if (bytesFree <= 0) break;
            
            const qint64 toWrite = qMin<qint64>(bytesFree, remaining);
            qint64 written = m_audioDevice->write(ad.data.constData() + offset, toWrite);
            if (written <= 0) {
                // 写入失败或设备暂不可写，稍后重试
                break;
            }
            
            offset += written;
            remaining -= written;
            m_audioWrittenBytes += written;
        }
        
        if (remaining == 0) {
            // 完整写入，弹出队列
            m_audioQueue.dequeue();
        } else {
            // 仅写入部分，保留未写入的数据
            ad.data = ad.data.mid(offset);
            break;
        }
    }
    
    // Qt 音频时钟
    if (m_audioClockValid && m_audioSink) {
        qint64 processedUs = m_audioSink->processedUSecs();
        m_audioClock = m_audioStartPts + processedUs / 1000000.0;
    }
#endif
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




