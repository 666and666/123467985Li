// ===================================================================================
//
//                              音乐播放器 (v3.4 - UI最终美化版)
//
// 本次改良:
// 1. [颜色修改] 将所有按钮的颜色从淡红色系改为了天空蓝色系。
// 2. [功能新增] 创建了一个自定义的 FilledSlider 函数，实现了带填充效果的进度条。
// 3. [UI替换] 使用新的 FilledSlider 替换了音乐进度和音量控制滑块。
//
// ===================================================================================

#include "imgui.h"
#include "imgui_internal.h" // 需要包含 internal.h 来使用更底层的API
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"
#include <cstdio>
#include <SDL.h>
#include <SDL_opengl.h>
#include <string>
#include <vector>
#include <filesystem>
#include <set>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <random>

#include "miniaudio.h"

// ======== 全局字体 ========
ImFont* G_Font_Default = nullptr;
ImFont* G_Font_Large = nullptr;

// ======== 枚举与数据结构 ========
enum class PlayMode { ListLoop, RepeatOne, Shuffle };
enum class ActiveView { Main };
enum class PlayDirection { New, Back };

struct Song {
    std::string filePath;
    std::string displayName;
    std::string artist;
    std::string album;
};

struct AudioState {
    ma_decoder decoder;
    ma_device device;
    ma_mutex mutex;
    bool isAudioReady = false;
    bool isDeviceInitialized = false;
    std::string currentFilePath = "";
    int currentIndex = -1;
    PlayMode playMode = PlayMode::ListLoop;
    std::vector<int> playHistory;
    float totalSongDurationSec = 0.0f;
    Uint32 songStartTime = 0;
    float elapsedTimeAtPause = 0.0f;
};

// ======== 函数前置声明 ========
void StopAndUnloadSong(AudioState& audioState);
bool PlaySongAtIndex(AudioState& audioState, std::vector<Song>& playlist, int index, PlayDirection direction);
void ShowLeftSidebar(ImVec2 pos, ImVec2 size, ActiveView& currentView);
void ShowPlayerWindow(AudioState& audioState, std::vector<Song>& mainPlaylist, std::vector<Song>& activePlaylist, float& volume_percent, float progress, ImVec2 pos, ImVec2 size);
void ShowPlaylistWindow(AudioState& audioState, std::vector<Song>& mainPlaylist, std::vector<std::string>& musicDirs, ActiveView currentView, ImVec2 pos, ImVec2 size);
int GetNextSongIndex(AudioState& audioState, int listSize);
void SetModernDarkStyle();
void ScanDirectoryForMusic(const std::string& path, std::vector<Song>& playlist);
void RebuildPlaylist(std::vector<Song>& playlist, const std::vector<std::string>& musicDirs);

// [新增功能] 一个自定义的、带填充效果的滑块函数
bool FilledSlider(const char* label, float* v, float v_min, float v_max, const ImVec4& filled_color, const char* format) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);

    ImVec2 pos = window->DC.CursorPos;
    ImVec2 size(ImGui::GetContentRegionAvail().x, g.FontSize + style.FramePadding.y * 2.0f);

    const ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));
    ImGui::ItemSize(bb, style.FramePadding.y);
    if (!ImGui::ItemAdd(bb, id))
        return false;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);

    // 绘制背景
    const ImU32 bg_col = ImGui::GetColorU32(ImGuiCol_FrameBg);
    window->DrawList->AddRectFilled(bb.Min, bb.Max, bg_col, style.FrameRounding);

    // 处理交互
    if (held && bb.GetWidth() > 0.0f) {
        const float mouse_x_clamped = ImClamp(g.IO.MousePos.x, bb.Min.x, bb.Max.x);
        const float new_value = (mouse_x_clamped - bb.Min.x) / bb.GetWidth();
        *v = v_min + new_value * (v_max - v_min);
    }

    // 绘制填充部分
    float grab_px = (*v - v_min) / (v_max - v_min) * (bb.GetWidth());
    ImVec2 filled_max = ImVec2(bb.Min.x + grab_px, bb.Max.y);
    const ImU32 filled_col = ImGui::GetColorU32(filled_color);
    window->DrawList->AddRectFilled(bb.Min, filled_max, filled_col, style.FrameRounding);

    // 绘制文本
    if (format) {
        char value_buf[64];
        ImFormatString(value_buf, IM_ARRAYSIZE(value_buf), format, *v);
        ImGui::RenderTextClipped(bb.Min, bb.Max, value_buf, NULL, NULL, style.ButtonTextAlign, &bb);
    }
    
    return held;
}

// ======== 音频、配置与逻辑函数 ========

void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    AudioState* audioState = (AudioState*)pDevice->pUserData;
    if (audioState == NULL) return;
    ma_mutex_lock(&audioState->mutex);
    if (audioState->isAudioReady) {
        ma_decoder_read_pcm_frames(&audioState->decoder, pOutput, frameCount, NULL);
    } else {
        memset(pOutput, 0, frameCount * ma_get_bytes_per_frame(pDevice->playback.format, pDevice->playback.channels));
    }
    ma_mutex_unlock(&audioState->mutex);
}

