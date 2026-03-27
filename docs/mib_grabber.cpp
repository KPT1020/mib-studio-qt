#include <iostream>
#include <filesystem>
#include <EGrabber.h>
#include <opencv2/opencv.hpp>
#include <FormatConverter.h>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <conio.h>
#include <chrono>
#include <iomanip>
#include <CircularBuffer/CircularBuffer.h>
#include <tuple>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <image_processing/image_processing.h>
#include <menu_system/menu_system.h>
#include <nlohmann/json.hpp>

// Suppress warning about illegal character in XMT_DLL_SER.h
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4828)
#endif

#include "XMT_DLL_SER.h"

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#define M_PI 3.14159265358979323846 // pi

cv::Mat backgroundFrame;

using namespace Euresys;

static int lastUsedCameraIndex = -1;

int selectCamera()
{
    try
    {
        EGenTL genTL;
        EGrabberDiscovery discovery(genTL);
        std::cout << "Scanning for available eGrabbers and cameras..." << std::endl;
        discovery.discover();

        // Display available cameras
        if (discovery.cameraCount() == 0)
        {
            throw std::runtime_error("No cameras detected in the system");
        }

        std::cout << "\nAvailable cameras:" << std::endl;
        for (int i = 0; i < discovery.cameraCount(); ++i)
        {
            EGrabberCameraInfo info = discovery.cameras(i);
            std::cout << i << ": " << info.grabbers[0].deviceModelName << std::endl;
        }

        // Let user select camera
        std::cout << "\nSelect camera (0-" << discovery.cameraCount() - 1 << "): ";
        int selectedCamera;
        std::cin >> selectedCamera;
        std::cin.clear();
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

        if (selectedCamera < 0 || selectedCamera >= discovery.cameraCount())
        {
            throw std::runtime_error("Invalid camera selection");
        }

        return selectedCamera;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return -1;
    }
}

void configure_js(std::string config_path)
{
    try
    {
        // Verify file extension
        if (config_path.substr(config_path.size() - 3) != ".js")
        {
            throw std::runtime_error("Config path must end with .js");
        }

        // Always select a camera for configuration
        int selectedCamera = selectCamera();
        if (selectedCamera < 0)
        {
            // Selection failed or was canceled
            return;
        }

        // Update the last used camera index for other functions
        lastUsedCameraIndex = selectedCamera;

        // Initialize grabber with the selected camera
        EGenTL gentl;
        EGrabberDiscovery discovery(gentl);
        discovery.discover();
        Euresys::EGrabber<> grabber(discovery.cameras(selectedCamera));

        // Run the configuration script
        grabber.runScript(config_path);
        std::cout << "Config script executed successfully on camera " << selectedCamera << std::endl;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Configuration error: " << e.what() << std::endl;
    }
}

ImageParams initializeGrabber(EGrabber<CallbackOnDemand> &grabber)
{
    grabber.reallocBuffers(3);
    grabber.start(1);
    ScopedBuffer firstBuffer(grabber);

    ImageParams params;
    params.width = firstBuffer.getInfo<size_t>(gc::BUFFER_INFO_WIDTH);
    params.height = firstBuffer.getInfo<size_t>(gc::BUFFER_INFO_HEIGHT);
    params.pixelFormat = firstBuffer.getInfo<uint64_t>(gc::BUFFER_INFO_PIXELFORMAT);
    params.imageSize = firstBuffer.getInfo<size_t>(gc::BUFFER_INFO_SIZE);
    // Read target FPS from config.json
    json config = readConfig("config.json");
    const int cameraTargetFPS = config.value("cameraTargetFPS", 5000); // Default to 5000 if not specified
    params.bufferCount = cameraTargetFPS;                              // You can adjust this as needed

    grabber.stop();
    return params;
}

void initializeBackgroundFrame(SharedResources &shared, const ImageParams &params)
{
    std::lock_guard<std::mutex> lock(shared.backgroundFrameMutex);
    shared.backgroundFrame = cv::Mat(static_cast<int>(params.height), static_cast<int>(params.width), CV_8UC1, cv::Scalar(255));
    cv::GaussianBlur(shared.backgroundFrame, shared.blurredBackground, cv::Size(3, 3), 0);
}

