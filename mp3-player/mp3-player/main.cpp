// ===================================================================================
//
//                      音乐播放器 (v3.7 - 在线TTS最终版)
//
// ===================================================================================

// C++ Standard Library & System
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <set>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <random>
#include <future>
#include <iomanip>
#include <ctime>

// Vendor Libraries
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"
#include <SDL.h>
#include <SDL_opengl.h>
#include "miniaudio.h"

// Libraries for Online TTS
#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>

// ======== 全局变量, 枚举, 数据结构 (与之前版本相同) ========
ImFont* G_Font_Default = nullptr;
ImFont* G_Font_Large = nullptr;
enum class PlayMode { ListLoop, RepeatOne, Shuffle };
enum class ActiveView { Main };
enum class PlayDirection { New, Back };
struct Song { std::string filePath, displayName, artist, album; };
struct AudioState {
    ma_decoder decoder;
    ma_mutex mutex;
    bool isAudioReady = false;
    std::string currentFilePath = "";
    int currentIndex = -1;
    PlayMode playMode = PlayMode::ListLoop;
    std::vector<int> playHistory;
    float totalSongDurationSec = 0.0f;
    Uint32 songStartTime = 0;
    float elapsedTimeAtPause = 0.0f;
    ma_decoder tts_decoder;
    std::vector<char> tts_audio_buffer;
    bool is_tts_decoder_initialized = false;
    bool is_playing_tts = false;
};
void StopAndUnloadSong(AudioState& audioState);
bool PlaySongAtIndex(AudioState& audioState, std::vector<Song>& playlist, int index, PlayDirection direction);
void ShowLeftSidebar(ImVec2 pos, ImVec2 size, ActiveView& currentView);
void ShowPlaylistWindow(AudioState& audioState, std::vector<Song>& mainPlaylist, std::vector<std::string>& musicDirs, ActiveView currentView, ImVec2 pos, ImVec2 size);
void ShowPlayerWindow(AudioState& audioState, std::vector<Song>& mainPlaylist, std::vector<Song>& activePlaylist, float& volume_percent, float progress, ImVec2 pos, ImVec2 size);
int GetNextSongIndex(AudioState& audioState, int listSize);
void SetModernDarkStyle();
void ScanDirectoryForMusic(const std::string& path, std::vector<Song>& playlist);
void RebuildPlaylist(std::vector<Song>& playlist, const std::vector<std::string>& musicDirs);
bool FilledSlider(const char* label, float* v, float v_min, float v_max, const ImVec4& filled_color, const char* format);
ma_device g_device;
AudioState g_audioState;

// ======== 函数前置声明 ========
// ...

// ======== 在线TTS及辅助函数 ========

// Base64 编码函数
std::string base64_encode(const unsigned char* bytes_to_encode, unsigned int in_len) {
    std::string ret;
    int i = 0, j = 0;
    unsigned char char_array_3[3], char_array_4[4];
    const std::string base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    while (in_len--) {
        char_array_3[i++] = *(bytes_to_encode++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;
            for (i = 0; i < 4; i++) ret += base64_chars[char_array_4[i]];
            i = 0;
        }
    }
    if (i) {
        for (j = i; j < 3; j++) char_array_3[j] = '\0';
        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        for (j = 0; j < i + 1; j++) ret += base64_chars[char_array_4[j]];
        while (i++ < 3) ret += '=';
    }
    return ret;
}
std::string base64_encode(const std::string& str) {
    return base64_encode(reinterpret_cast<const unsigned char*>(str.c_str()), str.length());
}

