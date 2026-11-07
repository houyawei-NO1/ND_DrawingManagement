cmake_minimum_required(VERSION 3.20)
project(ND_DrawingManagement_PC LANGUAGES CXX)

# Qt6 需要 C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 源码含中文，统一按 UTF-8 编译：MSVC 用 /utf-8，GCC/MinGW 用 -finput-charset
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
add_compile_options("$<$<CXX_COMPILER_ID:GNU>:-finput-charset=UTF-8>")
add_compile_options("$<$<CXX_COMPILER_ID:GNU>:-fexec-charset=UTF-8>")

# Find Qt 6
find_package(Qt6 COMPONENTS
    Core
    Gui
    Widgets
    Sql
    Network
    HttpServer
    Pdf
    PdfWidgets
    Svg
    REQUIRED
)

# Enable Qt automatic tools
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Source files
set(PROJECT_SOURCES
    main.cpp
    db/databasemanager.cpp
    image/heifconverter.cpp
    ui/mainwindow.cpp
    ui/entrydialog.cpp
    ui/managepage.cpp
    ui/settingsdialog.cpp
    ui/materialcodedialog.cpp
    ui/qtymanage.cpp
    ui/ndmessagebox.cpp
    ui/roledialog.cpp
    server/lanhttpserver.cpp
    server/remoteapiclient.cpp
)

# Header files
set(PROJECT_HEADERS
    model/product.h
    db/databasemanager.h
    image/heifconverter.h
    ui/mainwindow.h
    ui/entrydialog.h
    ui/settingsdialog.h
    ui/materialcodedialog.h
    ui/qtymanage.h
    ui/ndmessagebox.h
    ui/roledialog.h
    server/lanhttpserver.h
    server/remoteapiclient.h
)

# Resource files
set(PROJECT_RESOURCES
    res.qrc
)

# Windows resource file
if(WIN32)
    set(PROJECT_RESOURCES ${PROJECT_RESOURCES} logo.rc)
endif()

# Create executable
add_executable(${PROJECT_NAME}
    ${PROJECT_SOURCES}
    ${PROJECT_HEADERS}
    ${PROJECT_RESOURCES}
)

# Link Qt libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Sql
    Qt6::Network
    Qt6::HttpServer
    Qt6::Pdf
    Qt6::PdfWidgets
    Qt6::Svg
)

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Deployment settings for Qt
if(WIN32)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        WIN32_EXECUTABLE ON
    )
endif()

# Set output directory
set_target_properties(${PROJECT_NAME} PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# HEIC 解码运行库：编译后自动复制到 exe 同目录（安装包脚本从同一目录取）
if(WIN32)
    set(HEIF_RUNTIME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libheif")
    if(EXISTS "${HEIF_RUNTIME_DIR}/heif.dll")
        foreach(HEIF_DLL heif.dll libde265-0.dll)
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${HEIF_RUNTIME_DIR}/${HEIF_DLL}"
                        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/${HEIF_DLL}"
                VERBATIM)
        endforeach()
    endif()
endif()
