#include "AppRuntimeUtils.h"
#include <fstream>
#include <cstdint>
#include <filesystem>

std::wstring ConvertString(const std::string& str) {
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    std::wstring result(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &result[0], size);
    return result;
}

std::string ConvertString(const std::wstring& str) {
    int size = WideCharToMultiByte(CP_UTF8, 0, str.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, str.c_str(), -1, &result[0], size, nullptr, nullptr);
    return result;
}

void Log(const std::wstring& message) { OutputDebugStringW(message.c_str()); }

void Log(std::ostream& os, const std::string& message) {
    os << message << std::endl;
    OutputDebugStringA(message.c_str());
}

void Log(const char* message) { OutputDebugStringA(message); }

void Log(const std::string& message) { OutputDebugStringA(message.c_str()); }

LONG WINAPI ExportDump(EXCEPTION_POINTERS* exception) {
    if (exception && exception->ExceptionRecord) {
        wchar_t executable[MAX_PATH]{};
        GetModuleFileNameW(nullptr, executable, MAX_PATH);
        const auto directory = std::filesystem::path(executable).parent_path() / "logs";
        std::error_code pathError;
        std::filesystem::create_directories(directory, pathError);
        std::ofstream crash(directory / "crash.log", std::ios::app);
        const auto* record = exception->ExceptionRecord;
        crash << "exception=0x" << std::hex << record->ExceptionCode
              << " address=" << record->ExceptionAddress << '\n';
        HMODULE module = nullptr;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(record->ExceptionAddress), &module)) {
            wchar_t path[MAX_PATH]{};
            GetModuleFileNameW(module, path, MAX_PATH);
            crash << "module=" << ConvertString(std::wstring(path))
                  << " offset=0x" << (reinterpret_cast<uintptr_t>(record->ExceptionAddress) -
                      reinterpret_cast<uintptr_t>(module)) << '\n';
        }
    }
    MessageBoxA(nullptr, "Crash!", "Error", MB_OK);
    return EXCEPTION_EXECUTE_HANDLER;
}
