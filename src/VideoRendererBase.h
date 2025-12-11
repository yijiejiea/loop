/**
 * @file VideoRendererBase.h
 * @brief 跨平台视频渲染器抽象基类
 * 
 * 各平台实现：
 * - Windows: D3D11Renderer (D3D11VA 硬件解码)
 * - macOS:   MetalRenderer (VideoToolbox 硬件解码)
 * - Linux:   OpenGLRenderer (VAAPI/VDPAU 硬件解码)
 */

#ifndef VIDEORENDERERBASE_H
#define VIDEORENDERERBASE_H

#include <QWidget>
#include <QString>

/**
 * @brief 视频渲染器抽象基类
 * 
 * 定义所有平台通用的视频播放接口
 */
class VideoRendererBase : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief 解码模式
     */
    enum DecodeMode {
        Auto,       ///< 自动选择（优先硬件）
        Hardware,   ///< 强制硬件解码
        Software    ///< 强制软件解码
    };
    Q_ENUM(DecodeMode)

    explicit VideoRendererBase(QWidget *parent = nullptr) : QWidget(parent) {}
    virtual ~VideoRendererBase() = default;

    // ========================================
    // 纯虚函数 - 各平台必须实现
    // ========================================
    
    /**
     * @brief 打开视频文件
     * @param filename 文件路径
     * @return 成功返回 true
     */
    virtual bool openFile(const QString &filename) = 0;
    
    /**
     * @brief 关闭当前文件
     */
    virtual void closeFile() = 0;
    
    /**
     * @brief 开始播放
     */
    virtual void play() = 0;
    
    /**
     * @brief 暂停播放
     */
    virtual void pause() = 0;
    
    /**
     * @brief 停止播放
     */
    virtual void stop() = 0;
    
    /**
     * @brief 切换播放/暂停
     */
    virtual void togglePause() = 0;
    
    /**
     * @brief 跳转到指定位置
     * @param seconds 秒数
     */
    virtual void seek(double seconds) = 0;
    
    /**
     * @brief 设置音量
     * @param volume 音量 (0-100)
     */
    virtual void setVolume(int volume) = 0;
    
    // ========================================
    // 虚函数 - 有默认实现，可覆盖
    // ========================================
    
    /**
     * @brief 设置解码模式
     */
    virtual void setDecodeMode(DecodeMode mode) { m_decodeMode = mode; }
    
    /**
     * @brief 获取解码模式
     */
    virtual DecodeMode decodeMode() const { return m_decodeMode; }
    
    /**
     * @brief 设置循环播放
     */
    virtual void setLoop(bool loop) { m_loop = loop; }
    
    /**
     * @brief 是否循环播放
     */
    virtual bool isLoop() const { return m_loop; }
    
    /**
     * @brief 获取当前音量
     */
    virtual int volume() const { return m_volume; }
    
    /**
     * @brief 获取视频时长
     */
    virtual double duration() const { return m_duration; }
    
    /**
     * @brief 获取当前播放位置
     */
    virtual double position() const { return m_currentPts; }
    
    /**
     * @brief 是否正在播放
     */
    virtual bool isPlaying() const { return m_playing; }
    
    /**
     * @brief 是否暂停
     */
    virtual bool isPaused() const { return m_paused; }
    
    /**
     * @brief 是否使用硬件解码
     */
    virtual bool isHardwareDecoding() const { return false; }
    
    /**
     * @brief 获取渲染器名称（用于调试）
     */
    virtual QString rendererName() const = 0;

signals:
    /**
     * @brief 文件加载完成
     */
    void fileLoaded();
    
    /**
     * @brief 播放位置改变
     * @param position 当前位置（秒）
     */
    void positionChanged(double position);
    
    /**
     * @brief 播放状态改变
     * @param playing 是否正在播放
     */
    void playbackStateChanged(bool playing);
    
    /**
     * @brief 播放结束
     */
    void endOfFile();
    
    /**
     * @brief 发生错误
     * @param error 错误信息
     */
    void errorOccurred(const QString &error);

protected:
    // 通用状态
    DecodeMode m_decodeMode = Auto;
    bool m_loop = true;
    bool m_playing = false;
    bool m_paused = false;
    int m_volume = 100;
    double m_duration = 0;
    double m_currentPts = 0;
    QString m_currentFile;
};

// ========================================
// 工厂函数 - 创建当前平台的渲染器
// ========================================

/**
 * @brief 创建当前平台的视频渲染器
 * @param parent 父 widget
 * @return 渲染器实例
 * 
 * Windows → D3D11Renderer
 * macOS   → MetalRenderer (或 OpenGLRenderer)
 * Linux   → OpenGLRenderer
 */
VideoRendererBase* createVideoRenderer(QWidget *parent = nullptr);

/**
 * @brief 获取当前平台支持的渲染器列表
 * @return 渲染器名称列表
 */
QStringList availableRenderers();

#endif // VIDEORENDERERBASE_H

