# Albert X Window Switcher
*Note: The current build only supports Albert v0.16.4 and under. Support for Albert v0.17 is coming soon!

## Description
QPlugin for the [Albert Launcher](https://albertlauncher.github.io/) that allows Albert to switch to open windows. This utility is designed for systems that use the X11 window system. As of the time that this was written, this functionality does exist in Albert via a python wrapper for the [wmctrl](https://github.com/Conservatory/wmctrl) utility, however this implementation aims to have better performance as it removes any of the added overhead of python and pybind11 because the functionality is implemented natively.

## Dependencies
This plugin also shares the same dependencies that are needed to build [Albert](https://albertlauncher.github.io/) from sources. Information about building [Albert](https://albertlauncher.github.io/) from sources and its dependencies can be found [here](https://albertlauncher.github.io/docs/installing/).

## Installation

### Option 1: Compile alongside Albert
```
git clone --recursive https://github.com/albertlauncher/albert.git
cd albert/plugins
git submodule add https://github.com/jbwong05/Albert-X-Window-Switcher.git
./Albert-X-Window-Switcher/updateCMakeLists.sh
cd ..
mkdir albert-build
cd albert-build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release
make
sudo make install
```

### Option 2: Compile separately from Albert
```
git clone https://github.com/jbwong05/Albert-X-Window-Switcher.git
cd Albert-X-Window-Switcher
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release -DBUILD_SEPARATELY=ON ..
make
sudo make install
```

## Uninstallation
```
sudo rm -f /usr/lib/albert/plugins/libxwindowswitcher.so
```