// [最终版] fetch_tts_audio, 实现真正的在线语音合成
bool fetch_tts_audio(const std::string& text_to_speak, std::vector<char>& audio_buffer) {
    // 1. --- 讯飞API配置 - 请务必替换为您自己的凭证 ---
    const std::string APPID      = "9474efc2";
    const std::string API_KEY    = "ZmRiMjRkOGU2YmY5NzQxNDUzODZmNzM0";
    const std::string API_SECRET = "adca69353372a14a6ad512271fd63eea";

    // 检查是否填写凭证
    if (APPID == "YOUR_APPID" || API_KEY == "YOUR_API_KEY" || API_SECRET == "YOUR_API_SECRET") {
        fprintf(stderr, "错误: 请在main.cpp的fetch_tts_audio函数中填入您的讯飞API凭证。\n");
        return false;
    }

    // 2. --- 构建认证URL (与之前版本相同) ---
    std::string host = "tts-api.xfyun.cn";
    std::string path = "/v2/tts";
    auto now = std::chrono::system_clock::now();
    auto itt = std::chrono::system_clock::to_time_t(now);
    std::stringstream date_ss;
    date_ss << std::put_time(std::gmtime(&itt), "%a, %d %b %Y %H:%M:%S GMT");
    std::string date = date_ss.str();
    
    std::string signature_origin = "host: " + host + "\ndate: " + date + "\nGET " + path + " HTTP/1.1";
    unsigned char digest[SHA256_DIGEST_LENGTH];
    HMAC(EVP_sha256(), API_SECRET.c_str(), API_SECRET.length(), (unsigned char*)signature_origin.c_str(), signature_origin.length(), digest, nullptr);
    std::string signature_base64 = base64_encode(digest, SHA256_DIGEST_LENGTH);
    std::string authorization_origin = "api_key=\"" + API_KEY + "\", algorithm=\"hmac-sha256\", headers=\"host date request-line\", signature=\"" + signature_base64 + "\"";
    std::string authorization = base64_encode(authorization_origin);
    
    std::string request_url = "wss://" + host + path + "?authorization=" + authorization + "&date=" + date + "&host=" + host;

    // 3. --- 使用 ixwebsocket 进行通信 ---
    ix::WebSocket webSocket;
    std::promise<bool> promise;
    auto future = promise.get_future();

    // ======== [修正] 消息处理回调函数 ========
    webSocket.setOnMessageCallback([&](const ix::WebSocketMessagePtr& msg) {
        if (msg->type == ix::WebSocketMessageType::Message) {
            // 首先判断消息是二进制还是文本
            if (msg->binary) {
                // 如果是二进制，说明是音频数据流
                // 数据存放在 msg->str 中，将其追加到我们的缓冲区
                audio_buffer.insert(audio_buffer.end(), msg->str.begin(), msg->str.end());
            } else {
                // 如果是文本，说明是JSON格式的状态信息
                printf("WebSocket: 收到文本消息: %s\n", msg->str.c_str());
                // 检查是否是最后一条消息
                if (msg->str.find("\"status\":2") != std::string::npos) {
                    webSocket.close();
                }
                // 检查讯飞是否返回错误码
                if (msg->str.find("\"code\":0") == std::string::npos) {
                    fprintf(stderr, "讯飞API返回错误: %s\n", msg->str.c_str());
                    webSocket.close();
                }
            }
        } 
        else if (msg->type == ix::WebSocketMessageType::Open) {
            printf("WebSocket: 连接已建立。正在发送合成请求...\n");
            // 连接成功后，发送包含业务参数的JSON帧
            std::string frame_text = "{\"common\":{\"app_id\":\"" + APPID + "\"}," +
                                     "\"business\":{\"vcn\":\"xiaoyan\",\"aue\":\"raw\",\"sfl\":1,\"tte\":\"UTF8\"}," +
                                     "\"data\":{\"text\":\"" + base64_encode(text_to_speak) + "\",\"status\":2}}";
            webSocket.send(frame_text);
        } 
        else if (msg->type == ix::WebSocketMessageType::Close) {
            printf("WebSocket: 连接已关闭。\n");
            promise.set_value(!audio_buffer.empty()); // 如果收到数据则任务成功
        } 
        else if (msg->type == ix::WebSocketMessageType::Error) {
            fprintf(stderr, "WebSocket 错误: %s\n", msg->errorInfo.reason.c_str());
            promise.set_value(false); // 出错则任务失败
        }
    });
    // ==========================================

    webSocket.setUrl(request_url);
    printf("正在连接到 %s\n", request_url.c_str());
    webSocket.start();

    // 等待WebSocket操作完成 (最多等待10秒)
    if (future.wait_for(std::chrono::seconds(10)) == std::future_status::timeout) {
        fprintf(stderr, "错误: 在线语音合成超时。\n");
        webSocket.stop();
        return false;
    }

    return future.get();
}
// 音频回调函数
void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    AudioState* audioState = (AudioState*)pDevice->pUserData;
    if (audioState == NULL) return;

    ma_mutex_lock(&audioState->mutex);

    if (audioState->is_playing_tts && audioState->is_tts_decoder_initialized) {
        ma_uint64 framesRead = ma_decoder_read_pcm_frames(&audioState->tts_decoder, pOutput, frameCount, NULL);
        if (framesRead < frameCount) {
            audioState->is_playing_tts = false;
            printf("TTS finished, starting song playback.\n");
            audioState->songStartTime = SDL_GetTicks();
            audioState->elapsedTimeAtPause = 0.0f;
            ma_uint32 framesOfSong = frameCount - (ma_uint32)framesRead;
            void* pSongOutput = (ma_int8*)pOutput + framesRead * ma_get_bytes_per_frame(pDevice->playback.format, pDevice->playback.channels);
            if (audioState->isAudioReady) {
                ma_decoder_read_pcm_frames(&audioState->decoder, pSongOutput, framesOfSong, NULL);
            } else {
                memset(pSongOutput, 0, framesOfSong * ma_get_bytes_per_frame(pDevice->playback.format, pDevice->playback.channels));
            }
        }
    } else if (audioState->isAudioReady) {
        ma_decoder_read_pcm_frames(&audioState->decoder, pOutput, frameCount, NULL);
    } else {
        memset(pOutput, 0, frameCount * ma_get_bytes_per_frame(pDevice->playback.format, pDevice->playback.channels));
    }

    ma_mutex_unlock(&audioState->mutex);
}

