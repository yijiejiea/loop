cmake_minimum_required(VERSION 3.20)
project(LoopVideoPlayer VERSION 2.0.0 LANGUAGES CXX)

# 设置 C++23 标准
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Qt 自动处理
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ============================================
# FFmpeg 配置
# ============================================
# 设置 FFmpeg 路径（用户需要根据实际路径修改）
# 下载地址: https://github.com/BtbN/FFmpeg-Builds/releases
# 推荐下载: ffmpeg-master-latest-win64-gpl-shared.zip
set(FFMPEG_SDK_PATH "${CMAKE_SOURCE_DIR}/third_party/ffmpeg" CACHE PATH "Path to FFmpeg SDK")

# 检查 FFmpeg 是否存在
if(EXISTS "${FFMPEG_SDK_PATH}/include/libavcodec/avcodec.h")
    message(STATUS "Found FFmpeg at: ${FFMPEG_SDK_PATH}")
    set(FFMPEG_FOUND TRUE)
else()
    message(WARNING "FFmpeg not found at ${FFMPEG_SDK_PATH}")
    message(WARNING "Please download FFmpeg from:")
    message(WARNING "  https://github.com/BtbN/FFmpeg-Builds/releases")
    message(WARNING "Download: ffmpeg-master-latest-win64-gpl-shared.zip")
    message(WARNING "And extract to: ${FFMPEG_SDK_PATH}")
    set(FFMPEG_FOUND FALSE)
endif()

# ============================================
# Qt6 配置
# ============================================
find_package(Qt6 REQUIRED COMPONENTS 
    Core 
    Gui 
    Widgets
    Multimedia
    OpenGLWidgets
)

# ============================================
# 源文件（跨平台架构）
# ============================================
set(SOURCES
    src/main.cpp
    src/FloatingVideoPlayer.cpp
    src/FloatingVideoPlayer.h
    src/VideoRendererBase.h
    src/VideoRendererFactory.cpp
    # 旧版本兼容
    src/FFmpegPlayer.cpp
    src/FFmpegPlayer.h
    src/VideoWidget.cpp
    src/VideoWidget.h
)

# Windows 平台：D3D11 渲染器
if(WIN32)
    list(APPEND SOURCES
        src/D3D11Renderer.cpp
        src/D3D11Renderer.h
    )
endif()

# 所有平台：OpenGL 渲染器（可选）
# 如需启用，取消下面注释并添加 Qt OpenGL 依赖
# list(APPEND SOURCES
#     src/OpenGLRenderer.cpp
#     src/OpenGLRenderer.h
# )

# 创建可执行文件 (WIN32 避免控制台窗口)
if(WIN32)
    add_executable(${PROJECT_NAME} WIN32 ${SOURCES})
else()
    add_executable(${PROJECT_NAME} ${SOURCES})
endif()

# 链接 Qt 库
target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::OpenGLWidgets
)

# 包含目录
target_include_directories(${PROJECT_NAME} PRIVATE 
    ${CMAKE_SOURCE_DIR}/src
)

# ============================================
# FFmpeg 链接
# ============================================
if(FFMPEG_FOUND)
    target_include_directories(${PROJECT_NAME} PRIVATE "${FFMPEG_SDK_PATH}/include")
    
    if(WIN32)
        # Windows: 链接 FFmpeg 库
        target_link_libraries(${PROJECT_NAME} PRIVATE
            "${FFMPEG_SDK_PATH}/lib/avcodec.lib"
            "${FFMPEG_SDK_PATH}/lib/avformat.lib"
            "${FFMPEG_SDK_PATH}/lib/avutil.lib"
            "${FFMPEG_SDK_PATH}/lib/swscale.lib"
            "${FFMPEG_SDK_PATH}/lib/swresample.lib"
        )
        
        # 复制 FFmpeg DLL 到输出目录
        file(GLOB FFMPEG_DLLS "${FFMPEG_SDK_PATH}/bin/*.dll")
        foreach(DLL ${FFMPEG_DLLS})
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${DLL}"
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
                COMMENT "Copying ${DLL}..."
            )
        endforeach()
    else()
        # Linux/macOS: 使用 pkg-config
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(FFMPEG REQUIRED 
            libavcodec 
            libavformat 
            libavutil 
            libswscale 
            libswresample
        )
        target_include_directories(${PROJECT_NAME} PRIVATE ${FFMPEG_INCLUDE_DIRS})
        target_link_libraries(${PROJECT_NAME} PRIVATE ${FFMPEG_LIBRARIES})
    endif()
    
    target_compile_definitions(${PROJECT_NAME} PRIVATE FFMPEG_AVAILABLE=1)
else()
    target_compile_definitions(${PROJECT_NAME} PRIVATE FFMPEG_AVAILABLE=0)
endif()

# Windows 特定设置
if(WIN32)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
    
    # 链接 D3D11 库
    target_link_libraries(${PROJECT_NAME} PRIVATE
        d3d11
        dxgi
        d3dcompiler
    )
    
    # 部署 Qt 依赖
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${Qt6_DIR}/../../../bin")
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                --no-translations
                --no-system-d3d-compiler
                --no-opengl-sw
                "$<TARGET_FILE:${PROJECT_NAME}>"
            COMMENT "Running windeployqt..."
        )
    endif()
endif()

# 安装规则
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
