#!/bin/bash
echo -e "\noption(BUILD_XWINDOWSWITCHER \"Build the extension\" ON)\nif (BUILD_XWINDOWSWITCHER)\n    add_subdirectory(Albert-X-Window-Switcher)\nendif()\n" >> ./CMakeLists.txt