// [新架构] 停止播放并仅卸载解码器
void StopAndUnloadSong(AudioState& audioState) {
    ma_device_stop(&g_device);

    ma_mutex_lock(&audioState.mutex);
    if (audioState.isAudioReady) {
        ma_decoder_uninit(&audioState.decoder);
    }
    if (audioState.is_tts_decoder_initialized) {
        ma_decoder_uninit(&audioState.tts_decoder);
    }
    ma_mutex_unlock(&audioState.mutex);

    audioState.isAudioReady = false;
    audioState.is_tts_decoder_initialized = false;
    audioState.is_playing_tts = false;
    audioState.currentIndex = -1;
    audioState.currentFilePath = "";
    audioState.totalSongDurationSec = 0.0f;
    audioState.elapsedTimeAtPause = 0.0f;
}

int GetNewRandomIndex(int current, int listSize) {
    if (listSize <= 1) return 0;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(0, listSize - 1);
    int nextIndex;
    do { nextIndex = distrib(gen); } while (nextIndex == current);
    return nextIndex;
}

int GetNextSongIndex(AudioState& audioState, int listSize) {
    if (listSize == 0) return -1;
    switch (audioState.playMode) {
        case PlayMode::RepeatOne: return audioState.currentIndex;
        case PlayMode::Shuffle: return GetNewRandomIndex(audioState.currentIndex, listSize);
        case PlayMode::ListLoop: default: return (audioState.currentIndex + 1) % listSize;
    }
}

