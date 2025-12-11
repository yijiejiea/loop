#ifndef FFMPEGPLAYER_H
#define FFMPEGPLAYER_H

#include <QObject>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QQueue>
#include <QImage>
#include <QAudioSink>
#include <QAudioFormat>
#include <QIODevice>
#include <memory>
#include <atomic>

#if FFMPEG_AVAILABLE
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
#include <libavutil/hwcontext.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}
#endif

/**
 * @brief 视频帧数据
 */
struct VideoFrame {
    QImage image;
    double pts = 0;  // 显示时间戳（秒）
};

/**
 * @brief 音频帧数据
 */
struct AudioFrame {
    QByteArray data;
    double pts = 0;
};

/**
 * @brief FFmpeg 解码线程
 */
class DecodeThread : public QThread
{
    Q_OBJECT
public:
    explicit DecodeThread(QObject *parent = nullptr);
    ~DecodeThread();

    bool openFile(const QString &filename);
    void closeFile();
    
    void startDecoding();
    void stopDecoding();
    void seekTo(double seconds);
    
    double duration() const { return m_duration; }
    int videoWidth() const { return m_videoWidth; }
    int videoHeight() const { return m_videoHeight; }
    
    // 获取解码后的帧
    bool getVideoFrame(VideoFrame &frame);
    bool getAudioFrame(AudioFrame &frame);
    
    // 音频格式
    QAudioFormat audioFormat() const;

signals:
    void fileOpened();
    void decodingFinished();
    void errorOccurred(const QString &error);

protected:
    void run() override;

private:
    void decodePacket();
    void flushQueues();
    bool initHardwareDecoder(const AVCodec *codec);
    AVFrame* transferHwFrame(AVFrame *hwFrame);  // 从 GPU 转移帧到 CPU

#if FFMPEG_AVAILABLE
    AVFormatContext *m_formatCtx = nullptr;
    AVCodecContext *m_videoCodecCtx = nullptr;
    AVCodecContext *m_audioCodecCtx = nullptr;
    SwsContext *m_swsCtx = nullptr;
    SwrContext *m_swrCtx = nullptr;
    AVBufferRef *m_hwDeviceCtx = nullptr;  // 硬件设备上下文
    AVPixelFormat m_hwPixFmt = AV_PIX_FMT_NONE;  // 硬件像素格式
    
    int m_videoStreamIndex = -1;
    int m_audioStreamIndex = -1;
    bool m_useHwDecode = false;  // 是否使用硬件解码
#endif

    double m_duration = 0;
    int m_videoWidth = 0;
    int m_videoHeight = 0;
    int m_audioSampleRate = 44100;
    int m_audioChannels = 2;
    
    // 帧队列
    QQueue<VideoFrame> m_videoQueue;
    QQueue<AudioFrame> m_audioQueue;
    QMutex m_videoMutex;
    QMutex m_audioMutex;
    QWaitCondition m_videoCondition;
    QWaitCondition m_audioCondition;
    
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_seeking{false};
    double m_seekTarget = 0;
    
    static constexpr int MAX_VIDEO_QUEUE_SIZE = 30;
    static constexpr int MAX_AUDIO_QUEUE_SIZE = 100;
};

/**
 * @brief FFmpeg 视频播放器
 * 
 * 提供完整的视频播放功能，包括：
 * - 视频解码和渲染
 * - 音频解码和播放
 * - 音视频同步
 * - 循环播放
 */
class FFmpegPlayer : public QObject
{
    Q_OBJECT

public:
    enum PlaybackState {
        StoppedState,
        PlayingState,
        PausedState
    };
    Q_ENUM(PlaybackState)

    explicit FFmpegPlayer(QObject *parent = nullptr);
    ~FFmpegPlayer();

    /**
     * @brief 加载视频文件
     */
    void loadFile(const QString &filename);

    /**
     * @brief 播放控制
     */
    void play();
    void pause();
    void stop();
    void togglePause();

    /**
     * @brief 跳转到指定位置
     * @param seconds 秒数
     */
    void seek(double seconds);

    /**
     * @brief 设置音量 (0-100)
     */
    void setVolume(int volume);
    int volume() const { return m_volume; }

    /**
     * @brief 设置循环播放
     */
    void setLoop(bool loop) { m_loop = loop; }
    bool isLoop() const { return m_loop; }

    /**
     * @brief 获取播放状态
     */
    PlaybackState state() const { return m_state; }
    bool isPlaying() const { return m_state == PlayingState; }
    bool isPaused() const { return m_state == PausedState; }

    /**
     * @brief 获取时间信息
     */
    double position() const { return m_currentPosition; }
    double duration() const { return m_duration; }

    /**
     * @brief 获取视频尺寸
     */
    int videoWidth() const;
    int videoHeight() const;

signals:
    void positionChanged(double seconds);
    void durationChanged(double seconds);
    void stateChanged(PlaybackState state);
    void fileLoaded();
    void endOfFile();
    void errorOccurred(const QString &error);
    void frameReady(const QImage &frame);

private slots:
    void onFileOpened();
    void onDecodingFinished();
    void onDecodeError(const QString &error);
    void processVideo();
    void processAudio();

private:
    void setupAudio();
    void cleanupAudio();
    void setState(PlaybackState state);

    DecodeThread *m_decodeThread = nullptr;
    
    // 音频播放
    std::unique_ptr<QAudioSink> m_audioSink;
    QIODevice *m_audioDevice = nullptr;
    
    // 播放控制
    QTimer *m_videoTimer = nullptr;
    QTimer *m_audioTimer = nullptr;
    
    PlaybackState m_state = StoppedState;
    double m_currentPosition = 0;
    double m_duration = 0;
    double m_audioClock = 0;  // 音频时钟，用于同步
    int m_volume = 50;
    bool m_loop = true;
    
    QString m_currentFile;
    qint64 m_startTime = 0;  // 播放开始时间
};

#endif // FFMPEGPLAYER_H

