#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#else
#include <X11/Xlib.h>
#endif
#include <string>
#include <iostream>
#include "FoxRacing.hpp"

#include <fstream>
#include <chrono>
#include <vector>

bool FoxRacing::s_requestRestart = false;
bool FoxRacing::s_restartUseVulkan = false;

void LogToFile(const std::string& message)
{
#ifdef _WIN32
	std::string logpath = "C:\\Temp\\Cake_screensaver.log";
#else
	std::string logpath = "/tmp/Cake_screensaver.log";
#endif
	try {
		std::ofstream file(logpath, std::ios::app);
		if (!file.is_open()) return;
		auto now = std::chrono::system_clock::now();
		auto time = std::chrono::system_clock::to_time_t(now);
		file << "[" << std::ctime(&time) << "] " << message << "\n";
		file.close();
	} catch (...) { }
}

int main(int argc, char* argv[]) {
	bool useVulkan = true;

	for (int i = 1; i < argc; ++i) {
		std::string arg = argv[i];
		if (arg == "--gl" || arg == "--opengl")
			useVulkan = false;
		else if (arg == "--vk" || arg == "--vulkan")
			useVulkan = true;
	}

	std::cout << "Starting FoxRacing with " << (useVulkan ? "Vulkan" : "OpenGL") << std::endl;

	while (true) {
		try {
			LogToFile("Creating FoxRacing game instance...");
			fe::XRGameOptions options(1000, 1000, false);
			options.useVulkan = useVulkan;
			FoxRacing game(options);

			LogToFile("Running game...");
			game.Run();

			LogToFile("Game exited normally");

			useVulkan = FoxRacing::s_restartUseVulkan;
			if (!FoxRacing::s_requestRestart) break;
			FoxRacing::s_requestRestart = false;

			std::cout << "Restarting with " << (useVulkan ? "Vulkan" : "OpenGL") << std::endl;
		} catch (const std::exception& e) {
			LogToFile(std::string("Exception caught: ") + e.what());
			break;
		} catch (...) {
			LogToFile("Unknown exception caught");
			break;
		}
	}

	return 0;
}

#ifdef _WIN32

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR lpCmdLine, int) {
	int argc = 0;
	LPWSTR* wArgv = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (!wArgv) return main(0, nullptr);

	std::vector<std::string> args;
	std::vector<char*> argv;
	for (int i = 0; i < argc; ++i) {
		int len = WideCharToMultiByte(CP_UTF8, 0, wArgv[i], -1, nullptr, 0, nullptr, nullptr);
		std::string s(len - 1, '\0');
		WideCharToMultiByte(CP_UTF8, 0, wArgv[i], -1, s.data(), len, nullptr, nullptr);
		args.push_back(std::move(s));
	}
	LocalFree(wArgv);

	for (auto& s : args) argv.push_back(s.data());

	return main(argc, argv.data());
}

#endif
