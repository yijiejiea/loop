/**
 * @file VideoRendererFactory.cpp
 * @brief 视频渲染器工厂函数实现
 * 
 * 根据平台创建对应的视频渲染器
 */

#include "VideoRendererBase.h"

#ifdef _WIN32
#include "D3D11Renderer.h"
#endif

// OpenGL 渲染器可在所有平台使用
#include "OpenGLRenderer.h"

VideoRendererBase* createVideoRenderer(QWidget *parent)
{
#ifdef _WIN32
    // Windows: 优先使用 D3D11
    return new D3D11Renderer(parent);
#else
    // macOS/Linux: 使用 OpenGL
    // 注意：OpenGLRenderer 不继承 VideoRendererBase
    // 这里需要一个适配器，或者直接返回 OpenGLRenderer*
    // 由于 OpenGLRenderer 实现了相同的接口，调用方需要自行处理
    return nullptr;  // 需要修改调用方式
#endif
}

QStringList availableRenderers()
{
    QStringList list;
    
#ifdef _WIN32
    list << "D3D11 (Windows)";
#endif

#ifdef __APPLE__
    list << "Metal (macOS)";
#endif

    // OpenGL 在所有平台可用
    list << "OpenGL (Cross-Platform)";
    
    return list;
}

