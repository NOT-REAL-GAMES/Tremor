#include "file_dialog.h"
#include "../main.h"
#include <filesystem>
#include <iostream>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif

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
        OPENFILENAMEA ofn;
        char szFile[260] = {0};

        // Initialize filename with default path
        if (!defaultPath.empty()) {
            strncpy(szFile, defaultPath.c_str(), sizeof(szFile) - 1);
        }

        ZeroMemory(&ofn, sizeof(ofn));
        ofn.lStructSize = sizeof(ofn);
        ofn.lpstrFile = szFile;
        ofn.nMaxFile = sizeof(szFile);
        ofn.lpstrTitle = title.c_str();
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

        // Build filter string
        std::string filterStr;
        for (const auto& filter : filters) {
            filterStr += filter.name + '\0' + filter.extension + '\0';
        }
        filterStr += '\0';
        ofn.lpstrFilter = filterStr.c_str();

        bool result = false;
        if (type == Type::Open) {
            result = GetOpenFileNameA(&ofn);
        } else {
            ofn.Flags |= OFN_OVERWRITEPROMPT;
            result = GetSaveFileNameA(&ofn);
        }

        if (result) {
            return std::string(szFile);
        }

        return ""; // User cancelled or error
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
                filename = std::filesystem::path(searchDir) / filename;
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