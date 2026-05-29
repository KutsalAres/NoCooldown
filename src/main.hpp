#pragma once
#include <cstdint>
#include <cstring>
#include "logger.hpp"
#ifdef __ANDROID__
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <link.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
