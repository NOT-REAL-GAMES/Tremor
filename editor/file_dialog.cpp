// Windows includes MUST come before main.h
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
// Deliberately NOT defining NOUSER here — shobjidl.h needs it
#include <windows.h>
#include <shobjidl.h>
#include <objbase.h>
#include <atlcomcli.h>
#endif

// Now main.h is safe to include
#include "file_dialog.h"
#include "../main.h"

#include <filesystem>
#include <iostream>
#include <algorithm>

#ifdef __linux__
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <array>
#endif


namespace tremor::editor {

    // =============================================================================
    // FileDialog Implementation
    // =============================================================================

    std::string FileDialog::show(Type type, const std::string& title,
                                const std::vector<Filter>& filters,
                                const std::string& defaultPath) {
        #ifdef _WIN32
        return showWindowsDialog(type, title, filters, defaultPath);
        #elif defined(__linux__)
        return showLinuxDialog(type, title, filters, defaultPath);
        #else
        return showConsoleDialog(type, title, filters, defaultPath);
        #endif
    }

    std::string FileDialog::showOpenDialog(const std::string& defaultPath) {
        std::vector<Filter> filters = {
            {"Taffy Files", "*.taf"},
            {"GLTF Files", "*.gltf"},
            {"GLB Files", "*.glb"},
            {"All Model Files", "*.taf;*.gltf;*.glb"},
            {"All Files", "*.*"}
        };
        return show(Type::Open, "Open Model", filters, defaultPath);
    }

    std::string FileDialog::showSaveDialog(const std::string& defaultPath) {
        std::vector<Filter> filters = {
            {"Taffy Files", "*.taf"},
            {"All Files", "*.*"}
        };
        return show(Type::Save, "Save Model", filters, defaultPath);
    }

    bool FileDialog::fileExists(const std::string& path) {
        return std::filesystem::exists(path);
    }

    std::string FileDialog::getFileExtension(const std::string& path) {
        std::filesystem::path p(path);
        return p.extension().string();
    }
    
    #ifdef _WIN32
    std::string FileDialog::showWindowsDialog(Type type, const std::string& title,
                                            const std::vector<Filter>& filters,
                                            const std::string& defaultPath) {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

        std::string result;

        CComPtr<IFileDialog> dialog;
        HRESULT hr;

        if (type == Type::Open) {
            hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                                CLSCTX_ALL, IID_IFileOpenDialog,
                                reinterpret_cast<void**>(&dialog));
        } else {
            hr = CoCreateInstance(CLSID_FileSaveDialog, nullptr,
                                CLSCTX_ALL, IID_IFileSaveDialog,
                                reinterpret_cast<void**>(&dialog));
        }

        if (FAILED(hr)) {
            CoUninitialize();
            return showConsoleDialog(type, title, filters, defaultPath);
        }

        // Set title
        std::wstring wtitle(title.begin(), title.end());
        dialog->SetTitle(wtitle.c_str());

        // Build filter spec from filters vector
        std::vector<COMDLG_FILTERSPEC> filterSpecs;
        std::vector<std::wstring> nameStorage, specStorage; // keep wstrings alive

        for (const auto& f : filters) {
            nameStorage.emplace_back(f.name.begin(), f.name.end());
            specStorage.emplace_back(f.extension.begin(), f.extension.end());
            filterSpecs.push_back({ nameStorage.back().c_str(),
                                    specStorage.back().c_str() });
        }

        if (!filterSpecs.empty()) {
            dialog->SetFileTypes(static_cast<UINT>(filterSpecs.size()),
                                filterSpecs.data());
        }

        // Set default folder
        if (!defaultPath.empty()) {
            std::wstring wpath(defaultPath.begin(), defaultPath.end());
            CComPtr<IShellItem> folder;
            if (SUCCEEDED(SHCreateItemFromParsingName(wpath.c_str(), nullptr,
                                                    IID_IShellItem,
                                                    reinterpret_cast<void**>(&folder)))) {
                dialog->SetDefaultFolder(folder);
            }
        }

        if (type == Type::Save) {
            dialog->SetDefaultExtension(L"taf");
        }

