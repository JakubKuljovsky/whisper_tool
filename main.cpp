#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <thread>
#include "whisper.h"

namespace fs = std::filesystem;

// Simple WAV loader (PCM 16-bit)
bool load_wav_simple(const std::string & fname, std::vector<float> & pcmf32) {
    std::ifstream file(fname, std::ios::binary);
    if (!file.is_open()) return false;
    file.seekg(44); // Skip WAV header
    int16_t sample;
    while (file.read(reinterpret_cast<char*>(&sample), sizeof(sample))) {
        pcmf32.push_back(static_cast<float>(sample) / 32768.0f);
    }
    return true;
}

int main(int argc, char** argv) {
    std::cout << "--- WhisperTool Diagnostic Start ---" << std::endl;

    if (argc < 2) {
        std::cout << "Usage: ./WhisperTool <video_file> [model_path]" << std::endl;
        return 1;
    }

    std::string input_video = argv[1];
    std::string model_path;

    // --- 1. MODEL SELECTION ---
    if (argc >= 3) {
        model_path = argv[2];
    } else {
        std::cout << "Enter path to the model file (e.g., models/ggml-base.bin): ";
        std::getline(std::cin, model_path);
    }

    if (!fs::exists(model_path)) {
        std::cerr << "Error: Model file not found at " << model_path << std::endl;
        return 1;
    }

    // --- 2. LANGUAGE SELECTION ---
    std::string lang;
    std::cout << "========================================" << std::endl;
    std::cout << "   TRANSCRIPTION SETTINGS" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Enter language code (e.g., 'en', 'cs', 'de', or 'auto'): ";
    std::cin >> lang;

    // --- 3. THREAD DETECTION ---
    unsigned int n_threads = std::thread::hardware_concurrency();
    if (n_threads == 0) n_threads = 4; // Failsafe
    std::cout << "Detected " << n_threads << " CPU cores. All will be utilized." << std::endl;

    std::string base_name = fs::path(input_video).stem().string();
    std::string temp_wav = base_name + "_temp.wav";
    std::string output_txt = base_name + ".txt";

    // --- 4. AUDIO EXTRACTION ---
    std::cout << "\n>>> Extracting audio via FFmpeg..." << std::endl;
    std::string ffmpeg_cmd = "ffmpeg -y -i \"" + input_video + "\" -ar 16000 -ac 1 -c:a pcm_s16le " + temp_wav + " -loglevel quiet";
    if (std::system(ffmpeg_cmd.c_str()) != 0) {
        std::cerr << "Error: FFmpeg execution failed!" << std::endl;
        return 1;
    }

    // --- 5. WHISPER INITIALIZATION ---
    whisper_context_params cparams = whisper_context_default_params();
    auto ctx = whisper_init_from_file_with_params(model_path.c_str(), cparams);
    if (!ctx) {
        std::cerr << "Error: Failed to initialize Whisper context with model: " << model_path << std::endl;
        return 1;
    }

    std::vector<float> pcmf32;
    if (!load_wav_simple(temp_wav, pcmf32)) {
        std::cerr << "Error: Failed to load temporary WAV file!" << std::endl;
        return 1;
    }

    // --- 6. WHISPER CONFIGURATION ---
    whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    wparams.language = lang.c_str();
    wparams.n_threads = n_threads;
    wparams.print_progress = true;

    std::cout << ">>> Processing transcription (Threads: " << n_threads << ", Language: " << lang << ")..." << std::endl;

    if (whisper_full(ctx, wparams, pcmf32.data(), pcmf32.size()) != 0) {
        std::cerr << "Error: Transcription failed!" << std::endl;
        return 1;
    }

    // --- 7. OUTPUT WRITING ---
    std::ofstream out_file(output_txt);
    if (!out_file.is_open()) {
        std::cerr << "Error: Could not create output file!" << std::endl;
        return 1;
    }

    int n_segments = whisper_full_n_segments(ctx);
    for (int i = 0; i < n_segments; ++i) {
        out_file << whisper_full_get_segment_text(ctx, i) << " ";
    }

    std::cout << "\n>>> Success! Transcription saved to: " << output_txt << std::endl;

    out_file.close();
    whisper_free(ctx);
    fs::remove(temp_wav); // Cleanup

    return 0;
}
