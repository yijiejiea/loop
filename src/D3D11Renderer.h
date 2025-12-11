#ifndef D3D11RENDERER_H
#define D3D11RENDERER_H

#include <QWidget>
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
#include <libswresample/swresample.h>
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
 * @brief D3D11 硬件加速视频播放器
 * 
 * 特点：
 * - 使用 D3D11VA 硬件解码
 * - 解码输出直接是 D3D11 Texture
 * - 使用 D3D11 渲染，全程 GPU 操作
 * - 零 CPU 拷贝，最高性能
 */
class D3D11Renderer : public QWidget
{
    Q_OBJECT

public:
    explicit D3D11Renderer(QWidget *parent = nullptr);
    ~D3D11Renderer();

    // 播放控制
    void loadFile(const QString &filename);
    void play();
    void pause();
    void stop();
    void togglePause();
    void seek(double seconds);
    
    // 属性
    void setVolume(int volume);
    int volume() const { return m_volume; }
    void setLoop(bool loop) { m_loop = loop; }
    bool isLoop() const { return m_loop; }
    
    // 状态
    bool isPlaying() const { return m_playing && !m_paused; }
    bool isPaused() const { return m_paused; }
    double position() const { return m_currentPts; }
    double duration() const { return m_duration; }

signals:
    void positionChanged(double seconds);
    void durationChanged(double seconds);
    void playbackStateChanged(bool playing);
    void fileLoaded();
    void endOfFile();
    void errorOccurred(const QString &error);

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
    bool openFile(const QString &filename);
    void closeFile();
    bool initHardwareDecoder(const AVCodec *codec);
    
    // 解码和渲染
    void decodeThread();
    void renderFrame(ID3D11Texture2D *texture, int textureIndex);
    void renderNV12Frame(ID3D11Texture2D *texture, int textureIndex);
    
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
    ComPtr<ID3D11PixelShader> m_pixelShader;
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
    
    // 播放状态
    bool m_playing = false;
    bool m_paused = false;
    bool m_loop = true;
    int m_volume = 50;
    double m_duration = 0;
    double m_currentPts = 0;
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
    };
    QQueue<VideoFrame> m_frameQueue;
    QMutex m_frameMutex;
    QMutex m_d3dMutex;  // D3D11 上下文访问保护
    QWaitCondition m_frameCondition;
    static constexpr int MAX_FRAME_QUEUE = 3;  // 小队列，减少延迟
    
    QString m_currentFile;
    bool m_d3dInitialized = false;
};

#endif // D3D11RENDERER_H

