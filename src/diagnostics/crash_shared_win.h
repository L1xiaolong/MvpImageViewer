#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
namespace ispview::diagnostics {
struct CrashShared {
    DWORD processId;
    DWORD threadId;
    DWORD result;
    LONG shutdown;
    EXCEPTION_RECORD exception;
    CONTEXT context;
};
}
