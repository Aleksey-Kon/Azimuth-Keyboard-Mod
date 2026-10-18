#pragma once
#include <windows.h>
#include <filesystem>
#include <stdexcept>
#include <string>
#include "Settings.h"

// Test-only DLL/config copies keep integration tests away from user settings.
struct TestProfile
{
    std::filesystem::path Folder;
    std::filesystem::path Dll;

    explicit TestProfile(const std::filesystem::path& source)
    {
        const auto original = std::filesystem::absolute(source);
        Folder = original.parent_path() / L"tests" / (L"profile-" + std::to_wstring(GetCurrentProcessId()));
        if (!std::filesystem::create_directory(Folder)) throw std::runtime_error("Test profile already exists");
        try
        {
            Dll = Folder / original.filename();
            std::filesystem::copy_file(original, Dll);
            std::filesystem::copy(original.parent_path() / L"presets", Folder / L"presets", std::filesystem::copy_options::recursive);
            gamepadmod::SaveSelectedPreset(Folder / L"settings.ini", 0);
        }
        catch (...)
        {
            std::filesystem::remove_all(Folder);
            throw;
        }
    }

    ~TestProfile()
    {
        std::error_code ignored;
        std::filesystem::remove_all(Folder, ignored);
    }
    TestProfile(const TestProfile&) = delete;
    TestProfile& operator=(const TestProfile&) = delete;
};