void StopAndUnloadSong(AudioState& audioState) {
    if (audioState.isDeviceInitialized) {
        ma_device_stop(&audioState.device);
        ma_device_uninit(&audioState.device);
    }
    ma_mutex_lock(&audioState.mutex);
    if (audioState.isAudioReady) ma_decoder_uninit(&audioState.decoder);
    ma_mutex_unlock(&audioState.mutex);
    audioState.isAudioReady = false;
    audioState.isDeviceInitialized = false;
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

bool PlaySongAtIndex(AudioState& audioState, std::vector<Song>& playlist, int index, PlayDirection direction) {
    if (playlist.empty() || index < 0 || index >= playlist.size()) return false;
    if (direction == PlayDirection::New && audioState.currentIndex != -1) audioState.playHistory.push_back(audioState.currentIndex);
    StopAndUnloadSong(audioState);
    const char* filePath = playlist[index].filePath.c_str();
    if (ma_decoder_init_file(filePath, NULL, &audioState.decoder) != MA_SUCCESS) {
        printf("Could not load file: %s\n", filePath); return false;
    }
    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format = audioState.decoder.outputFormat;
    deviceConfig.playback.channels = audioState.decoder.outputChannels;
    deviceConfig.sampleRate = audioState.decoder.outputSampleRate;
    deviceConfig.dataCallback = data_callback;
    deviceConfig.pUserData = &audioState;
    if (ma_device_init(NULL, &deviceConfig, &audioState.device) != MA_SUCCESS) {
        printf("Failed to initialize audio device.\n"); ma_decoder_uninit(&audioState.decoder); return false;
    }
    ma_uint64 totalFrames;
    ma_decoder_get_length_in_pcm_frames(&audioState.decoder, &totalFrames);
    audioState.totalSongDurationSec = (totalFrames > 0) ? (float)totalFrames / audioState.decoder.outputSampleRate : 0.0f;
    audioState.isDeviceInitialized = true;
    audioState.isAudioReady = true;
    audioState.currentFilePath = filePath;
    audioState.currentIndex = index;
    audioState.elapsedTimeAtPause = 0.0f;
    if (ma_device_start(&audioState.device) != MA_SUCCESS) {
        printf("Failed to start audio device.\n"); StopAndUnloadSong(audioState); return false;
    }
    audioState.songStartTime = SDL_GetTicks();
    printf("Playing: %s\n", playlist[index].displayName.c_str());
    return true;
}

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
    // [本次修改] 将按钮颜色从淡红色改为天空蓝
    colors[ImGuiCol_Button]               = ImVec4(0.30f, 0.60f, 0.90f, 1.00f);
    colors[ImGuiCol_ButtonHovered]        = ImVec4(0.40f, 0.70f, 1.00f, 1.00f);
    colors[ImGuiCol_ButtonActive]         = ImVec4(0.25f, 0.55f, 0.85f, 1.00f);
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
                    newSong.album = "未知专辑";
                } else {
                    newSong.displayName = filename;
                    newSong.artist = "未知艺术家";
                    newSong.album = "未知专辑";
                }
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

// ======== 主程序 ========
int main(int, char**) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO) != 0) {
        printf("Error: %s\n", SDL_GetError()); return -1;
    }
    const char* glsl_version = "#version 130";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Window* window = SDL_CreateWindow("音乐播放器 v3.4", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, window_flags);
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1);
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
    AudioState audioState;
    if (ma_mutex_init(&audioState.mutex) != MA_SUCCESS) return -1;
    float volume_percent = 50.0f;
    std::vector<Song> mainPlaylist;
    std::vector<std::string> musicDirs;
    LoadConfig(musicDirs);
    RebuildPlaylist(mainPlaylist, musicDirs);
    ActiveView currentView = ActiveView::Main;
    float display_progress = 0.0f;
    bool done = false;
    while (!done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT || (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID(window))) done = true;
        }
        bool isPlaying = audioState.isDeviceInitialized && ma_device_is_started(&audioState.device);
        if (audioState.isAudioReady) {
            float totalElapsedTime = isPlaying ? audioState.elapsedTimeAtPause + (float)(SDL_GetTicks() - audioState.songStartTime) / 1000.0f : audioState.elapsedTimeAtPause;
            display_progress = (audioState.totalSongDurationSec > 0) ? (totalElapsedTime / audioState.totalSongDurationSec) : 0.0f;
            if (isPlaying && display_progress >= 1.0f) {
                if (audioState.playMode == PlayMode::RepeatOne) {
                    PlaySongAtIndex(audioState, mainPlaylist, audioState.currentIndex, PlayDirection::New);
                } else {
                    int nextIndex = GetNextSongIndex(audioState, mainPlaylist.size());
                    if (nextIndex != -1) PlaySongAtIndex(audioState, mainPlaylist, nextIndex, PlayDirection::New);
                    else StopAndUnloadSong(audioState);
                }
            }
        } else {
            display_progress = 0.0f;
        }
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        if (audioState.isDeviceInitialized) ma_device_set_master_volume(&audioState.device, volume_percent / 100.0f);
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const float playerHeight = 120.0f;
        const float leftSidebarWidth = 220.0f;
        ShowLeftSidebar(viewport->Pos, ImVec2(leftSidebarWidth, viewport->Size.y - playerHeight), currentView);
        ShowPlaylistWindow(audioState, mainPlaylist, musicDirs, currentView, ImVec2(viewport->Pos.x + leftSidebarWidth, viewport->Pos.y), ImVec2(viewport->Size.x - leftSidebarWidth, viewport->Size.y - playerHeight));
        ShowPlayerWindow(audioState, mainPlaylist, mainPlaylist, volume_percent, display_progress, ImVec2(viewport->Pos.x, viewport->Pos.y + viewport->Size.y - playerHeight), ImVec2(viewport->Size.x, playerHeight));
        ImGui::Render();
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        glClearColor(0.95f, 0.95f, 0.95f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }
    StopAndUnloadSong(audioState);
    ma_mutex_uninit(&audioState.mutex);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

