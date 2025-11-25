#pragma once

#include <QObject>
#include <QFileSystemWatcher>
#include <QJsonObject>
#include <QString>

namespace backend { class AppBackend; }
class PlaybackPanel;

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

private slots:
	void onFileChanged(const QString& path);

private:
	QString resolveActiveConfigPath() const;
	void ensureDefaultConfigExists(const QString& path) const;
	void loadAndApplyFromPath(const QString& path);
	static int toOddKernelSize(int v);

	backend::AppBackend& backend_;
	PlaybackPanel* playbackPanel_{nullptr};
	QFileSystemWatcher watcher_;
	QString watchedPath_;
};

} // namespace frontend