void triggerOut(EGrabber<CallbackOnDemand> &grabber, SharedResources &shared)
{
    // grabber.setString<InterfaceModule>("LineSelector", "TTLIO12");
    // grabber.setString<InterfaceModule>("LineMode", "Output");
    grabber.setString<InterfaceModule>("LineSource", shared.manualTriggerEnabled.load() ? "High" : "Low");
}

void triggerThread(EGrabber<CallbackOnDemand> &grabber, SharedResources &shared)
{
    // Event-driven manual trigger: follow manualTriggerEnabled changes immediately
    while (!shared.done)
    {
        {
            std::unique_lock<std::mutex> lk(shared.manualTriggerMutex);
            shared.manualTriggerCondition.wait(lk, [&]
                                               { return shared.done || shared.manualTriggerEnabled.load(); });
        }
        if (shared.done)
            break;
        triggerOut(grabber, shared);
    }

    // Signal that this thread is ready to be joined
    {
        std::lock_guard<std::mutex> lock(shared.threadShutdownMutex);
        shared.threadsReadyToJoin.fetch_add(1, std::memory_order_release);
        shared.threadShutdownCondition.notify_one();
    }

    std::cout << "Trigger thread interrupted." << std::endl;
}

void processTrigger(EGrabber<CallbackOnDemand> &grabber, SharedResources &shared)
{
    if (shared.processTrigger && shared.validProcessingFrame)
    {
        // track how long it takes to set the line source high
        auto trigger_start = std::chrono::high_resolution_clock::now();
        // grabber.setString<InterfaceModule>("LineSelector", "TTLIO12");
        // grabber.setString<InterfaceModule>("LineMode", "Output");
        if (shared.done)
            return;
        grabber.setString<InterfaceModule>("LineSource", "High");
        auto trigger_end = std::chrono::high_resolution_clock::now();
        auto trigger_onset_duration = std::chrono::duration_cast<std::chrono::microseconds>(trigger_end - trigger_start);

        // Store the trigger onset duration in shared resources for dashboard display
        shared.triggerOnsetDuration.store(trigger_onset_duration.count());

        // Busy-wait loop for approximately 1 microsecond
        auto start = std::chrono::high_resolution_clock::now();
        while (!shared.done && std::chrono::high_resolution_clock::now() - start < std::chrono::microseconds(1))
        {
            // Busy-wait
        }
        if (!shared.done)
        {
            grabber.setString<InterfaceModule>("LineSource", "Low");
        }
        shared.processTrigger = false;
    }
}

void processTriggerThread(EGrabber<CallbackOnDemand> &grabber, SharedResources &shared)
{
    grabber.setString<InterfaceModule>("LineSelector", "TTLIO12");
    grabber.setString<InterfaceModule>("LineMode", "Output");
    while (!shared.done)
    {
        {
            std::unique_lock<std::mutex> lk(shared.triggerMutex);
            shared.triggerCondition.wait(lk, [&]
                                         { return shared.done || shared.processTrigger.load(); });
        }
        if (shared.done)
            break;
        processTrigger(grabber, shared);
    }

    // Signal that this thread is ready to be joined
    {
        std::lock_guard<std::mutex> lock(shared.threadShutdownMutex);
        shared.threadsReadyToJoin.fetch_add(1, std::memory_order_release);
        shared.threadShutdownCondition.notify_one();
    }

    std::cout << "Process trigger thread interrupted." << std::endl;
}

