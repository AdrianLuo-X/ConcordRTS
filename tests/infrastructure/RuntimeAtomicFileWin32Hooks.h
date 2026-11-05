#pragma once
// W006-01: compile the unchanged production implementation with test-local
// Win32 call shims. Real files/handles are used; only selected OS results fail.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <mutex>
#include <ostream>
#include <sstream>
#include <streambuf>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace probe {
HANDLE WINAPI create(LPCWSTR,DWORD,DWORD,LPSECURITY_ATTRIBUTES,DWORD,DWORD,HANDLE);
BOOL WINAPI write(HANDLE,LPCVOID,DWORD,LPDWORD,LPOVERLAPPED);
BOOL WINAPI flush(HANDLE);
BOOL WINAPI close(HANDLE);
BOOL WINAPI replace(LPCWSTR,LPCWSTR,DWORD);
BOOL WINAPI cleanup(LPCWSTR);
}
#define CreateFileW probe::create
#define WriteFile probe::write
#define FlushFileBuffers probe::flush
#define CloseHandle probe::close
#define MoveFileExW probe::replace
#define DeleteFileW probe::cleanup
