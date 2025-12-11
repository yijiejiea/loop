#ifndef D3D11RENDERER_H
#define D3D11RENDERER_H

#include "VideoRendererBase.h"
#include <QTimer>
#include <memory>
#include <atomic>

#ifdef _WIN32
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#endif

#if FFMPEG_AVAILABLE
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
#endif

#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QQueue>
#include <QAudioSink>
#include <QIODevice>

/**
 * @brief 音频帧数据
 */
struct AudioData {
    QByteArray data;
    double pts = 0;
};

/**
 * @brief D3D11 视频播放器 (Windows 平台)
 * 
 * 继承自 VideoRendererBase，实现 Windows 平台的视频渲染
 * 
 * 特点：
 * - 支持 D3D11VA 硬件解码（默认）
 * - 支持 FFmpeg 软件解码（可选）
 * - 使用 D3D11 渲染
 * - 软硬解码可运行时切换
 */
class D3D11Renderer : public VideoRendererBase
{
    Q_OBJECT

public:
    explicit D3D11Renderer(QWidget *parent = nullptr);
    ~D3D11Renderer() override;

    // ========================================
    // 实现 VideoRendererBase 接口
    // ========================================
    bool openFile(const QString &filename) override;
    void closeFile() override;
    void play() override;
    void pause() override;
    void stop() override;
    void togglePause() override;
    void seek(double seconds) override;
    void setVolume(int volume) override;
    
    QString rendererName() const override { return "D3D11 (Windows)"; }
    bool isHardwareDecoding() const override { return m_hwDeviceCtx != nullptr; }
    
    // 使用基类的 DecodeMode
    using VideoRendererBase::DecodeMode;
    using VideoRendererBase::setDecodeMode;
    using VideoRendererBase::decodeMode;
    
    // 兼容旧接口（停止当前播放，打开新文件并自动播放）
    void loadFile(const QString &filename);

signals:
    // 额外信号（基类已有基本信号）
    void durationChanged(double seconds);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    QPaintEngine* paintEngine() const override { return nullptr; } // 禁用 Qt 绘制

private slots:
    void onRenderTimer();
    void onAudioTimer();

private:
    // D3D11 初始化
    bool initD3D11();
    void cleanupD3D11();
    bool createSwapChain();
    bool createShaders();
    bool createSamplerState();
    void resizeSwapChain();
    
    // FFmpeg 初始化
    bool initHardwareDecoder(const AVCodec *codec);
    
    // 解码和渲染
    void decodeThread();
    void renderFrame(ID3D11Texture2D *texture, int textureIndex);
    void renderNV12Frame(ID3D11Texture2D *texture, int textureIndex);
    void renderBGRAFrame(ID3D11Texture2D *texture);
    
    // 音频
    void setupAudio();
    void cleanupAudio();
    void processAudio();

private:
#ifdef _WIN32
    // D3D11 对象
    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<IDXGISwapChain1> m_swapChain;
    ComPtr<ID3D11RenderTargetView> m_renderTarget;
    ComPtr<ID3D11VertexShader> m_vertexShader;
    ComPtr<ID3D11PixelShader> m_pixelShader;      // NV12 → RGB
    ComPtr<ID3D11PixelShader> m_pixelShaderBGRA;  // BGRA 直接采样
    ComPtr<ID3D11InputLayout> m_inputLayout;
    ComPtr<ID3D11Buffer> m_vertexBuffer;
    ComPtr<ID3D11SamplerState> m_sampler;
    ComPtr<ID3D11ShaderResourceView> m_textureSRV_Y;
    ComPtr<ID3D11ShaderResourceView> m_textureSRV_UV;
#endif

#if FFMPEG_AVAILABLE
    // FFmpeg 对象
    AVFormatContext *m_formatCtx = nullptr;
    AVCodecContext *m_videoCodecCtx = nullptr;
    AVCodecContext *m_audioCodecCtx = nullptr;
    AVBufferRef *m_hwDeviceCtx = nullptr;
    SwrContext *m_swrCtx = nullptr;
    SwsContext *m_swsCtx = nullptr;  // 软解码时的颜色转换
    
    int m_videoStreamIndex = -1;
    int m_audioStreamIndex = -1;
#endif

    // 解码线程
    std::unique_ptr<QThread> m_decodeThread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_seeking{false};
    double m_seekTarget = 0;
    
    // 音频队列
    QQueue<AudioData> m_audioQueue;
    QMutex m_audioMutex;
    std::unique_ptr<QAudioSink> m_audioSink;
    QIODevice *m_audioDevice = nullptr;
    
    // 播放状态 (基类已有: m_playing, m_paused, m_loop, m_volume, m_duration, m_currentPts)
    double m_audioClock = 0;
    
    // 视频信息
    int m_videoWidth = 0;
    int m_videoHeight = 0;
    
    // 定时器
    QTimer *m_renderTimer = nullptr;
    QTimer *m_audioTimer = nullptr;
    
    // 帧队列
    struct VideoFrame {
#ifdef _WIN32
        ComPtr<ID3D11Texture2D> texture;
#endif
        int textureIndex = 0;
        double pts = 0;
        bool isBGRA = false;  // true = 软解码(BGRA), false = 硬解码(NV12)
    };
    QQueue<VideoFrame> m_frameQueue;
    QMutex m_frameMutex;
    QMutex m_d3dMutex;  // D3D11 上下文访问保护
    QWaitCondition m_frameCondition;
    static constexpr int MAX_FRAME_QUEUE = 3;  // 小队列，减少延迟
    
    // m_currentFile 在基类中
    bool m_d3dInitialized = false;
};

#endif // D3D11RENDERER_H

