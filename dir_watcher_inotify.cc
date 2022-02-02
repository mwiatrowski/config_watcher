#include "dir_watcher.h"

#include <poll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <unistd.h>

#include <iostream>
#include <sstream>
#include <thread>

using namespace std;

namespace common {
namespace dir_watcher {

void sendFileEvents(byte* events_buffer, int buffer_len, WatcherCallback callback) {
    int offset = 0;

    while (offset + sizeof(inotify_event) <= buffer_len) {
        inotify_event* event = (inotify_event*) (events_buffer + offset);
        offset += sizeof(inotify_event) + event->len;

        string filename(event->name, event->name + event->len);
        int mask = event->mask;

        if ((mask & IN_DELETE) || (mask & IN_MOVED_FROM)) {
            callback(FileEvent(FILE_REMOVED, filename));
        }

        if ((mask & IN_CREATE) || (mask & IN_MOVED_TO)) {
            callback(FileEvent(FILE_CREATED, filename));
        }

        if (mask & IN_MODIFY) {
            callback(FileEvent(FILE_CHANGED, filename));
        }
    }
}

void watchDirectoryInternal(filesystem::path path, int exit_fd, WatcherCallback callback) {
    int inotify_fd = inotify_init();
    if (inotify_fd == -1) {
        cerr << "inotify_init() error" << endl;
        return;
    }

    int dir_fd = inotify_add_watch(
        inotify_fd,
        path.string().c_str(),
        IN_ATTRIB | IN_CREATE | IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO);
    if (dir_fd == -1) {
        cerr << "inotify_add_watch() error" << endl;
        close(inotify_fd);
        return;
    }

    pollfd poll_fds[2];
    poll_fds[0] = { .fd = inotify_fd, .events = POLLIN, .revents = 0 };
    poll_fds[1] = { .fd = exit_fd, .events = POLLIN, .revents = 0 };

    constexpr int buff_len = 1024;
    alignas(inotify_event) byte events_buffer[buff_len];

    while (true) {
        int success = poll(poll_fds, 2, -1);
        if (success < 0) {
            cerr << "poll() error" << endl;
            break;
        }

        if (poll_fds[1].revents & POLLIN) {
            break;
        }

        if (poll_fds[0].revents & POLLIN) {
            int bytes_read = read(inotify_fd, events_buffer, sizeof(events_buffer));
            if (bytes_read == -1) {
                cerr << "read() error" << endl;
                continue;
            }

            sendFileEvents(events_buffer, bytes_read, callback);
        }
    }

    close(dir_fd);
    close(inotify_fd);
}

class DirWatcherImpl {
  public:
    ~DirWatcherImpl() {
        if (bool(worker_thread)) {
            uint64_t b = 1;
            write(exit_fd, &b, sizeof(b));
            worker_thread->join();
            worker_thread.reset();
            close(exit_fd);
        }
    }

    bool watchDirectory(const filesystem::path& path, WatcherCallback callback) {
        if (bool(worker_thread)) {
            cerr << "FileWatcherImpl is already running" << endl;
            return false;
        }

        exit_fd = eventfd(0, 0);
        if (exit_fd == -1) {
            cerr << "eventfd() error" << endl;
            return false;
        }

        worker_thread = make_unique<thread>(watchDirectoryInternal, path, exit_fd, callback);
        return true;
    }

  private:
    int exit_fd = -1;
    unique_ptr<thread> worker_thread;
};

DirWatcher::DirWatcher() : impl(make_unique<DirWatcherImpl>()) {}

DirWatcher::~DirWatcher() {
    impl.reset();
}

bool DirWatcher::watchDirectory(const filesystem::path& path, WatcherCallback callback) {
    return impl->watchDirectory(path, callback);
}

} // namespace dir_watcher
} // namespace common
