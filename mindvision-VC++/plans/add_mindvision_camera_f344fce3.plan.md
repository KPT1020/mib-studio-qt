---
name: Add MindVision Camera
overview: Add a MindVisionCamera implementation of ICamera alongside the existing EGrabberCamera. Both camera types will be discoverable and selectable from the ConnectTab UI, with the factory pattern routing to the correct implementation.
todos:
  - id: copy-sdk-headers
    content: Copy MindVision SDK headers (CameraApi.h, CameraDefine.H, CameraStatus.h, CameraApiLoad.h) into mib-studio-qt/include/MindVision/
    status: completed
  - id: create-mindvision-camera
    content: Create MindVisionCamera.h and MindVisionCamera.cpp implementing ICamera using MindVision SDK (init, start, stop, grabFrame, pollStats, checkDeviceHealth)
    status: completed
  - id: update-discovered-camera
    content: Add CameraType enum to DiscoveredCamera, add cameraIndex field, move EGrabber.h include to .cpp
    status: completed
  - id: add-mv-discovery
    content: Add discoverMindVisionCameras() to CameraControlService using CameraSdkInit + CameraEnumerateDevice
    status: completed
  - id: update-appbackend
    content: Update AppBackend to support MindVision camera selection alongside eGrabber (new setMindVisionCameraSelection method)
    status: completed
  - id: update-connect-tab
    content: Update ConnectTab to discover and display both eGrabber and MindVision cameras, route selection to correct backend method
    status: completed
  - id: update-device-init
    content: Update DeviceInitManager to discover both camera types for auto-connect
    status: completed
  - id: update-cmake
    content: "Update CMakeLists.txt: add MindVision include dir, add MindVisionCamera.cpp to mib_backend sources"
    status: completed
isProject: false
---

# Add MindVision Camera Implementation

## Architecture

The existing `ICamera` interface and `CameraFactory` pattern make this straightforward. We add a new `MindVisionCamera` class that implements `ICamera`, update discovery to find both camera types, and let the user choose which camera to connect to.

```mermaid
graph TD
    ICamera["ICamera (interface)"]
    EGrabber["EGrabberCamera (existing)"]
    MindVision["MindVisionCamera (new)"]
    MockCam["MockCamera (existing)"]
    CaptureService["CaptureService"]
    Factory["CameraFactory lambda"]

    ICamera --> EGrabber
    ICamera --> MindVision
    ICamera --> MockCam
    Factory --> CaptureService
    EGrabber -.-> Factory
    MindVision -.-> Factory
    MockCam -.-> Factory
```



## Key Files Created/Modified

### Created
- `include/MindVision/` – SDK headers (CameraApi.h, CameraDefine.H, CameraStatus.h, CameraApiLoad.h)
- `include/camera/common/MindVisionCamera.h` – class declaration
- `src/camera/common/MindVisionCamera.cpp` – implementation (defines API_LOAD_MAIN for dynamic DLL loading)

### Modified
- `include/backend/services/CameraControlService.h` – added CameraType enum, cameraIndex field, discoverMindVisionCameras(), discoverAllCameras(), moved EGrabber.h include
- `src/backend/services/CameraControlService.cpp` – added discoverMindVisionCameras() and discoverAllCameras()
- `include/backend/AppBackend.h` – added setMindVisionCameraSelection(), selectedMvCameraIndex_
- `src/backend/AppBackend.cpp` – implemented setMindVisionCameraSelection(), updated isCameraConfigured()
- `src/frontend/tabs/ConnectTab.cpp` – DeviceSelection struct replaces DeviceIdx, populateDevices uses discoverAllCameras(), onConnect routes by type
- `src/frontend/system/DeviceInitManager.cpp` – discovery uses discoverAllCameras(), auto-connect handles MindVision
- `CMakeLists.txt` – added MindVisionCamera.cpp and include/MindVision include dir

## DLL Loading Notes

`CameraApiLoad.h` defines all MindVision function pointers as global `extern` variables, with `LoadSdkApi()` loading them from the installed `MVCAMSDK_X64.dll` via registry lookup. The `API_LOAD_MAIN` define (set only in `MindVisionCamera.cpp`) provides the actual variable definitions; all other TUs get extern declarations.