// [新架构] 播放歌曲的核心函数
bool PlaySongAtIndex(AudioState& audioState, std::vector<Song>& playlist, int index, PlayDirection direction) {
    printf("DEBUG: 1. Entering PlaySongAtIndex for song: %s\n", playlist[index].displayName.c_str());

    if (playlist.empty() || index < 0 || index >= playlist.size()) return false;
    if (direction == PlayDirection::New && audioState.currentIndex != -1) audioState.playHistory.push_back(audioState.currentIndex);

    StopAndUnloadSong(audioState);
    printf("DEBUG: 2. Old song stopped and unloaded.\n");

    const Song& songToPlay = playlist[index];
    const char* filePath = songToPlay.filePath.c_str();

    if (ma_decoder_init_file(filePath, NULL, &audioState.decoder) != MA_SUCCESS) {
        printf("Could not load song file: %s\n", filePath);
        return false;
    }
    ma_uint64 totalFrames;
    ma_decoder_get_length_in_pcm_frames(&audioState.decoder, &totalFrames);
    audioState.totalSongDurationSec = (totalFrames > 0) ? (float)totalFrames / audioState.decoder.outputSampleRate : 0.0f;
    printf("DEBUG: 3. Song decoder initialized.\n");

    bool ttsSucceeded = false;
    std::string tts_text = "接下来播放的是 " + songToPlay.displayName;
    audioState.tts_audio_buffer.clear();
    
    if (fetch_tts_audio(tts_text, audioState.tts_audio_buffer) && !audioState.tts_audio_buffer.empty()) {
        ma_decoder_config tts_decoder_config = ma_decoder_config_init_default();
        if (ma_decoder_init_memory(audioState.tts_audio_buffer.data(), audioState.tts_audio_buffer.size(), &tts_decoder_config, &audioState.tts_decoder) == MA_SUCCESS) {
            audioState.is_tts_decoder_initialized = true;
            ttsSucceeded = true;
            printf("DEBUG: 4. TTS decoder initialized from memory.\n");
        } else {
             printf("DEBUG: 4. Failed to init TTS decoder from memory.\n");
        }
    }
    
    if (!ttsSucceeded) {
        printf("DEBUG: 4. TTS fetch/init failed, skipping.\n");
    }

    ma_mutex_lock(&audioState.mutex);
    audioState.isAudioReady = true;
    audioState.is_playing_tts = ttsSucceeded;
    ma_mutex_unlock(&audioState.mutex);
    
    audioState.currentFilePath = filePath;
    audioState.currentIndex = index;
    audioState.elapsedTimeAtPause = 0.0f;

    // ======== [修正] 修复了这里的拼写错误 ========
    if (ma_device_start(&g_device) != MA_SUCCESS) {
        printf("Failed to start audio device.\n");
        StopAndUnloadSong(audioState);
        return false;
    }
    // ==========================================
    
    printf("DEBUG: 5. Device started. Playback commencing.\n");

    if (!ttsSucceeded) {
        audioState.songStartTime = SDL_GetTicks();
    }
    
    return true;
}


// ======== UI及其他辅助函数 (大部分无逻辑修改) ========
void SaveConfig(const std::vector<std::string>& musicDirs);
void LoadConfig(std::vector<std::string>& musicDirs);
void ScanDirectoryForMusic(const std::string& path, std::vector<Song>& playlist);
void RebuildPlaylist(std::vector<Song>& playlist, const std::vector<std::string>& musicDirs);
bool FilledSlider(const char* label, float* v, float v_min, float v_max, const ImVec4& filled_color, const char* format);
void SetModernDarkStyle();


