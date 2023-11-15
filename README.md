# Time Machine Helper

A Qt based GUI application for helping with macOS Time Machine.
This is a macOS specific application.

## Prerequisites

This project minimally needs the following:

- [macOS Sonoma (version 14)](https://www.apple.com/macos/sonoma/) (or newer).
- [C++20](https://en.wikipedia.org/wiki/C++20) (or higher).
- [coroutine library support](https://en.cppreference.com/w/cpp/coroutine).

Additionally, to use administrative commands like to delete backups, the `sudo` system needs to be setup to allow the application running user to run the `/usr/bin/tmutil` program as root.

For more info on `sudo`, see:

```sh
man 8 sudo
```

## Please Note

As of November 8, 2023, GitHub doesn't appear to have a runner available for macos-14 yet.
So, this project's continuous integration setup is disabled for now and shows up as failed.
