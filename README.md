# Windows
```
cl /EHsc /std:c++17 config_test.cc config_reader.cc dir_watcher_winapi.cc
```

# Linux
```
clang++ -std=c++17 -pthread config_test.cc config_reader.cc dir_watcher_inotify.cc -o config_test
```
