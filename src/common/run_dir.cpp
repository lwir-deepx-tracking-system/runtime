#include "common/run_dir.hpp"

#include <csignal>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iostream>

namespace dxapp {

std::atomic<bool>& g_interrupted() {
    static std::atomic<bool> flag{false};
    return flag;
}

void signalHandler(int /*signum*/) {
    g_interrupted().store(true);
}

void installSignalHandlers() {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
}

std::string makeRunDir(const std::string& baseDir,
                              const std::string& scriptTag,
                              const std::string& runKind,
                              const std::string& runName) {
    std::time_t now = std::time(nullptr);
    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &now);
#else
    localtime_r(&now, &tm_buf);
#endif
    std::string timestamp(20, '\0');
    auto len = std::strftime(&timestamp[0], timestamp.size(), "%Y%m%d-%H%M%S", &tm_buf);
    timestamp.resize(len);
    return baseDir + "/" + scriptTag + "-" + runKind + "-" + runName + "-" + timestamp;
}

void writeRunInfo(const std::string& runDir,
                         const std::string& binaryPath,
                         const std::string& modelPath,
                         const std::string& inputSource) {
    std::ofstream ofs(runDir + "/run_info.txt");
    if (!ofs) return;
    ofs << "script: " << binaryPath << "\n";
    ofs << "model: " << modelPath << "\n";
    ofs << "input: " << inputSource << "\n";
}

void writeInputTensor(const std::string& filePath, const cv::Mat& image) {
    std::ofstream ofs(filePath, std::ios::binary);
    if (ofs && !image.empty()) {
        size_t byte_size = image.total() * image.elemSize();
        ofs.write(static_cast<const char*>(static_cast<const void*>(image.data)),
                  static_cast<std::streamsize>(byte_size));
    }
}

void dumpTensorsToFiles(
    const std::string& targetDir,
    const cv::Mat& preprocessedImage,
    const std::vector<std::shared_ptr<dxrt::Tensor>>& outputs) {

    fs::create_directories(targetDir);

    // Input tensor
    writeInputTensor(targetDir + "/input_tensor.bin", preprocessedImage);

    // Output tensors
    for (size_t i = 0; i < outputs.size(); ++i) {
        const auto& shape = outputs[i]->shape();
        size_t num_elements = 1;
        for (auto s : shape) num_elements *= static_cast<size_t>(s);
        size_t byte_size = num_elements * sizeof(float);
        std::ofstream ofs(targetDir + "/output_tensor_" + std::to_string(i) + ".bin",
                          std::ios::binary);
        if (ofs) {
            ofs.write(static_cast<const char*>(static_cast<const void*>(outputs[i]->data())),
                      static_cast<std::streamsize>(byte_size));
        }
    }
}

std::string frameDumpDir(const std::string& baseDumpDir,
                                int frameIdx, bool perFrame) {
    if (!perFrame) return baseDumpDir;
    std::string buf(16, '\0');
    auto len = std::snprintf(&buf[0], buf.size(), "frame%06d", frameIdx);
    buf.resize(static_cast<size_t>(len));
    return baseDumpDir + "/" + buf;
}

cv::VideoWriter initVideoWriter(const std::string& saveDir,
                                       double fps,
                                       const cv::Size& frameSize,
                                       std::string& resolvedPath) {
    fs::create_directories(saveDir);
    double useFps = (fps > 0) ? fps : 30.0;

    std::string mp4Path = saveDir + "/output.mp4";
    cv::VideoWriter writer;

    // Try H.264 (avc1) first — universally playable on all modern players/OS
    writer.open(mp4Path, cv::VideoWriter::fourcc('a', 'v', 'c', '1'),
                useFps, frameSize);
    if (writer.isOpened()) {
        resolvedPath = fs::absolute(mp4Path).string();
        std::cout << "[DXAPP] [INFO] Saving output video: " << resolvedPath << std::endl;
        return writer;
    }
    writer.release();

    // Fallback: mp4v (MPEG-4 Part 2) — requires codec pack on some systems
    std::cerr << "[DXAPP] [WARN] avc1 codec failed, retrying with mp4v..." << std::endl;
    writer.open(mp4Path, cv::VideoWriter::fourcc('m', 'p', '4', 'v'),
                useFps, frameSize);
    if (writer.isOpened()) {
        resolvedPath = fs::absolute(mp4Path).string();
        std::cout << "[DXAPP] [INFO] Saving output video: " << resolvedPath
                  << " (mp4v fallback)" << std::endl;
        return writer;
    }
    writer.release();

    // Fallback: XVID/AVI
    std::cerr << "[DXAPP] [WARN] mp4v codec failed, retrying with XVID/AVI..." << std::endl;
    std::string aviPath = saveDir + "/output.avi";
    writer.open(aviPath, cv::VideoWriter::fourcc('X', 'V', 'I', 'D'),
                useFps, frameSize);
    if (writer.isOpened()) {
        resolvedPath = fs::absolute(aviPath).string();
        std::cout << "[DXAPP] [INFO] Saving output video: " << resolvedPath
                  << " (XVID fallback)" << std::endl;
        return writer;
    }
    writer.release();

    std::cerr << "[DXAPP] [ERROR] Failed to open VideoWriter for output." << std::endl;
    resolvedPath.clear();
    return writer;
}

std::string buildInputSourceString(const std::string& imagePath,
                                          const std::string& videoFile,
                                          int cameraIndex,
                                          const std::string& rtspUrl) {
    if (!imagePath.empty()) return imagePath;
    if (cameraIndex >= 0) return "camera:" + std::to_string(cameraIndex);
    if (!rtspUrl.empty()) return rtspUrl;
    return videoFile;
}

}  // namespace dxapp
