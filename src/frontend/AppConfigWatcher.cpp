#include "frontend/AppConfigWatcher.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSettings>
#include <QTextStream>

#include <spdlog/spdlog.h>
#ifdef _WIN32
#define NOMINMAX // Prevent Windows.h from defining min/max macros
#include <windows.h>
#include <shlobj.h>
#endif

#include "backend/AppBackend.h"
#include "backend/services/ProcessingService.h"
#include "backend/services/AutofocusService.h"
#include "frontend/PlaybackPanel.h"

namespace frontend
{

	namespace
	{
		// Get user-writable config directory, falling back to ../include/ for development
		static QString getUserConfigDir()
		{
			QString appDir = QCoreApplication::applicationDirPath();
			QString appDirLower = appDir.toLower();

#ifdef _WIN32
			// Check if installed in Program Files (requires admin to write)
			if (appDirLower.contains("program files") ||
				appDirLower.contains("program files (x86)"))
			{
				// Use user-writable location
				char appDataPath[MAX_PATH];
				if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, appDataPath)))
				{
					QString userConfigDir = QDir(QString::fromStdString(std::string(appDataPath) + "\\MIB_Studio_Qt\\include")).absolutePath();
					// Ensure directory exists
					QDir().mkpath(userConfigDir);
					return userConfigDir;
				}
			}
