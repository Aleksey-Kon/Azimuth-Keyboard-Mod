#pragma once
#include <windows.h>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace gamepadmod
{
    inline int LoadSelectedPreset(const std::filesystem::path& path)
    {
        wchar_t value[32]{};
        const DWORD length = GetPrivateProfileStringW(L"mod", L"preset", L"0", value, 32, path.c_str());
        return length == 1 && value[0] >= L'0' && value[0] <= L'3' ? value[0] - L'0' : 0;
    }

    inline void SaveSelectedPreset(const std::filesystem::path& path, int index)
    {
        if (index < 0 || index > 3) throw std::out_of_range("Invalid preset selection");
        const auto value = std::to_wstring(index);
        if (!WritePrivateProfileStringW(L"mod", L"preset", value.c_str(), path.c_str()))
            throw std::runtime_error("Cannot save selected preset to settings.ini");
    }
}
