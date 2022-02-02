#include "dir_watcher.h"

#include <windows.h>

#include <cassert>
#include <cstddef>
#include <cstring>

#include <chrono>
#include <codecvt>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>

using namespace std;

namespace common {
namespace dir_watcher {

using common::dir_watcher::WatcherCallback;

void sendFileEvents(char* events_buffer, int buffer_len, WatcherCallback callback) {
    wstring_convert<codecvt<wchar_t, char, mbstate_t>, wchar_t> converter;
    int offset = 0;

    while (offset + sizeof(FILE_NOTIFY_INFORMATION) <= buffer_len) {
        PFILE_NOTIFY_INFORMATION notification = (PFILE_NOTIFY_INFORMATION) (events_buffer + offset);

        int next_entry_offset = notification->NextEntryOffset;
        if (next_entry_offset == 0) {
            offset = buffer_len;
        } else {
            offset += next_entry_offset;
        }

        constexpr int max_filename_len = 1000;
        char filename_buffer[max_filename_len];
        int bytes_written = WideCharToMultiByte(
            CP_ACP,
            0,
            notification->FileName,
            notification->FileNameLength / sizeof(WCHAR),
            filename_buffer,
            max_filename_len,
            NULL,
            NULL);
        if (bytes_written == 0) {
            cerr << "WideCharToMultiByte() error" << endl;
            continue;
        }
        string filename = string(filename_buffer, filename_buffer + bytes_written);

        switch (notification->Action) {
        case FILE_ACTION_ADDED:
        case FILE_ACTION_RENAMED_NEW_NAME: {
            callback(FileEvent(FILE_CREATED, filename));
            break;
        }

        case FILE_ACTION_REMOVED:
        case FILE_ACTION_RENAMED_OLD_NAME: {
            callback(FileEvent(FILE_REMOVED, filename));
            break;
        }

        case FILE_ACTION_MODIFIED: {
            callback(FileEvent(FILE_CHANGED, filename));
            break;
        }

        default: {
            assert(false);
        }
        }
    }
}

void watchDirectoryInternal(filesystem::path path, HANDLE exit_handle, WatcherCallback callback) {
    string path_as_string = path.string();
    LPSTR dir_path = const_cast<char*>(path_as_string.c_str());

    HANDLE dir_handle = CreateFileA(
        dir_path,
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        NULL);
    if (dir_handle == INVALID_HANDLE_VALUE) {
        cerr << "CreateFileA() error" << endl;
        return;
    }

    HANDLE change_handle = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (change_handle == NULL) {
        cerr << "CreateEvent() error" << endl;
        CloseHandle(dir_handle);
        return;
    }

    OVERLAPPED overlapped;
    memset(&overlapped, 0, sizeof(OVERLAPPED));
    overlapped.hEvent = change_handle;

    constexpr int buffer_size = 1024;
    alignas(DWORD) char events_buffer[buffer_size];

    BOOL success = ReadDirectoryChangesW(
        dir_handle,
        (LPVOID) events_buffer,
        buffer_size,
        FALSE,
        FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE,
        NULL,
        &overlapped,
        NULL);
    if (!success) {
        cerr << "ReadDirectoryChangesW() error" << endl;
        CloseHandle(change_handle);
        CloseHandle(dir_handle);
        return;
    }

    HANDLE wait_handles[2] = { change_handle, exit_handle };
    bool must_exit = false;

    while (!must_exit) {
        DWORD wait_status = WaitForMultipleObjects(2, wait_handles, FALSE, INFINITE);

        switch (wait_status) {
        case WAIT_OBJECT_0: {
            DWORD bytes_transferred;
            GetOverlappedResult(dir_handle, &overlapped, &bytes_transferred, FALSE);
            sendFileEvents(events_buffer, bytes_transferred, callback);

            BOOL success = ReadDirectoryChangesW(
                dir_handle,
                (LPVOID) events_buffer,
                buffer_size,
                FALSE,
                FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE,
                NULL,
                &overlapped,
                NULL);
            if (!success) {
                cerr << "ReadDirectoryChangesW() error" << endl;
                CloseHandle(change_handle);
                CloseHandle(dir_handle);
                return;
            }

            break;
        }

        case WAIT_OBJECT_0 + 1: {
            must_exit = true;
            break;
        }

        default: {
            assert(false);
        }
        }
    }

    CloseHandle(change_handle);
    CloseHandle(dir_handle);
}

class DirWatcherImpl {
  public:
    ~DirWatcherImpl() {
        if (bool(worker_thread)) {
            SetEvent(exit_handle);
            worker_thread->join();
            worker_thread.reset();
            CloseHandle(exit_handle);
        }
    }

    bool watchDirectory(const filesystem::path& path, WatcherCallback callback) {
        if (bool(worker_thread)) {
            cerr << "FileWatcherImpl is already running" << endl;
            return false;
        }

        exit_handle = CreateEvent(NULL, FALSE, FALSE, NULL);
        if (exit_handle == NULL) {
            cerr << "CreateEvent() error" << endl;
            return false;
        }

        worker_thread = make_unique<thread>(watchDirectoryInternal, path, exit_handle, callback);
        return true;
    }

  private:
    HANDLE exit_handle = NULL;
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