#endif
			// Development: use ../include/ relative to executable
			return QDir(appDir).absoluteFilePath("../include");
		}
	}

	AppConfigWatcher::AppConfigWatcher(backend::AppBackend &backend,
									   PlaybackPanel *playbackPanel,
									   QObject *parent)
		: QObject(parent), backend_(backend), playbackPanel_(playbackPanel)
	{
		connect(&watcher_, &QFileSystemWatcher::fileChanged, this, &AppConfigWatcher::onFileChanged);
	}

	void AppConfigWatcher::start()
	{
		setWatchedPath(resolveActiveConfigPath());
	}

	void AppConfigWatcher::setWatchedPath(const QString &path)
	{
		// Remove any previous path
		if (!watchedPath_.isEmpty())
		{
			watcher_.removePath(watchedPath_);
		}
		watchedPath_.clear();

		if (path.isEmpty())
		{
			return;
		}

		ensureDefaultConfigExists(path);

		if (!watcher_.addPath(path))
		{
			SPDLOG_WARN("AppConfigWatcher: failed to watch path {}", path.toStdString());
		}
		else
		{
			SPDLOG_INFO("AppConfigWatcher: watching {}", path.toStdString());
			watchedPath_ = path;
			// Immediately apply on (re)watch
			loadAndApplyFromPath(path);
		}
	}

	void AppConfigWatcher::onFileChanged(const QString &path)
	{
		SPDLOG_INFO("AppConfigWatcher: file changed: {}", path.toStdString());
		// Some platforms require re-adding the path after change
		if (!path.isEmpty())
		{
			watcher_.removePath(path);
			watcher_.addPath(path);
		}
		loadAndApplyFromPath(path);
	}

	QString AppConfigWatcher::resolveActiveConfigPath() const
	{
		QSettings s;
		const QString ext = s.value("Config/ExternalAppConfigPath").toString().trimmed();
		if (!ext.isEmpty())
			return ext;
		// Use centralized helper to get user-writable config directory
		return QDir(getUserConfigDir()).absoluteFilePath("config.json");
	}

	void AppConfigWatcher::ensureDefaultConfigExists(const QString &path) const
	{
		// Only ensure when path points to app include path
		const QString defaultPath = QDir(getUserConfigDir()).absoluteFilePath("config.json");
		if (QFileInfo(path).absoluteFilePath() != QFileInfo(defaultPath).absoluteFilePath())
		{
			return;
		}
		QFileInfo fi(path);
		QDir dir(fi.absolutePath());
		if (!dir.exists())
		{
			dir.mkpath(".");
		}
		if (!QFile::exists(path))
		{
			QFile res(":/defaults/config.json");
			if (res.open(QIODevice::ReadOnly))
			{
				QFile out(path);
				if (out.open(QIODevice::WriteOnly | QIODevice::Truncate))
				{
					const QByteArray data = res.readAll();
					if (out.write(data) != data.size())
					{
						SPDLOG_WARN("AppConfigWatcher: failed to write default config.json to {}", path.toStdString());
					}
				}
				else
				{
					SPDLOG_WARN("AppConfigWatcher: failed to create {}", path.toStdString());
				}
			}
			else
			{
				SPDLOG_WARN("AppConfigWatcher: failed to open resource defaults/config.json");
			}
		}
	}

	int AppConfigWatcher::toOddKernelSize(int v)
	{
		if (v < 1)
			v = 1;
		if ((v % 2) == 0)
			v += 1;
		return v;
	}

	void AppConfigWatcher::loadAndApplyFromPath(const QString &path)
	{
		QFile f(path);
		if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
		{
			SPDLOG_WARN("AppConfigWatcher: failed to open {}: {}", path.toStdString(), f.errorString().toStdString());
			return;
		}
		const QByteArray data = f.readAll();
		const QJsonDocument doc = QJsonDocument::fromJson(data);
		if (!doc.isObject())
		{
			SPDLOG_WARN("AppConfigWatcher: config root is not an object");
			return;
		}
		const QJsonObject root = doc.object();

		// 1) Processing config
		backend::services::ProcessingConfig pcfg;
		{
			// Start from current config to preserve unspecified values
			pcfg = backend_.processing().getProcessingConfig();
			if (root.contains("image_processing") && root.value("image_processing").isObject())
			{
				const QJsonObject ip = root.value("image_processing").toObject();
				if (ip.contains("gaussian_blur_size"))
					pcfg.gaussian_blur_size = ip.value("gaussian_blur_size").toInt(pcfg.gaussian_blur_size);
				if (ip.contains("bg_subtract_threshold"))
					pcfg.bg_subtract_threshold = ip.value("bg_subtract_threshold").toInt(pcfg.bg_subtract_threshold);
				if (ip.contains("morph_kernel_size"))
					pcfg.morph_kernel_size = ip.value("morph_kernel_size").toInt(pcfg.morph_kernel_size);
				if (ip.contains("morph_iterations"))
					pcfg.morph_iterations = ip.value("morph_iterations").toInt(pcfg.morph_iterations);
				if (ip.contains("area_threshold_min"))
					pcfg.area_threshold_min = ip.value("area_threshold_min").toInt(pcfg.area_threshold_min);
				if (ip.contains("area_threshold_max"))
					pcfg.area_threshold_max = ip.value("area_threshold_max").toInt(pcfg.area_threshold_max);
				if (ip.contains("empty_frame_pixel_threshold"))
					pcfg.empty_frame_pixel_threshold = ip.value("empty_frame_pixel_threshold").toInt(pcfg.empty_frame_pixel_threshold);
				if (ip.contains("filters") && ip.value("filters").isObject())
				{
					const QJsonObject fl = ip.value("filters").toObject();
					if (fl.contains("enable_border_check"))
						pcfg.enable_border_check = fl.value("enable_border_check").toBool(pcfg.enable_border_check);
					if (fl.contains("enable_area_range_check"))
						pcfg.enable_area_range_check = fl.value("enable_area_range_check").toBool(pcfg.enable_area_range_check);
					if (fl.contains("require_single_inner_contour"))
						pcfg.require_single_inner_contour = fl.value("require_single_inner_contour").toBool(pcfg.require_single_inner_contour);
				}
			}
		}
		backend_.processing().setProcessingConfig(pcfg);
		SPDLOG_INFO("AppConfigWatcher: applied ProcessingConfig (blur={}, thresh={}, morph={}x{}, area=[{},{}], empty_px={})",
					pcfg.gaussian_blur_size, pcfg.bg_subtract_threshold, pcfg.morph_kernel_size, pcfg.morph_iterations,
					pcfg.area_threshold_min, pcfg.area_threshold_max, pcfg.empty_frame_pixel_threshold);

		// 2) Flush interval (buffer threshold)
		if (root.contains("buffer_threshold"))
		{
			const int flushEvery = std::max(1, root.value("buffer_threshold").toInt(1000));
			backend_.processing().setFlushInterval(static_cast<size_t>(flushEvery));
		}

		// 2.5) Pixel to micron conversion factor
		if (root.contains("pixel_to_micron_factor"))
		{
			const double factor = root.value("pixel_to_micron_factor").toDouble(0.4886);
			if (factor > 0.0)
			{
				backend_.processing().setPixelToMicronFactor(factor);
				SPDLOG_INFO("AppConfigWatcher: applied pixel_to_micron_factor={}", factor);
			}
		}

		// 3) Display FPS for PlaybackPanel
		if (playbackPanel_ && root.contains("display_fps"))
		{
			const int fps = std::max(1, std::min(240, root.value("display_fps").toInt(60)));
			playbackPanel_->setDisplayFps(fps);
		}

		// 4) Autofocus config (propagate to service)
		{
			backend::services::AutofocusService::Config af = backend_.autofocus().getConfig();
			if (root.contains("autofocus_focus_setpoint"))
				af.focusSetpoint = root.value("autofocus_focus_setpoint").toDouble(af.focusSetpoint);
			if (root.contains("autofocus_focus_range"))
				af.focusRange = root.value("autofocus_focus_range").toDouble(af.focusRange);
			if (root.contains("autofocus_voltage_step"))
				af.voltageStep = root.value("autofocus_voltage_step").toDouble(af.voltageStep);
			if (root.contains("autofocus_fine_voltage_step"))
				af.fineVoltageStep = root.value("autofocus_fine_voltage_step").toDouble(af.fineVoltageStep);
			if (root.contains("autofocus_max_voltage"))
				af.maxVoltage = root.value("autofocus_max_voltage").toDouble(af.maxVoltage);
			if (root.contains("autofocus_min_voltage"))
				af.minVoltage = root.value("autofocus_min_voltage").toDouble(af.minVoltage);
			if (root.contains("autofocus_initial_voltage"))
				af.initialVoltage = root.value("autofocus_initial_voltage").toDouble(af.initialVoltage);
			if (root.contains("autofocus_manual_voltage_step"))
				af.manualVoltageStep = root.value("autofocus_manual_voltage_step").toDouble(af.manualVoltageStep);
			if (root.contains("ring_ratio_stale_ms"))
				af.ringRatioStaleMs = root.value("ring_ratio_stale_ms").toInt(af.ringRatioStaleMs);
			if (root.contains("require_new_sample_per_step"))
				af.requireNewSamplePerStep = root.value("require_new_sample_per_step").toBool(af.requireNewSamplePerStep);
			if (root.contains("autofocus_min_samples_per_step"))
				af.minSamplesPerStep = root.value("autofocus_min_samples_per_step").toInt(af.minSamplesPerStep);
			if (root.contains("safe_shutdown_voltage"))
				af.safeShutdownVoltage = root.value("safe_shutdown_voltage").toDouble(af.safeShutdownVoltage);
			if (root.contains("focus_direction"))
				af.focusDirection = root.value("focus_direction").toBool(af.focusDirection);
			backend_.autofocus().setConfig(af);
		}
	}

} // namespace frontend