// ======== 主程序入口 ========
int main(int, char**) {
    // 初始化 SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO) != 0) {
        printf("Error: %s\n", SDL_GetError()); return -1;
    }

    // 初始化 OpenGL and Window
    const char* glsl_version = "#version 130";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Window* window = SDL_CreateWindow("音乐播放器 v3.6", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, window_flags);
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1);
    
    // 初始化 ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    const char* font_path = "font.ttf";
    if (std::filesystem::exists(font_path)) {
        ImFontConfig font_config;
        G_Font_Default = io.Fonts->AddFontFromFileTTF(font_path, 22.0f, &font_config, io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
        G_Font_Large = io.Fonts->AddFontFromFileTTF(font_path, 32.0f, &font_config, io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
        printf("Font '%s' loaded.\n", font_path);
    } else {
        printf("WARNING: Font file '%s' not found. Chinese characters will not display.\n", font_path);
    }
    SetModernDarkStyle();
    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init(glsl_version);
    
    // [新架构] 在程序启动时初始化一次音频设备
    if (ma_mutex_init(&g_audioState.mutex) != MA_SUCCESS) return -1;
    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format   = ma_format_f32;
    deviceConfig.playback.channels = 2;
    deviceConfig.sampleRate        = 44100;
    deviceConfig.dataCallback      = data_callback;
    deviceConfig.pUserData         = &g_audioState;
    if (ma_device_init(NULL, &deviceConfig, &g_device) != MA_SUCCESS) {
        printf("Failed to initialize playback device.\n"); return -1;
    }

    // 加载播放列表和配置
    float volume_percent = 50.0f;
    std::vector<Song> mainPlaylist;
    std::vector<std::string> musicDirs;
    LoadConfig(musicDirs);
    RebuildPlaylist(mainPlaylist, musicDirs);
    ActiveView currentView = ActiveView::Main;
    float display_progress = 0.0f;
    bool done = false;

    // 主循环
    while (!done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT || (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID(window))) done = true;
        }

        bool isPlaying = ma_device_is_started(&g_device);
        if (g_audioState.isAudioReady && !g_audioState.is_playing_tts) {
            float totalElapsedTime = isPlaying ? g_audioState.elapsedTimeAtPause + (float)(SDL_GetTicks() - g_audioState.songStartTime) / 1000.0f : g_audioState.elapsedTimeAtPause;
            display_progress = (g_audioState.totalSongDurationSec > 0) ? (totalElapsedTime / g_audioState.totalSongDurationSec) : 0.0f;
            if (isPlaying && display_progress >= 1.0f) {
                if (g_audioState.playMode == PlayMode::RepeatOne) {
                    PlaySongAtIndex(g_audioState, mainPlaylist, g_audioState.currentIndex, PlayDirection::New);
                } else {
                    int nextIndex = GetNextSongIndex(g_audioState, mainPlaylist.size());
                    if (nextIndex != -1) PlaySongAtIndex(g_audioState, mainPlaylist, nextIndex, PlayDirection::New);
                    else StopAndUnloadSong(g_audioState);
                }
            }
        } else if (!g_audioState.isAudioReady) {
            display_progress = 0.0f;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        ma_device_set_master_volume(&g_device, volume_percent / 100.0f);
        
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const float playerHeight = 120.0f;
        const float leftSidebarWidth = 220.0f;
        ShowLeftSidebar(viewport->Pos, ImVec2(leftSidebarWidth, viewport->Size.y - playerHeight), currentView);
        ShowPlaylistWindow(g_audioState, mainPlaylist, musicDirs, currentView, ImVec2(viewport->Pos.x + leftSidebarWidth, viewport->Pos.y), ImVec2(viewport->Size.x - leftSidebarWidth, viewport->Size.y - playerHeight));
        ShowPlayerWindow(g_audioState, mainPlaylist, mainPlaylist, volume_percent, display_progress, ImVec2(viewport->Pos.x, viewport->Pos.y + viewport->Size.y - playerHeight), ImVec2(viewport->Size.x, playerHeight));
        
        ImGui::Render();
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        glClearColor(0.95f, 0.95f, 0.95f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    // 清理
    StopAndUnloadSong(g_audioState);
    ma_device_uninit(&g_device);
    ma_mutex_uninit(&g_audioState.mutex);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}


// ======== UI渲染函数的具体实现 ========

void ShowLeftSidebar(ImVec2 pos, ImVec2 size, ActiveView& currentView) {
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::Begin("Sidebar", nullptr, flags);
    ImGui::Text(" ");
    ImGui::Text(" 小李的音乐盒");
    ImGui::Separator();
    if (ImGui::Selectable("   列表", currentView == ActiveView::Main)) {
        currentView = ActiveView::Main;
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void ShowPlaylistWindow(AudioState& audioState, std::vector<Song>& mainPlaylist, std::vector<std::string>& musicDirs, ActiveView currentView, ImVec2 pos, ImVec2 size) {
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGuiWindowFlags playlist_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::Begin("Playlist", nullptr, playlist_flags);
    ImGui::BeginChild("DirManagement", ImVec2(0, 150), true, ImGuiWindowFlags_NoScrollbar);
    ImGui::Text("音乐文件夹路径");
    ImGui::Separator();
    static char add_dir_buf[1024] = "";
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - 100);
    ImGui::InputTextWithHint("##AddPath", "在此输入新文件夹路径...", add_dir_buf, sizeof(add_dir_buf));
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("扫描音乐", ImVec2(90, 0))) {
        if (strlen(add_dir_buf) > 0) {
            std::string new_dir = add_dir_buf;
            if (std::find(musicDirs.begin(), musicDirs.end(), new_dir) == musicDirs.end()) {
                musicDirs.push_back(new_dir);
                SaveConfig(musicDirs);
            }
            strcpy(add_dir_buf, "");
        }
        RebuildPlaylist(mainPlaylist, musicDirs);
    }
    ImGui::BeginChild("##DirList", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
    if(musicDirs.empty()){
        ImGui::TextDisabled("暂无文件夹，请在上方添加...");
    } else {
        for (const auto& dir : musicDirs) ImGui::TextUnformatted(dir.c_str());
    }
    ImGui::EndChild();
    ImGui::EndChild();
    ImGui::Text("音乐列表 (%zu首)", mainPlaylist.size());
    ImGui::Separator();
    if (ImGui::BeginTable("playlist_table", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 40.0f);
        ImGui::TableSetupColumn("歌曲", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("歌手", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(mainPlaylist.size());
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                ImGui::TableNextRow();
                Song& song = mainPlaylist[i];
                bool is_selected = (audioState.currentIndex == i);
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%d", i + 1);
                ImGui::TableSetColumnIndex(1);
                if (ImGui::Selectable(song.displayName.c_str(), is_selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                    if (ImGui::IsMouseDoubleClicked(0)) PlaySongAtIndex(audioState, mainPlaylist, i, PlayDirection::New);
                }
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%s", song.artist.c_str());
            }
        }
        clipper.End();
        ImGui::EndTable();
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void ShowPlayerWindow(AudioState& audioState, std::vector<Song>& mainPlaylist, std::vector<Song>& activePlaylist, float& volume_percent, float progress, ImVec2 pos, ImVec2 size) {
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGuiWindowFlags player_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;
    
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1.0f, 1.0f, 1.0f, 0.98f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f, 0.0f));
    ImGui::Begin("Player", nullptr, player_flags);

    const ImVec4 filled_color = ImGui::GetStyle().Colors[ImGuiCol_Button];

    if (ImGui::BeginTable("PlayerLayout", 3, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableSetupColumn("Info", ImGuiTableColumnFlags_WidthStretch, 0.3f);
        ImGui::TableSetupColumn("Controls", ImGuiTableColumnFlags_WidthStretch, 0.4f);
        ImGui::TableSetupColumn("Volume", ImGuiTableColumnFlags_WidthStretch, 0.3f);
        ImGui::TableNextRow(ImGuiTableRowFlags_None, size.y);

        ImGui::TableSetColumnIndex(0);
        Song* currentSong = (audioState.isAudioReady && audioState.currentIndex >= 0 && audioState.currentIndex < activePlaylist.size()) ? &activePlaylist[audioState.currentIndex] : nullptr;
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 15.f);
        ImGui::BeginGroup();
        if (currentSong) {
            if (G_Font_Large) ImGui::PushFont(G_Font_Large);
            ImGui::Text("%s", currentSong->displayName.c_str());
            if (G_Font_Large) ImGui::PopFont();
            ImGui::Text("%s", currentSong->artist.c_str());
        } else {
            if (G_Font_Large) ImGui::PushFont(G_Font_Large);
            ImGui::Text("未加载歌曲");
            if (G_Font_Large) ImGui::PopFont();
        }
        ImGui::EndGroup();

        ImGui::TableSetColumnIndex(1);
        ImGui::BeginDisabled(!audioState.isAudioReady);
        {
            float button_size = ImGui::GetTextLineHeight() * 1.5f;
            float controls_total_height = button_size + ImGui::GetTextLineHeight() + ImGui::GetStyle().ItemSpacing.y * 2;
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (size.y - controls_total_height) * 0.5f);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - (button_size * 2 + 80.f + ImGui::GetStyle().ItemSpacing.x * 2)) * 0.5f);
            
            bool isPlaying = ma_device_is_started(&g_device);
            if (ImGui::ArrowButton("##prev", ImGuiDir_Left)) {
                if (!audioState.playHistory.empty()) {
                    PlaySongAtIndex(audioState, activePlaylist, audioState.playHistory.back(), PlayDirection::Back);
                    audioState.playHistory.pop_back();
                } else if (!activePlaylist.empty()) {
                    PlaySongAtIndex(audioState, activePlaylist, (audioState.currentIndex - 1 + activePlaylist.size()) % activePlaylist.size(), PlayDirection::New);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button(isPlaying ? "暂停" : "播放", ImVec2(80.f, 0))) {
                if (isPlaying) {
                    ma_device_stop(&g_device);
                    if (!audioState.is_playing_tts) {
                        audioState.elapsedTimeAtPause += (float)(SDL_GetTicks() - audioState.songStartTime) / 1000.0f;
                    }
                } else {
                    ma_device_start(&g_device);
                    if (!audioState.is_playing_tts) {
                        audioState.songStartTime = SDL_GetTicks();
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::ArrowButton("##next", ImGuiDir_Right)) {
                if (!activePlaylist.empty()) PlaySongAtIndex(audioState, activePlaylist, GetNextSongIndex(audioState, activePlaylist.size()), PlayDirection::New);
            }

            char time_str_elapsed[64];
            if (audioState.is_playing_tts) {
                sprintf(time_str_elapsed, "00:00");
            } else {
                sprintf(time_str_elapsed, "%02d:%02d", (int)(progress * audioState.totalSongDurationSec) / 60, (int)(progress * audioState.totalSongDurationSec) % 60);
            }
            
            ImGui::BeginDisabled(audioState.is_playing_tts);
            if (FilledSlider("##Progress", &progress, 0.0f, 1.0f, filled_color, time_str_elapsed)) {
                if (audioState.isAudioReady) {
                    ma_mutex_lock(&audioState.mutex);
                    ma_uint64 targetFrame = (ma_uint64)(progress * audioState.totalSongDurationSec * audioState.decoder.outputSampleRate);
                    ma_decoder_seek_to_pcm_frame(&audioState.decoder, targetFrame);
                    audioState.elapsedTimeAtPause = progress * audioState.totalSongDurationSec;
                    if(isPlaying) { audioState.songStartTime = SDL_GetTicks(); }
                    ma_mutex_unlock(&audioState.mutex);
                }
            }
            ImGui::EndDisabled();
        }
        ImGui::EndDisabled();

        ImGui::TableSetColumnIndex(2);
        const char* modeText = "";
        switch (audioState.playMode) {
            case PlayMode::ListLoop: modeText = "顺序"; break;
            case PlayMode::RepeatOne: modeText = "单曲"; break;
            case PlayMode::Shuffle: modeText = "随机"; break;
        }
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (size.y - ImGui::GetFrameHeight()) * 0.5f);
        if (ImGui::Button(modeText)) {
            audioState.playMode = (PlayMode)(((int)audioState.playMode + 1) % 3);
            audioState.playHistory.clear();
        }
        ImGui::SameLine();
        ImGui::PushItemWidth(150);
        FilledSlider("##Volume", &volume_percent, 0.0f, 100.0f, filled_color, "音量 %.0f%%");
        ImGui::PopItemWidth();

        ImGui::EndTable();
    }
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}


// ======== 辅助函数定义 ========
// (这些函数之前只有声明，现在把它们的定义放在这里)

void SaveConfig(const std::vector<std::string>& musicDirs) {
    std::ofstream configFile("config.txt");
    if (configFile.is_open()) for (const auto& dir : musicDirs) configFile << dir << std::endl;
}

void LoadConfig(std::vector<std::string>& musicDirs) {
    std::ifstream configFile("config.txt");
    if (configFile.is_open()) {
        std::string line;
        while (std::getline(configFile, line)) if (!line.empty()) musicDirs.push_back(line);
    }
}

void ScanDirectoryForMusic(const std::string& path, std::vector<Song>& playlist) {
    const std::set<std::string> supported_extensions = {".mp3", ".wav", ".flac"};
    try {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(path)) {
            if (!entry.is_regular_file()) continue;
            std::string extension = entry.path().extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c){ return std::tolower(c); });
            if (supported_extensions.count(extension)) {
                std::string full_path = entry.path().string();
                if (std::find_if(playlist.begin(), playlist.end(), [&](const Song& s){ return s.filePath == full_path; }) != playlist.end()) continue;
                Song newSong;
                newSong.filePath = full_path;
                std::string filename = entry.path().stem().string();
                size_t separatorPos = filename.find(" - ");
                if (separatorPos != std::string::npos) {
                    newSong.artist = filename.substr(0, separatorPos);
                    newSong.displayName = filename.substr(separatorPos + 3);
                } else {
                    newSong.displayName = filename;
                    newSong.artist = "未知艺术家";
                }
                newSong.album = "未知专辑";
                playlist.push_back(newSong);
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        printf("Filesystem error scanning %s: %s\n", path.c_str(), e.what());
    }
}

void RebuildPlaylist(std::vector<Song>& playlist, const std::vector<std::string>& musicDirs) {
    playlist.clear();
    for (const auto& dir : musicDirs) ScanDirectoryForMusic(dir, playlist);
    printf("Playlist rebuilt. Total songs: %zu\n", playlist.size());
}

bool FilledSlider(const char* label, float* v, float v_min, float v_max, const ImVec4& filled_color, const char* format) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
    ImVec2 pos = window->DC.CursorPos;
    ImVec2 size(ImGui::GetContentRegionAvail().x, g.FontSize + style.FramePadding.y * 2.0f);
    const ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));
    ImGui::ItemSize(bb, style.FramePadding.y);
    if (!ImGui::ItemAdd(bb, id)) return false;
    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
    if (held && bb.GetWidth() > 0.0f) {
        const float mouse_x_clamped = ImClamp(g.IO.MousePos.x, bb.Min.x, bb.Max.x);
        const float new_value = (mouse_x_clamped - bb.Min.x) / bb.GetWidth();
        *v = v_min + new_value * (v_max - v_min);
    }
    const ImU32 bg_col = ImGui::GetColorU32(ImGuiCol_FrameBg);
    window->DrawList->AddRectFilled(bb.Min, bb.Max, bg_col, style.FrameRounding);
    float grab_px = (*v - v_min) / (v_max - v_min) * (bb.GetWidth());
    ImVec2 filled_max = ImVec2(bb.Min.x + grab_px, bb.Max.y);
    const ImU32 filled_col = ImGui::GetColorU32(filled_color);
    window->DrawList->AddRectFilled(bb.Min, filled_max, filled_col, style.FrameRounding);
    if (format) {
        char value_buf[64];
        ImFormatString(value_buf, IM_ARRAYSIZE(value_buf), format, *v);
        ImGui::RenderTextClipped(bb.Min, bb.Max, value_buf, NULL, NULL, style.ButtonTextAlign, &bb);
    }
    return held;
}

void SetModernDarkStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding     = ImVec2(10, 10); style.FramePadding      = ImVec2(10, 6);
    style.ItemSpacing       = ImVec2(10, 6);  style.ItemInnerSpacing  = ImVec2(6, 6);
    style.WindowRounding    = 8.0f;           style.FrameRounding     = 6.0f;
    style.GrabRounding      = 6.0f;           style.PopupRounding     = 6.0f;
    style.ScrollbarRounding = 6.0f;           style.TabRounding       = 6.0f;
    auto& colors = style.Colors;
    colors[ImGuiCol_Text] = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.95f, 0.95f, 0.95f, 0.98f);
    colors[ImGuiCol_ChildBg] = ImVec4(1.00f, 1.00f, 1.00f, 0.95f);
    colors[ImGuiCol_Border] = ImVec4(0.85f, 0.85f, 0.85f, 0.50f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.88f, 0.88f, 0.88f, 0.80f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.95f, 0.95f, 0.95f, 0.80f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.75f, 0.75f, 0.75f, 0.80f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.40f, 0.60f, 0.90f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.50f, 0.70f, 1.00f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.45f, 0.65f, 0.95f, 0.50f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.85f, 0.40f, 0.40f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.95f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_Separator] = colors[ImGuiCol_Border];
    colors[ImGuiCol_TableHeaderBg] = ImVec4(0.76f, 0.82f, 0.92f, 1.00f);
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.85f, 0.85f, 0.85f, 1.00f);
    colors[ImGuiCol_TableBorderStrong] = ImVec4(0.70f, 0.70f, 0.70f, 1.00f);
    colors[ImGuiCol_Button]               = ImVec4(0.30f, 0.60f, 0.90f, 1.00f);
    colors[ImGuiCol_ButtonHovered]        = ImVec4(0.40f, 0.70f, 1.00f, 1.00f);
    colors[ImGuiCol_ButtonActive]         = ImVec4(0.25f, 0.55f, 0.85f, 1.00f);
}