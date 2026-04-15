# Frontend — MOC

> Qt Widgets UI. Root is [[MainWindow]]; it owns the tab widget and wires
> controllers to [[../architecture/AppBackend]].

## Core
- [[MainWindow]] — QMainWindow; tabs, corner widgets, sidebar, statusbar
- [[Controllers]] — CameraController, ExperimentController

## Tabs
- [[ConnectTab]] — device selection (hardware or mock)
- [[PreviewPage]] — live display + playback + [[ConfigTabs]] dock
- [[ConfigTabs]] — experiment settings, JS camera scripts, ROI
- [[ExperimentMonitoringTab]] — live histograms + scatter plots
- [[HdfReviewTab]] — post-experiment review from saved HDF5
- [[NanopositionerTab]] — [[../services/AutofocusService]] UI
- [[SyringePumpTab]] — [[../services/SyringePumpService]] UI

## Support
- [[Dialogs]] — settings dialogs (Mock, Processing, Monitoring, Buffer save,
  Conversion factor, Frame viewer, Syringe pump)
- [[System-Utilities]] — `AppConfigWatcher`, `AutoUpdater`,
  `DeviceInitManager`, `PlaybackPanel`, notifier bridges

**Up**: [[../README|Vault home]] · **See also**: [[../services/_MOC]]
