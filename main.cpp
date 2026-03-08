#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <thread>
#include <iomanip>
#include <algorithm>
#include <sstream>
#include <chrono>
#include "whisper.h"

namespace fs = std::filesystem;

std::string to_timestamp(int64_t t) {
    int64_t sec = t / 100;
    int64_t msec = (t % 100) * 10;
    int64_t min = sec / 60;
    sec %= 60;
    int64_t hour = min / 60;
    min %= 60;
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(2) << hour << ":"
        << std::setfill('0') << std::setw(2) << min << ":"
        << std::setfill('0') << std::setw(2) << sec << ","
        << std::setfill('0') << std::setw(3) << msec;
    return oss.str();
}

void progress_callback(struct whisper_context * ctx, struct whisper_state * state, int progress, void * user_data) {
    int barWidth = 40;
    std::cout << "\rProgress: [";
    int pos = barWidth * progress / 100;
    for (int i = 0; i < barWidth; ++i) {
        if (i < pos) std::cout << "=";
        else if (i == pos) std::cout << ">";
        else std::cout << " ";
    }
    std::cout << "] " << progress << "%" << std::flush;
}

bool load_wav_simple(const std::string & fname, std::vector<float> & pcmf32) {
    std::ifstream file(fname, std::ios::binary);
    if (!file.is_open()) return false;
    file.seekg(44); 
    int16_t sample;
    while (file.read(reinterpret_cast<char*>(&sample), sizeof(sample))) {
        pcmf32.push_back(static_cast<float>(sample) / 32768.0f);
    }
    return true;
}

void process_file(const fs::path& input_path, const fs::path& output_path, whisper_context* ctx, const std::string& lang, bool use_gpu, bool make_srt) {

    if (fs::exists(output_path)) {
        std::cout << "Skipping: " << input_path.filename().string() << " Already transcribed" << std::endl;
        return;
    }
    std::cout << "\n----------------------------------------" << std::endl;
    std::cout << "Processing: " << input_path.filename().string() << std::endl;

    std::string temp_wav = (input_path.parent_path() / (input_path.stem().string() + "_temp.wav")).string();
    
    std::string ffmpeg_cmd = "ffmpeg -y -i \"" + input_path.string() + "\" -ar 16000 -ac 1 -c:a pcm_s16le \"" + temp_wav + "\" -loglevel quiet";
    if (std::system(ffmpeg_cmd.c_str()) != 0) {
        std::cerr << "FFmpeg failed for " << input_path << std::endl;
        return;
    }

    std::vector<float> pcmf32;
    if (!load_wav_simple(temp_wav, pcmf32)) return;

    whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    wparams.language = lang.c_str();
    wparams.n_threads = use_gpu ? 1 : std::thread::hardware_concurrency();
    wparams.progress_callback = progress_callback;

    if (whisper_full(ctx, wparams, pcmf32.data(), pcmf32.size()) == 0) {
        std::ofstream out_file(output_path);
        int n_segments = whisper_full_n_segments(ctx);
        for (int i = 0; i < n_segments; ++i) {
            const char* text = whisper_full_get_segment_text(ctx, i);
            if (make_srt) {
                out_file << i + 1 << "\n" << to_timestamp(whisper_full_get_segment_t0(ctx, i)) 
                         << " --> " << to_timestamp(whisper_full_get_segment_t1(ctx, i)) 
                         << "\n" << text << "\n\n";
            } else {
                out_file << text << " ";
            }
        }
        std::cout << "\nSaved to: " << output_path << std::endl;
    }

    fs::remove(temp_wav);
}


int main(int argc, char** argv) {
    if (argc < 3) {
        std::cout << "Usage: WhisperTool <source_dir> <target_dir> [model_path] [--gpu] [--srt] [--lang en|cs|auto]" << std::endl;
        return 1;
    }

    fs::path source_dir = argv[1];
    fs::path target_dir = argv[2];
    std::string model_path = "models/ggml-base.bin";
    std::string lang = "auto";
    bool use_gpu = false;
    bool make_srt = false;

    for (int i = 3; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--gpu") use_gpu = true;
        else if (arg == "--srt") make_srt = true;
        else if (arg == "--lang" && i + 1 < argc) lang = argv[++i];
        else if (arg.find(".bin") != std::string::npos) model_path = arg;
    }

    if (!fs::exists(model_path)) {
        std::cerr << "Model not found!" << std::endl;
        return 1;
    }

    whisper_context_params cparams = whisper_context_default_params();
    cparams.use_gpu = use_gpu;
    auto ctx = whisper_init_from_file_with_params(model_path.c_str(), cparams);
    if (!ctx) return 1;

    std::vector<std::string> extensions = {".mp3"};

    for (const auto& entry : fs::recursive_directory_iterator(source_dir)) {
        if (entry.is_regular_file()) {
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

            if (std::find(extensions.begin(), extensions.end(), ext) != extensions.end()) {
                fs::path relative = fs::relative(entry.path(), source_dir);
                fs::path final_output_dir = target_dir / relative.parent_path();
                fs::create_directories(final_output_dir);

                fs::path final_output_file = final_output_dir / (entry.path().stem().string() + (make_srt ? ".srt" : ".txt"));

                process_file(entry.path(), final_output_file, ctx, lang, use_gpu, make_srt);
            }
        } else {
            continue;
        }
    }

    whisper_free(ctx);
    std::cout << "\nAll done!" << std::endl;
    return 0;
}
