cmake_minimum_required(VERSION 3.16)
project(WhisperTool CXX)

set(CMAKE_CXX_STANDARD 17)

include(FetchContent)

# --- 1. WHISPER.CPP (Download and Build) ---
FetchContent_Declare(
    whisper
    GIT_REPOSITORY https://github.com/ggerganov/whisper.cpp.git
    GIT_TAG v1.5.4
)
FetchContent_MakeAvailable(whisper)

# --- 2. DIRECTORY SETUP ---
# Create a models directory in the build folder
set(MODEL_DIR "${CMAKE_BINARY_DIR}/models")
file(MAKE_DIRECTORY ${MODEL_DIR})

# --- 3. FFMPEG (Windows Binaries Example) ---
set(FFMPEG_URL "https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip")
set(FFMPEG_DIR "${CMAKE_BINARY_DIR}/ffmpeg")

if(NOT EXISTS "${FFMPEG_DIR}/bin/ffmpeg.exe")
    message(STATUS "Downloading FFmpeg binaries...")
    file(DOWNLOAD ${FFMPEG_URL} "${CMAKE_BINARY_DIR}/ffmpeg.zip" SHOW_PROGRESS)
    file(ARCHIVE_EXTRACT INPUT "${CMAKE_BINARY_DIR}/ffmpeg.zip" DESTINATION ${FFMPEG_DIR})
    
    file(GLOB_RECURSE FFMPEG_EXE "${FFMPEG_DIR}/*/bin/ffmpeg.exe")
    file(COPY ${FFMPEG_EXE} DESTINATION "${CMAKE_BINARY_DIR}")
endif()

# --- 4. TARGET CONFIGURATION ---
add_executable(${PROJECT_NAME} main.cpp)

# Link with whisper library
target_link_libraries(${PROJECT_NAME} PRIVATE whisper)

# Define paths for C++ code (FFmpeg is local to EXE)
target_compile_definitions(${PROJECT_NAME} PRIVATE 
    FFMPEG_PATH="ffmpeg.exe"
)

# --- 5. POST-BUILD ASSET COPYING ---
# Automatically copy DLLs and FFmpeg to the same directory as the EXE
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/bin/Release/whisper.dll" "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/ffmpeg.exe" "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}>/models"
)
