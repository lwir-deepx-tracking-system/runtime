/**
 * @file run_dir.hpp
 * @brief Structured run-directory utilities for C++ example runners
 *
 * Provides timestamped output directories, run_info.txt metadata,
 * tensor dump, VideoWriter codec fallback, and signal handling.
 *
 * Ported from original yolov5_sync.cpp / yolov5_async.cpp features
 * to the common runner infrastructure.
 */

#ifndef DXAPP_RUN_DIR_HPP
#define DXAPP_RUN_DIR_HPP

#include <atomic>
#if __cplusplus >= 201703L || (defined(_MSVC_LANG) && _MSVC_LANG >= 201703L)
#include <filesystem>
#else
#include <experimental/filesystem>
#endif
#include <memory>
#include <opencv2/videoio.hpp>
#include <string>
#include <vector>

#include <dxrt/dxrt_cxx_api.h>

namespace dxapp {
#if __cplusplus >= 201703L || (defined(_MSVC_LANG) && _MSVC_LANG >= 201703L)
namespace fs = std::filesystem;
#else
namespace fs = std::experimental::filesystem;
#endif

// ---------------------------------------------------------------
// Global signal handler for graceful Ctrl+C shutdown
// ---------------------------------------------------------------

/**
 * @brief Global flag set by SIGINT/SIGTERM handler for graceful shutdown.
 * All runner loops should check this flag in their iteration condition.
 */
std::atomic<bool>& g_interrupted();

/**
 * @brief Signal handler that sets g_interrupted to true.
 * Register with std::signal(SIGINT, signalHandler) in main.
 */
void signalHandler(int /*signum*/);

/**
 * @brief Install SIGINT and SIGTERM signal handlers for graceful shutdown.
 * Should be called at the beginning of run().
 */
void installSignalHandlers();

// ---------------------------------------------------------------
// Run directory creation
// ---------------------------------------------------------------

/**
 * @brief Create a timestamped run directory.
 * @param baseDir   Base directory (e.g., "artifacts/cpp_example")
 * @param scriptTag Short tag for the binary (e.g., "yolov5s_sync")
 * @param runKind   "image", "image-dir", or "stream"
 * @param runName   Source name (e.g., "bus.jpg", "camera0")
 * @return Full path like: {baseDir}/{scriptTag}-{runKind}-{runName}-{YYYYMMDD-HHMMSS}
 */
std::string makeRunDir(const std::string& baseDir,
                              const std::string& scriptTag,
                              const std::string& runKind,
                              const std::string& runName);

// ---------------------------------------------------------------
// Run info metadata
// ---------------------------------------------------------------

/**
 * @brief Write run_info.txt with execution metadata into the given directory.
 */
void writeRunInfo(const std::string& runDir,
                         const std::string& binaryPath,
                         const std::string& modelPath,
                         const std::string& inputSource);

// ---------------------------------------------------------------
// Tensor dump
// ---------------------------------------------------------------

/** Write a single cv::Mat to a binary file (for tensor dump or error diagnostics). */
void writeInputTensor(const std::string& filePath, const cv::Mat& image);

/**
 * @brief Dump input and output tensors as raw binary files.
 * Files: input_tensor.bin, output_tensor_0.bin, ...
 */
void dumpTensorsToFiles(
    const std::string& targetDir,
    const cv::Mat& preprocessedImage,
    const std::vector<std::shared_ptr<dxrt::Tensor>>& outputs);

/**
 * @brief Compute per-frame dump directory path.
 * @param baseDumpDir Base dump directory (e.g., runDir + "/dump_tensors")
 * @param frameIdx    0-based frame index
 * @param perFrame    If true, creates frame000001/ subdirectory
 */
std::string frameDumpDir(const std::string& baseDumpDir,
                                int frameIdx, bool perFrame);

// ---------------------------------------------------------------
// VideoWriter with mp4v → XVID fallback
// ---------------------------------------------------------------

/**
 * @brief Open a VideoWriter trying mp4v first, then XVID/AVI fallback.
 * @param saveDir   Directory to place the output file
 * @param fps       Frames per second
 * @param frameSize Output frame size
 * @param[out] resolvedPath  Absolute path of the successfully opened file
 * @return Opened cv::VideoWriter (check .isOpened())
 */
cv::VideoWriter initVideoWriter(const std::string& saveDir,
                                       double fps,
                                       const cv::Size& frameSize,
                                       std::string& resolvedPath);

// ---------------------------------------------------------------
// Input source string helper
// ---------------------------------------------------------------

/**
 * @brief Build a human-readable input source string for run_info.txt.
 */
std::string buildInputSourceString(const std::string& imagePath,
                                          const std::string& videoFile,
                                          int cameraIndex,
                                          const std::string& rtspUrl);

}  // namespace dxapp

#endif  // DXAPP_RUN_DIR_HPP
