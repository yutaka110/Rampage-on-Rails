#include <Windows.h>
#include <exception>
#include <filesystem>
#include <fstream>

#include "application/AppCommandLineRunner.h"
#include "application/AppMain.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    const AppCommandLineResult commandLineResult = RunAppCommandLineTools();
    if (commandLineResult.handled) {
        return commandLineResult.exitCode;
    }

    try {
        AppMain app;
        if (!app.Initialize(hInstance)) {
            return -1;
        }
        return app.Run();
    } catch (const std::exception& error) {
        wchar_t executable[MAX_PATH]{};
        GetModuleFileNameW(nullptr, executable, MAX_PATH);
        const auto directory = std::filesystem::path(executable).parent_path() / "logs";
        std::error_code pathError;
        std::filesystem::create_directories(directory, pathError);
        std::ofstream(directory / "startup_error.log") << error.what() << '\n';
        MessageBoxA(nullptr, error.what(), "Game startup failed", MB_OK | MB_ICONERROR);
        return 1;
    }
}
