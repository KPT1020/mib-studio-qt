#pragma once

#include <QObject>
#include <QFileSystemWatcher>
#include <QJsonObject>
#include <QString>
#include <QRect>

namespace backend { class AppBackend; }
namespace camera::common { enum class FrameDeliveryMode; }
class PlaybackPanel;
class QTimer;

namespace frontend {

class AppConfigWatcher : public QObject {
	Q_OBJECT
public:
	explicit AppConfigWatcher(backend::AppBackend& backend,
	                          PlaybackPanel* playbackPanel,
	                          QObject* parent = nullptr);

	// Start watching the current active app config path (external if set, else default include).
	void start();
	// Override the watched path explicitly (e.g., after Browse/Clear).
	void setWatchedPath(const QString& path);
	// Try to restore pending ROI if image dimensions are now available
	void tryRestorePendingRoi();
	// Write current ProcessingConfig back to the watched config.json file
	void writeBackProcessingConfig();
	// Persist camera.frame_delivery_mode into the watched config.json file,
	// preserving all unrelated keys.
	void writeBackCameraConfig(camera::common::FrameDeliveryMode mode);
	// Delivery mode most recently loaded from (or written to) the config file.
	camera::common::FrameDeliveryMode loadedDeliveryMode() const { return lastDeliveryMode_; }

signals:
	// Emitted when the watched config file changes
	void configFileChanged(const QString& path);
	// Emitted after camera.frame_delivery_mode has been parsed and applied to
	// CaptureService (a missing key deterministically maps to EveryFrame).
	void deliveryModeLoaded(camera::common::FrameDeliveryMode mode);

private slots:
	void onFileChanged(const QString& path);

private:
	QString resolveActiveConfigPath() const;
	void ensureDefaultConfigExists(const QString& path) const;
	void mergeNewDefaultsIntoConfig(const QString& path) const;
	void loadAndApplyFromPath(const QString& path);
	static int toOddKernelSize(int v);

	backend::AppBackend& backend_;
	PlaybackPanel* playbackPanel_{nullptr};
	QFileSystemWatcher watcher_;
	QString watchedPath_;
	QRect pendingRoi_;  // ROI to restore when image dimensions become available
	bool hasPendingRoi_ = false;  // Whether there's a pending ROI to restore
	QTimer* pendingRoiTimer_ = nullptr;  // Timer to periodically check for ROI restoration
	// Value-initialized to 0 == FrameDeliveryMode::EveryFrame (the enum is only
	// forward-declared here, so the enumerator itself is not nameable).
	camera::common::FrameDeliveryMode lastDeliveryMode_{};
};

} // namespace frontend


