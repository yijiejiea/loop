/**
 * @file OpenGLRenderer.h
 * @brief OpenGL 视频渲染器（跨平台：Linux/macOS/Windows）
 * 
 * 使用 OpenGL 进行视频渲染，配合 FFmpeg 解码
 * - Linux: VAAPI/VDPAU 硬件解码
 * - macOS: VideoToolbox 硬件解码
 * - Windows: 可作为 D3D11 的备选
 */

#ifndef OPENGLRENDERER_H
#define OPENGLRENDERER_H

#include "VideoRendererBase.h"
#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QTimer>
#include <memory>
#include <atomic>

#if FFMPEG_AVAILABLE
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
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
 * @brief OpenGL 视频播放器（跨平台）
 * 
 * 继承 QOpenGLWidget 而不是 VideoRendererBase（因为 Qt 不支持多重继承 QWidget）
 * 但实现相同的接口
 * 
 * 特点：
 * - 跨平台：Linux, macOS, Windows
 * - 支持各平台硬件解码
 * - 使用 OpenGL 着色器进行 YUV→RGB 转换
 */
class OpenGLRenderer : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    // 使用与 VideoRendererBase 相同的解码模式枚举
    enum DecodeMode {
        Auto,       ///< 自动选择（优先硬件）
        Hardware,   ///< 强制硬件解码
        Software    ///< 强制软件解码
    };
    Q_ENUM(DecodeMode)

    explicit OpenGLRenderer(QWidget *parent = nullptr);
    ~OpenGLRenderer() override;

    // ========================================
    // 与 VideoRendererBase 相同的接口
    // ========================================
    bool openFile(const QString &filename);
    void closeFile();
    void play();
    void pause();
    void stop();
    void togglePause();
    void seek(double seconds);
    void setVolume(int volume);
    
    QString rendererName() const { return "OpenGL (Cross-Platform)"; }
    bool isHardwareDecoding() const { return m_hwDeviceCtx != nullptr; }
    
    void setDecodeMode(DecodeMode mode) { m_decodeMode = mode; }
    DecodeMode decodeMode() const { return m_decodeMode; }
    void setLoop(bool loop) { m_loop = loop; }
    bool isLoop() const { return m_loop; }
    int volume() const { return m_volume; }
    double duration() const { return m_duration; }
    double position() const { return m_currentPts; }
    bool isPlaying() const { return m_playing; }
    bool isPaused() const { return m_paused; }
    
    // 兼容旧接口
    void loadFile(const QString &filename) { openFile(filename); }

signals:
    void fileLoaded();
    void positionChanged(double position);
    void playbackStateChanged(bool playing);
    void endOfFile();
    void errorOccurred(const QString &error);

protected:
    // QOpenGLWidget 重写
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

private slots:
    void onRenderTimer();
    void onAudioTimer();

private:
    // FFmpeg 初始化
    bool initHardwareDecoder(const AVCodec *codec);
    
    // 解码和渲染
    void decodeThread();
    void uploadFrame(const uint8_t *yData, const uint8_t *uData, const uint8_t *vData,
                     int yLinesize, int uLinesize, int vLinesize,
                     int width, int height);
    
    // 音频
    void setupAudio();
    void cleanupAudio();
    void processAudio();

private:
#if FFMPEG_AVAILABLE
    AVFormatContext *m_formatCtx = nullptr;
    AVCodecContext *m_videoCodecCtx = nullptr;
    AVCodecContext *m_audioCodecCtx = nullptr;
    AVBufferRef *m_hwDeviceCtx = nullptr;
    SwrContext *m_swrCtx = nullptr;
    SwsContext *m_swsCtx = nullptr;
    
    int m_videoStreamIndex = -1;
    int m_audioStreamIndex = -1;
#endif

    // OpenGL 对象
    std::unique_ptr<QOpenGLShaderProgram> m_shader;
    GLuint m_textureY = 0;
    GLuint m_textureU = 0;
    GLuint m_textureV = 0;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    bool m_glInitialized = false;

    // 解码线程
    std::unique_ptr<QThread> m_decodeThread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_seeking{false};
    double m_seekTarget = 0;
    
    // 音频
    struct AudioData {
        QByteArray data;
        double pts = 0;
    };
    QQueue<AudioData> m_audioQueue;
    QMutex m_audioMutex;
    std::unique_ptr<QAudioSink> m_audioSink;
    QIODevice *m_audioDevice = nullptr;
    
    // 帧数据
    struct FrameData {
        std::vector<uint8_t> yPlane;
        std::vector<uint8_t> uPlane;
        std::vector<uint8_t> vPlane;
        int width = 0;
        int height = 0;
        int yLinesize = 0;
        int uLinesize = 0;
        int vLinesize = 0;
        double pts = 0;
    };
    QQueue<FrameData> m_frameQueue;
    QMutex m_frameMutex;
    QWaitCondition m_frameCondition;
    FrameData m_currentFrame;
    bool m_hasNewFrame = false;
    static constexpr int MAX_FRAME_QUEUE = 3;
    
    // 播放状态
    DecodeMode m_decodeMode = Auto;
    bool m_loop = true;
    bool m_playing = false;
    bool m_paused = false;
    int m_volume = 50;
    double m_duration = 0;
    double m_currentPts = 0;
    double m_audioClock = 0;
    int m_videoWidth = 0;
    int m_videoHeight = 0;
    QString m_currentFile;
    
    // 定时器
    QTimer *m_renderTimer = nullptr;
    QTimer *m_audioTimer = nullptr;
};

#endif // OPENGLRENDERER_H