        // Show dialog
        hr = dialog->Show(nullptr);
        if (SUCCEEDED(hr)) {
            CComPtr<IShellItem> item;
            if (SUCCEEDED(dialog->GetResult(&item))) {
                PWSTR filePath = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &filePath))) {
                    // Convert wide string back to narrow
                    std::wstring ws(filePath);
                    result = std::string(ws.begin(), ws.end());
                    CoTaskMemFree(filePath);
                }
            }
        }

        CoUninitialize();
        return result;
    }
    #endif

    #ifdef __linux__
    std::string FileDialog::showLinuxDialog(Type type, const std::string& title,
                                           const std::vector<Filter>& filters,
                                           const std::string& defaultPath) {
        // Try to use zenity (GTK file dialog) if available
        std::string command = "which zenity > /dev/null 2>&1";
        if (system(command.c_str()) == 0) {
            std::string zenityCmd = "zenity --file-selection";
            zenityCmd += " --title=\"" + title + "\"";
            
            if (type == Type::Save) {
                zenityCmd += " --save";
                zenityCmd += " --confirm-overwrite";
            }

            if (!defaultPath.empty()) {
                zenityCmd += " --filename=\"" + defaultPath + "\"";
            }

            // Add file filters
            for (const auto& filter : filters) {
                zenityCmd += " --file-filter=\"" + filter.name + " | " + filter.extension + "\"";
            }

            zenityCmd += " 2>/dev/null"; // Suppress stderr

            FILE* pipe = popen(zenityCmd.c_str(), "r");
            if (pipe) {
                char buffer[512];
                std::string result;
                while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                    result += buffer;
                }
                pclose(pipe);

                // Remove trailing newline
                if (!result.empty() && result.back() == '\n') {
                    result.pop_back();
                }

                return result;
            }
        }

        // Fallback to console dialog
        Logger::get().warning("GUI file dialog not available, using console interface");
        return showConsoleDialog(type, title, filters, defaultPath);
    }
    #endif

    std::string FileDialog::showConsoleDialog(Type type, const std::string& title,
                                             const std::vector<Filter>& filters,
                                             const std::string& defaultPath) {
        std::cout << "\n=== " << title << " ===" << std::endl;
        
        // Show current directory
        std::string currentDir = std::filesystem::current_path().string();
        std::cout << "Current directory: " << currentDir << std::endl;

        // If default path is provided, start there
        std::string searchDir = defaultPath.empty() ? currentDir : defaultPath;
        if (!std::filesystem::is_directory(searchDir)) {
            searchDir = std::filesystem::path(searchDir).parent_path().string();
        }

        if (type == Type::Open) {
            // List available files
            auto files = listFiles(searchDir, filters);
            
            if (files.empty()) {
                std::cout << "No matching files found in: " << searchDir << std::endl;
                std::cout << "Enter full path to file: ";
                std::string path;
                std::getline(std::cin, path);
                return path;
            }

            std::cout << "\nAvailable files:" << std::endl;
            for (size_t i = 0; i < files.size(); ++i) {
                std::cout << "  " << (i + 1) << ". " << files[i] << std::endl;
            }

            std::cout << "\nEnter file number (1-" << files.size() << ") or full path: ";
            std::string input;
            std::getline(std::cin, input);

            // Try to parse as number
            try {
                int index = std::stoi(input) - 1;
                if (index >= 0 && index < static_cast<int>(files.size())) {
                    return files[index];
                }
            } catch (...) {
                // Not a number, treat as path
            }

            return input;
        } else {
            // Save dialog
            std::cout << "Enter filename to save: ";
            std::string filename;
            std::getline(std::cin, filename);

            // Add .taf extension if not present
            if (!filters.empty() && getFileExtension(filename).empty()) {
                std::string ext = filters[0].extension;
                if (ext.starts_with("*.")) {
                    ext = ext.substr(1); // Remove "*"
                }
                filename += ext;
            }

            // Make path relative to search directory if not absolute
            if (!std::filesystem::path(filename).is_absolute()) {
                filename = (std::filesystem::path(searchDir) / std::filesystem::path(filename)).generic_string();
            }

            return filename;
        }
    }

    std::vector<std::string> FileDialog::listFiles(const std::string& directory,
                                                  const std::vector<Filter>& filters) {
        std::vector<std::string> files;

        try {
            for (const auto& entry : std::filesystem::directory_iterator(directory)) {
                if (entry.is_regular_file()) {
                    std::string filepath = entry.path().string();
                    std::string extension = entry.path().extension().string();

                    // Check if file matches any filter
                    bool matches = filters.empty(); // If no filters, include all files
                    for (const auto& filter : filters) {
                        if (filter.extension == "*.*" || 
                            filter.extension == ("*" + extension)) {
                            matches = true;
                            break;
                        }
                    }

                    if (matches) {
                        files.push_back(filepath);
                    }
                }
            }
        } catch (const std::filesystem::filesystem_error& e) {
            Logger::get().error("Error listing files in {}: {}", directory, e.what());
        }

        // Sort files alphabetically
        std::sort(files.begin(), files.end());
        return files;
    }

} // namespace tremor::editor