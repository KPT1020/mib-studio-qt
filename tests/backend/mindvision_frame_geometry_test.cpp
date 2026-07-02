// mindvision_frame_geometry_test
//
// Guards backend::camera::mindvision::frameFitsInBuffer, the bounds check
// MindVisionCamera::grabFrame() now runs before CameraImageProcess() writes a
// per-frame tSdkFrameHead::iWidth/iHeight-sized image into outBuffer_ (fixed
// capacity, allocated once at start() from a separate CameraGetImageResolution
// query). Before this check existed, a frame report larger than the buffer
// allocated at start() -- the same untrusted-delivery mismatch
// EGrabberCamera::replenishPendingFrames guards against for its own
// buffer/geometry pair -- would overflow outBuffer_ with no downstream catch
// (data.size() == width*height stays internally consistent, so
// ProcessingService's own frame-buffer checks would not reject it).

#include "backend/camera/mindvision/MindVisionFrameGeometry.h"

#include "support/assert.h"

namespace mv = backend::camera::mindvision;

int main()
{
    // 1) Exact fit is accepted.
    MIB_EXPECT(mv::frameFitsInBuffer(640, 480, 640, 480), "exact match fits");

    // 2) A frame smaller than the buffer (e.g. a narrower ROI) is accepted.
    MIB_EXPECT(mv::frameFitsInBuffer(320, 240, 640, 480), "smaller frame fits");

    // 3) The critical fix: a frame report larger than the buffer allocated at
    //    start() must be rejected instead of overflowing outBuffer_.
    MIB_EXPECT(!mv::frameFitsInBuffer(1280, 480, 640, 480), "wider-than-buffer frame rejected");
    MIB_EXPECT(!mv::frameFitsInBuffer(640, 960, 640, 480), "taller-than-buffer frame rejected");
    MIB_EXPECT(!mv::frameFitsInBuffer(2000, 2000, 640, 480), "grossly oversized frame rejected");

    // 4) Same total byte count via a different width/height split still must
    //    not exceed the buffer's own w*h; a 800x384 frame (307200 bytes) does
    //    not fit a 640x480 buffer (307200 bytes) shape-for-shape, but the
    //    check is byte-count based like the allocation, so equal totals pass.
    MIB_EXPECT(mv::frameFitsInBuffer(800, 384, 640, 480), "equal byte count via different shape fits");

    // 5) Non-positive dimensions are always rejected, on either side.
    MIB_EXPECT(!mv::frameFitsInBuffer(0, 480, 640, 480), "zero frame width rejected");
    MIB_EXPECT(!mv::frameFitsInBuffer(640, 0, 640, 480), "zero frame height rejected");
    MIB_EXPECT(!mv::frameFitsInBuffer(-1, 480, 640, 480), "negative frame width rejected");
    MIB_EXPECT(!mv::frameFitsInBuffer(640, 480, 0, 480), "zero buffer width rejected");
    MIB_EXPECT(!mv::frameFitsInBuffer(640, 480, 640, 0), "zero buffer height rejected");

    if (mib::test::exitCode() == 0)
    {
        std::printf("MindVision frame-vs-buffer geometry bounds check verified\n");
    }
    return mib::test::exitCode();
}