void hybrid_sample(EGrabber<CallbackOnDemand> &grabber, const ImageParams &params, CircularBuffer &cameraBuffer, CircularBuffer &circularBuffer, CircularBuffer &processingBuffer, SharedResources &shared)
{
    commonSampleLogic(shared, "default_save_directory", [&](SharedResources &shared, const std::string &saveDir)
                      {
                          std::vector<std::thread> threads;
                          setupCommonThreads(shared, saveDir, circularBuffer, processingBuffer, params, threads);
                          threads.emplace_back(simulateCameraThread, std::ref(cameraBuffer), std::ref(shared), std::ref(params));
                          threads.emplace_back(triggerThread, std::ref(grabber), std::ref(shared));
                          threads.emplace_back(processTriggerThread, std::ref(grabber), std::ref(shared));

                          grabber.start();
                          size_t lastProcessedFrame = 0;
                          while (!shared.done)
                          {
                              if (shared.paused)
                              {
                                  std::this_thread::sleep_for(std::chrono::milliseconds(1));
                                  continue;
                              }
                              size_t latestFrame = shared.latestCameraFrame.load(std::memory_order_acquire);
                              if (latestFrame != lastProcessedFrame)
                              {
                                  const uint8_t *imageData = cameraBuffer.getPointer(latestFrame);
                                  if (imageData != nullptr)
                                  {
                                      circularBuffer.push(imageData);
                                      processingBuffer.push(imageData);
                                      {
                                          std::lock_guard<std::mutex> displayLock(shared.displayQueueMutex);
                                          std::lock_guard<std::mutex> processingLock(shared.processingQueueMutex);
                                          shared.framesToProcess.push(latestFrame);
                                          shared.framesToDisplay.push(latestFrame);
                                      }
                                      shared.displayQueueCondition.notify_one();
                                      shared.processingQueueCondition.notify_one();
                                      lastProcessedFrame = latestFrame;
                                  }
                              }
                          }
                          grabber.stop();
                          return threads; });
}

void temp_sample(EGrabber<CallbackOnDemand> &grabber, const ImageParams &params, CircularBuffer &circularBuffer, CircularBuffer &processingBuffer, SharedResources &shared)
{
    commonSampleLogic(shared, "default_save_directory", [&](SharedResources &shared, const std::string &saveDir)
                      {
                          std::vector<std::thread> threads;
                          setupCommonThreads(shared, saveDir, circularBuffer, processingBuffer, params, threads);
                          
                          threads.emplace_back(triggerThread, std::ref(grabber), std::ref(shared)); // previous testing trigger 
                          threads.emplace_back(processTriggerThread, std::ref(grabber), std::ref(shared));

                          grabber.start();
                          // egrabber request fps and exposure time load to shared.resources for metric display
                          uint64_t fr = grabber.getInteger<StreamModule>("StatisticsFrameRate");
                          uint64_t dr = grabber.getInteger<StreamModule>("StatisticsDataRate");
                          // Get exposure time
                          uint64_t exposureTime = grabber.getInteger<RemoteModule>("ExposureTime");
                          shared.currentFPS = static_cast<double>(fr);
                          shared.dataRate = static_cast<double>(dr);
                          shared.exposureTime = exposureTime;
                          size_t frameCount = 0;
                          uint64_t lastFrameId = 0;
                          uint64_t duplicateCount = 0;
                          while (!shared.done)
                          {
                              if (shared.paused)
                              {
                                  std::this_thread::sleep_for(std::chrono::milliseconds(1));
                                  cv::waitKey(1);
                                  continue;
                              }

                              // Update FPS and other metrics periodically (every 100 frames or so)
                              if (frameCount % 100 == 0)
                              {
                                  uint64_t fr = grabber.getInteger<StreamModule>("StatisticsFrameRate");
                                  uint64_t dr = grabber.getInteger<StreamModule>("StatisticsDataRate");
                                  uint64_t exposureTime = grabber.getInteger<RemoteModule>("ExposureTime");
                                  shared.currentFPS = static_cast<double>(fr);
                                  shared.dataRate = static_cast<double>(dr);
                                  shared.exposureTime = exposureTime;
                                  shared.updated = true;
                              }

                              ScopedBuffer buffer(grabber);
                              uint8_t *imagePointer = buffer.getInfo<uint8_t *>(gc::BUFFER_INFO_BASE);
                              uint64_t frameId = buffer.getInfo<uint64_t>(gc::BUFFER_INFO_FRAMEID);
                              uint64_t timestamp = buffer.getInfo<uint64_t>(gc::BUFFER_INFO_TIMESTAMP);
                              bool isIncomplete = buffer.getInfo<bool>(gc::BUFFER_INFO_IS_INCOMPLETE);
                              size_t sizeFilled = buffer.getInfo<size_t>(gc::BUFFER_INFO_SIZE_FILLED);

                              if (!isIncomplete)
                              {
                                  if (frameId <= lastFrameId)
                                  {
                                      ++duplicateCount;
                                      //   std::cout << "Duplicate frame detected: FrameID=" << frameId << ", Timestamp=" << timestamp << std::endl;
                                  }
                                  else
                                  {
                                      circularBuffer.push(imagePointer);
                                      processingBuffer.push(imagePointer);
                                      {
                                          std::lock_guard<std::mutex> displayLock(shared.displayQueueMutex);
                                          std::lock_guard<std::mutex> processingLock(shared.processingQueueMutex);
                                          shared.framesToProcess.push(frameCount);
                                          shared.framesToDisplay.push(frameCount);
                                      }
                                      shared.displayQueueCondition.notify_one();
                                      shared.processingQueueCondition.notify_one();
                                      frameCount++;
                                  }
                                  lastFrameId = frameId;
                              }
                              else
                              {
                                  //   std::cout << "Incomplete frame received: FrameID=" << frameId << std::endl;
                              }
                          }

                          grabber.stop();

                          return threads; });
}

