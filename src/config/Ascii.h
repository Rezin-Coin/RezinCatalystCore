// Copyright (c) 2018, The TurtleCoin Developers
//
// Please see the included LICENSE file for more information

#pragma once

#include <string>

const std::string windowsAsciiArt =
"\n                       \n"
" _____    _____   _____ \n"
"|  __ \  / ____| / ____|\n"
"| |__) || |     | |     \n"
"|  _  / | |     | |     \n"
"| | \ \ | |____ | |____ \n"
"|_|  \_\ \_____| \_____|\n";

const std::string nonWindowsAsciiArt =
   "\n                            \n"
    "  ______    ______   ______ \n"
    " (_____ \  / _____) / _____)\n"
    "  _____) )| /      | /      \n"
    " (_____ ( | |      | |      \n"
    "       | || \_____ | \_____ \n"
    "       |_| \______) \______)\n";

/* Windows has some characters it won't display in a terminal. If your ascii
   art works fine on Windows and Linux terminals, just replace 'asciiArt' with
   the art itself, and remove these two #ifdefs and above ascii arts */
#ifdef _WIN32

const std::string asciiArt = windowsAsciiArt;

#else
const std::string asciiArt = nonWindowsAsciiArt;
#endif
