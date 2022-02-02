#include "config_reader.h"

#include "dir_watcher.h"

#include <cassert>
#include <cctype>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>

using namespace std;

namespace common {

string trim(const string& text) {
    int n = text.size();

    int i = 0;
    for ( ; i < n; ++i) {
        if ((!isspace(text[i])) && (text[i] != '\0')) {
            break;
        }
    }

    int j = n - 1;
    for ( ; j >= 0; --j) {
        if ((!isspace(text[j])) && (text[j] != '\0')) {
            break;
        }
    }

    if (i > j) {
        return "";
    } else {
        return text.substr(i, j - i + 1);
    }
}

struct File {
    map<string, string> values;

    static File read(const filesystem::path& path) {
        File result;

        ifstream file_in(path, ios::in);
        string line;
        do {
            getline(file_in, line);

            if (line.length() > 0) {
                auto eq_pos = line.find('=');
                if (eq_pos == string::npos) {
                    continue;
                }

                string key = trim(line.substr(0, eq_pos));
                string value = trim(line.substr(eq_pos + 1));

                if (key.length() > 0 && value.length() > 0) {
                    result.values[key] = value;
                }
            }
        } while (file_in.good());

        return result;
    }
};

using common::dir_watcher::FileEvent;

void FileWatcherCallback(FileEvent);

class Reader {
  public:
    static Reader& getInstance() {
        static Reader instance;
        return instance;
    }

    bool watchDirectory(const string& path) {
        const lock_guard lock(data_mutex);

        if (already_watching) {
            return false;
        }

        bool success = file_watcher.watchDirectory(path, FileWatcherCallback);
        if (!success) {
            return false;
        }

        already_watching = true;
        watched_directory = path;

        error_code ec;
        auto directory = filesystem::directory_iterator(watched_directory, ec);
        if (bool(ec)) {
            return false;
        }

        for (auto const& dir_entry : directory) {
            if (!dir_entry.is_regular_file()) {
                continue;
            }

            auto new_file = File::read(dir_entry.path().string());
            string filename = trim(dir_entry.path().filename().string());
            files[filename] = move(new_file);
        }

        return true;
    }

    string load(const string& file, const string& key) {
        const lock_guard lock(data_mutex);

        if (files.count(file) == 0) {
            return "";
        }
        const File& config_file = files.at(file);
        if (config_file.values.count(key) == 0) {
            return "";
        }
        return config_file.values.at(key);
    }

    void processEvent(FileEvent event) {
        string filename = trim(event.file);

        switch (event.type) {
        case dir_watcher::FILE_CREATED: {
            cerr << "File created: " << filename << endl;
            auto new_file = File::read(watched_directory / filename);
            const lock_guard lock(data_mutex);
            files[filename] = move(new_file);
            break;
        }
        case dir_watcher::FILE_CHANGED: {
            cerr << "File changed: " << filename << endl;
            auto new_file = File::read(watched_directory / filename);
            const lock_guard lock(data_mutex);
            files[filename] = move(new_file);
            break;
        }
        case dir_watcher::FILE_REMOVED: {
            cerr << "File removed: " << filename << endl;
            const lock_guard lock(data_mutex);
            files.erase(filename);
            break;
        }
        default:
            assert(false);
        }
    }

  private:
    Reader() {}

    mutex data_mutex;
    dir_watcher::DirWatcher file_watcher;
    bool already_watching = false;
    filesystem::path watched_directory = "";
    map<string, File> files;
};

void FileWatcherCallback(FileEvent event) {
    auto& reader = Reader::getInstance();
    reader.processEvent(event);
}

namespace config {

bool watchDirectory(const string& path) {
    auto& reader = Reader::getInstance();
    return reader.watchDirectory(path);
}

string load(const string& file, const string& key) {
    auto& reader = Reader::getInstance();
    return reader.load(file, key);
}

} // namespace config
} // namespace common
