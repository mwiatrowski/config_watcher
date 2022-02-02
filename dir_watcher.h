#pragma once

#include <filesystem>
#include <memory>
#include <string>

namespace common {
namespace dir_watcher {

enum FileEventType {
    FILE_CREATED,
    FILE_CHANGED,
    FILE_REMOVED
};

struct FileEvent {
    FileEventType type;
    std::string file;
    FileEvent(FileEventType type, std::string file) : type(type), file(std::move(file)) {}
};

typedef void (*WatcherCallback)(FileEvent);

class DirWatcherImpl;

// This class is not thread save
class DirWatcher {
  public:
    DirWatcher();
    ~DirWatcher();
    bool watchDirectory(const std::filesystem::path& path, WatcherCallback callback);

  private:
    std::unique_ptr<DirWatcherImpl> impl;
};

} // namespace dir_watcher
} // namespace common
