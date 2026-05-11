#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#define NOGDI

#ifdef _WIN32
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #define NOCOMM
    #define NOSOUND
    #define NODRAWTEXT
    #define NOGDI
    #define NOUSER
    #define NOMCX
    #include <windows.h>
    #include <io.h>
    #include <direct.h>
#else
    #include <unistd.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <dirent.h>
#endif

#ifdef _WIN32
#undef SearchPath
#undef NEAR
#undef FAR
#undef near
#undef far
#endif
