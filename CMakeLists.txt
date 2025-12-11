cmake_minimum_required(VERSION 3.20)
project(LoopVideoPlayer VERSION 1.0.0 LANGUAGES CXX)

# 设置 C++23 标准
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Qt 自动处理
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ============================================
# libmpv 配置
# ============================================
# 设置 libmpv 路径（用户需要根据实际路径修改）
# 下载地址: https://sourceforge.net/projects/mpv-player-windows/files/libmpv/
set(MPV_SDK_PATH "${CMAKE_SOURCE_DIR}/third_party/mpv" CACHE PATH "Path to libmpv SDK")

# 检查 libmpv 是否存在
if(EXISTS "${MPV_SDK_PATH}/include/mpv/client.h")
    message(STATUS "Found libmpv at: ${MPV_SDK_PATH}")
    set(MPV_FOUND TRUE)
else()
    message(WARNING "libmpv not found at ${MPV_SDK_PATH}")
    message(WARNING "Please download libmpv from:")
    message(WARNING "  https://sourceforge.net/projects/mpv-player-windows/files/libmpv/")
    message(WARNING "And extract to: ${MPV_SDK_PATH}")
    set(MPV_FOUND FALSE)
endif()

# ============================================
# Qt6 配置（只需要基础模块）
# ============================================
find_package(Qt6 REQUIRED COMPONENTS 
    Core 
    Gui 
    Widgets
)

# 源文件
set(SOURCES
    src/main.cpp
    src/FloatingVideoPlayer.cpp
    src/FloatingVideoPlayer.h
    src/MpvWidget.cpp
    src/MpvWidget.h
)

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
)

# 包含目录
target_include_directories(${PROJECT_NAME} PRIVATE 
    ${CMAKE_SOURCE_DIR}/src
)

# ============================================
# libmpv 链接
# ============================================
if(MPV_FOUND)
    target_include_directories(${PROJECT_NAME} PRIVATE "${MPV_SDK_PATH}/include")
    
    if(WIN32)
        # Windows: 链接 mpv.lib
        target_link_libraries(${PROJECT_NAME} PRIVATE "${MPV_SDK_PATH}/libmpv.dll.a")
        
        # 复制 mpv DLL 到输出目录
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MPV_SDK_PATH}/mpv-2.dll"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
            COMMENT "Copying mpv-2.dll..."
        )
    else()
        # Linux/macOS: 使用 pkg-config
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(MPV REQUIRED mpv)
        target_include_directories(${PROJECT_NAME} PRIVATE ${MPV_INCLUDE_DIRS})
        target_link_libraries(${PROJECT_NAME} PRIVATE ${MPV_LIBRARIES})
    endif()
    
    target_compile_definitions(${PROJECT_NAME} PRIVATE MPV_AVAILABLE=1)
else()
    target_compile_definitions(${PROJECT_NAME} PRIVATE MPV_AVAILABLE=0)
endif()

# Windows 特定设置
if(WIN32)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        WIN32_EXECUTABLE TRUE
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