void runHybridSample()
{
    try
    {
        int selectedCamera = selectCamera();
        if (selectedCamera < 0)
        {
            return;
        }

        lastUsedCameraIndex = selectedCamera;

        // Initialize grabber with selected camera
        EGenTL genTL;
        EGrabberDiscovery discovery(genTL);
        discovery.discover();
        EGrabber<CallbackOnDemand> grabber(discovery.cameras(selectedCamera));

        ImageParams cameraParams = initializeGrabber(grabber);

        std::cout << "Select the image directory:\n";
        std::string imageDirectory = MenuSystem::navigateAndSelectFolder();
        ImageParams params = initializeImageParams(imageDirectory);
        CircularBuffer cameraBuffer(params.bufferCount, params.imageSize);
        CircularBuffer circularBuffer(params.bufferCount, params.imageSize);
        CircularBuffer processingBuffer(params.bufferCount, params.imageSize);
        loadImages(imageDirectory, cameraBuffer, true);

        SharedResources shared;
        initializeMockBackgroundFrame(shared, params, cameraBuffer);
        shared.roi = cv::Rect(0, 0, static_cast<int>(params.width), static_cast<int>(params.height));

        hybrid_sample(grabber, params, cameraBuffer, circularBuffer, processingBuffer, shared);

        std::cout << "Hybrid sampling completed.\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
    }
}

int mib_grabber_main()
{
    try
    {
        int selectedCamera = selectCamera();
        if (selectedCamera < 0)
        {
            return 1;
        }

        lastUsedCameraIndex = selectedCamera;

        // Initialize grabber with selected camera
        EGenTL genTL;
        EGrabberDiscovery discovery(genTL);
        discovery.discover();
        EGrabber<CallbackOnDemand> grabber(discovery.cameras(selectedCamera));

        // Continue with your existing initialization and grabbing logic
        ImageParams params = initializeGrabber(grabber);
        CircularBuffer circularBuffer(params.bufferCount, params.imageSize);
        CircularBuffer processingBuffer(params.bufferCount, params.imageSize);
        SharedResources shared;
        initializeBackgroundFrame(shared, params);
        shared.roi = cv::Rect(0, 0, static_cast<int>(params.width), static_cast<int>(params.height));

        temp_sample(grabber, params, circularBuffer, processingBuffer, shared);
    }
    catch (const std::exception &e)
    {
        std::cout << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
