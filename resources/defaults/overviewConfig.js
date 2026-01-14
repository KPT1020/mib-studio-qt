// Decrease the resolution before increasing the frame rate

// Make sure to remove offset before changing the resolution

var g = grabbers[0];
g.RemotePort.execute("AcquisitionStop");
g.InterfacePort.set("LineSelector", "TTLIO12");//Trigger
g.InterfacePort.set("LineMode", "Output");
g.InterfacePort.set("LineSource", "Low");
g.InterfacePort.set("LineSelector", "TTLIO11"); //LED
g.InterfacePort.set("LineMode", "Output");
g.InterfacePort.set("LineInverter", true);
g.InterfacePort.set("LineSource", "Device0Strobe");
g.DevicePort.set("CameraControlMethod", "RC");
g.DevicePort.set("ExposureRecoveryTime", "20000");
g.DevicePort.set("CycleMinimumPeriod", "20000");
g.RemotePort.set("OffsetY", 0);
g.RemotePort.set("OffsetX", 0);
g.RemotePort.set("Width", 1920);
g.RemotePort.set("Height", 1080);
g.RemotePort.set("ExposureTime", 3);
g.DevicePort.set("StrobeDelay", "-10");
g.DevicePort.set("StrobeDuration", "20");
g.RemotePort.set("TriggerMode", "On");
g.RemotePort.set("TriggerSource", "LinkTrigger0");
g.RemotePort.execute("AcquisitionStart");