void ShowLeftSidebar(ImVec2 pos, ImVec2 size, ActiveView& currentView) {
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::Begin("Sidebar", nullptr, flags);
    ImGui::Text(" ");
    ImGui::Text(u8" 小李的音乐盒");
    ImGui::Separator();
    if (ImGui::Selectable(u8"   列表", currentView == ActiveView::Main)) {
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
    ImGui::Text(u8"音乐文件夹路径");
    ImGui::Separator();
    static char add_dir_buf[1024] = "";
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - 100);
    ImGui::InputTextWithHint("##AddPath", u8"在此输入新文件夹路径...", add_dir_buf, sizeof(add_dir_buf));
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button(u8"扫描音乐", ImVec2(90, 0))) {
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
        ImGui::TextDisabled(u8"暂无文件夹，请在上方添加...");
    } else {
        for (const auto& dir : musicDirs) ImGui::TextUnformatted(dir.c_str());
    }
    ImGui::EndChild();
    ImGui::EndChild();
    ImGui::Text(u8"音乐列表 (%zu首)", mainPlaylist.size());
    ImGui::Separator();
    if (ImGui::BeginTable("playlist_table", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 40.0f);
        ImGui::TableSetupColumn(u8"歌曲", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(u8"歌手", ImGuiTableColumnFlags_WidthStretch);
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
            ImGui::Text(u8"未加载歌曲");
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
            bool isPlaying = audioState.isDeviceInitialized && ma_device_is_started(&audioState.device);
            if (ImGui::ArrowButton("##prev", ImGuiDir_Left)) {
                if (!audioState.playHistory.empty()) {
                    PlaySongAtIndex(audioState, activePlaylist, audioState.playHistory.back(), PlayDirection::Back);
                    audioState.playHistory.pop_back();
                } else if (!activePlaylist.empty()) {
                    PlaySongAtIndex(audioState, activePlaylist, (audioState.currentIndex - 1 + activePlaylist.size()) % activePlaylist.size(), PlayDirection::New);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button(isPlaying ? u8"暂停" : u8"播放", ImVec2(80.f, 0))) {
                if (isPlaying) {
                    ma_device_stop(&audioState.device);
                    audioState.elapsedTimeAtPause += (float)(SDL_GetTicks() - audioState.songStartTime) / 1000.0f;
                } else if (audioState.isDeviceInitialized) {
                    ma_device_start(&audioState.device);
                    audioState.songStartTime = SDL_GetTicks();
                }
            }
            ImGui::SameLine();
            if (ImGui::ArrowButton("##next", ImGuiDir_Right)) {
                if (!activePlaylist.empty()) PlaySongAtIndex(audioState, activePlaylist, GetNextSongIndex(audioState, activePlaylist.size()), PlayDirection::New);
            }
            char time_str_elapsed[64];
            sprintf(time_str_elapsed, "%02d:%02d", (int)(progress * audioState.totalSongDurationSec) / 60, (int)(progress * audioState.totalSongDurationSec) % 60);
            ImVec4 filled_color = ImGui::GetStyle().Colors[ImGuiCol_Button];
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
        }
        ImGui::EndDisabled();

        ImGui::TableSetColumnIndex(2);
        const char* modeText = "";
        switch (audioState.playMode) {
            case PlayMode::ListLoop: modeText = u8"顺序"; break;
            case PlayMode::RepeatOne: modeText = u8"单曲"; break;
            case PlayMode::Shuffle: modeText = u8"随机"; break;
        }
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (size.y - ImGui::GetFrameHeight()) * 0.5f);
        if (ImGui::Button(modeText)) {
            audioState.playMode = (PlayMode)(((int)audioState.playMode + 1) % 3);
            audioState.playHistory.clear();
        }
        ImGui::SameLine();
        ImVec4 filled_color = ImGui::GetStyle().Colors[ImGuiCol_Button];
        ImGui::PushItemWidth(150);
        FilledSlider("##Volume", &volume_percent, 0.0f, 100.0f, filled_color, u8"音量 %.0f%%");
        ImGui::PopItemWidth();

        ImGui::EndTable();
    }
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}