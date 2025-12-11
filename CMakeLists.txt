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

# 查找 Qt6 组件
find_package(Qt6 REQUIRED COMPONENTS 
    Core 
    Gui 
    Widgets 
    Multimedia 
    MultimediaWidgets
)

# 源文件
set(SOURCES
    src/main.cpp
    src/FloatingVideoPlayer.cpp
    src/FloatingVideoPlayer.h
)

# 资源文件
set(RESOURCES
    resources/resources.qrc
)

# 创建可执行文件 (WIN32 避免控制台窗口)
if(WIN32)
    add_executable(${PROJECT_NAME} WIN32 ${SOURCES} ${RESOURCES})
else()
    add_executable(${PROJECT_NAME} ${SOURCES} ${RESOURCES})
endif()

# 链接 Qt 库
target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::MultimediaWidgets
)

# 包含目录
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/src)

# Windows 特定设置
if(WIN32)
    # 设置子系统为 Windows (避免控制台)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
    
    # 查找 windeployqt 工具
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${Qt6_DIR}/../../../bin")
    
    if(WINDEPLOYQT_EXECUTABLE)
        # 使用 windeployqt 自动部署所有 Qt 依赖（包括平台插件）
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                --no-translations
                --no-system-d3d-compiler
                --no-opengl-sw
                "$<TARGET_FILE:${PROJECT_NAME}>"
            COMMENT "Running windeployqt to deploy Qt dependencies..."
        )
    else()
        message(WARNING "windeployqt not found, you may need to manually deploy Qt DLLs")
        
        # 备用方案：手动复制 DLL
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:Qt6::Core>
                $<TARGET_FILE:Qt6::Gui>
                $<TARGET_FILE:Qt6::Widgets>
                $<TARGET_FILE:Qt6::Multimedia>
                $<TARGET_FILE:Qt6::MultimediaWidgets>
                $<TARGET_FILE_DIR:${PROJECT_NAME}>
        )
        
        # 复制平台插件
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}>/platforms"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:Qt6::QWindowsIntegrationPlugin>"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/platforms/"
        )
    endif()
endif()

# 安装规则
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

