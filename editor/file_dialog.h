#pragma once

#include <string>
#include <vector>

namespace tremor::editor {

    /**
     * Simple file dialog implementation for the model editor
     * Currently provides basic functionality - can be enhanced with platform-specific dialogs
     */
    class FileDialog {
    public:
        enum class Type {
            Open,
            Save
        };

        struct Filter {
            std::string name;        // Display name (e.g., "Taffy Files")
            std::string extension;   // Extension (e.g., "*.taf")
        };

        // Show file dialog and return selected path
        static std::string show(Type type, const std::string& title,
                               const std::vector<Filter>& filters = {},
                               const std::string& defaultPath = "");

        // Convenience methods for common model editor operations
        static std::string showOpenDialog(const std::string& defaultPath = "bin/assets/");
        static std::string showSaveDialog(const std::string& defaultPath = "bin/assets/");

        // Check if a file exists
        static bool fileExists(const std::string& path);

        // Get file extension
        static std::string getFileExtension(const std::string& path);

        // Platform-specific implementations
        #ifdef _WIN32
        static std::string showWindowsDialog(Type type, const std::string& title,
                                           const std::vector<Filter>& filters,
                                           const std::string& defaultPath);
        #endif

        #ifdef __linux__
        static std::string showLinuxDialog(Type type, const std::string& title,
                                         const std::vector<Filter>& filters,
                                         const std::string& defaultPath);
        #endif

    private:
        // Fallback command-line interface for file selection
        static std::string showConsoleDialog(Type type, const std::string& title,
                                           const std::vector<Filter>& filters,
                                           const std::string& defaultPath);
        
        // List files in directory
        static std::vector<std::string> listFiles(const std::string& directory,
                                                 const std::vector<Filter>& filters);
    };

} // namespace tremor::editor