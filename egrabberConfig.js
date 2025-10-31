var g = grabbers[0];
g.RemotePort.execute("AcquisitionStop");
g.InterfacePort.set("LineSelector", "TTLIO12");//Trigger
g.InterfacePort.set("LineMode", "Output");
g.InterfacePort.set("LineSource", "Low");
g.InterfacePort.set("LineSelector", "TTLIO11"); //LED
g.InterfacePort.set("LineMode", "Output");
g.InterfacePort.set("LineInverter", true);
g.InterfacePort.set("LineSource", "Device0Strobe");
g.RemotePort.set("Width", 512);
g.RemotePort.set("Height", 96);
g.RemotePort.set("OffsetY", 500);
g.RemotePort.set("OffsetX", 704);
g.RemotePort.set("ExposureTime", 3);
g.DevicePort.set("CameraControlMethod", "RC");
g.DevicePort.set("ExposureRecoveryTime", "200");
g.DevicePort.set("CycleMinimumPeriod", "200");
g.DevicePort.set("StrobeDelay", "-4");
g.DevicePort.set("StrobeDuration", "12");
g.RemotePort.set("TriggerMode", "On");
g.RemotePort.set("TriggerSource", "LinkTrigger0");
g.RemotePort.execute("AcquisitionStart");
