#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>

namespace gamepadmod
{
    inline std::string Trim(std::string value)
    {
        const auto first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return {};
        return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
    }

    inline int KeyCode(std::string key)
    {
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        if (key.size() == 1 && ((key[0] >= 'A' && key[0] <= 'Z') || (key[0] >= '0' && key[0] <= '9'))) return key[0];
        const std::map<std::string, int> names = {
            {"NONE", 0}, {"BACKSPACE", 8}, {"TAB", 9}, {"ENTER", 13}, {"SHIFT", 16},
            {"CTRL", 17}, {"ALT", 18}, {"ESC", 27}, {"SPACE", 32}, {"PAGEUP", 33},
            {"PAGEDOWN", 34}, {"END", 35}, {"HOME", 36}, {"LEFT", 37}, {"UP", 38},
            {"RIGHT", 39}, {"DOWN", 40}, {"INSERT", 45}, {"DELETE", 46},
            {"LSHIFT", 160}, {"RSHIFT", 161}, {"LCTRL", 162}, {"RCTRL", 163}
        };
        if (const auto it = names.find(key); it != names.end()) return it->second;
        if (key.size() == 7 && key.substr(0, 6) == "NUMPAD" && key[6] >= '0' && key[6] <= '9') return 96 + key[6] - '0';
        throw std::runtime_error("Unknown key: " + key);
    }

    struct Preset
    {
        std::string Name;
        std::array<int, 14> Buttons{};
        std::array<int, 8> Axes{};
        int LeftTrigger = 0;
        int RightTrigger = 0;
        double StickScale = 1.0;
        double TriggerScale = 1.0;
        double SteeringSeconds = 0.0;
    };

    inline constexpr std::array<const char*, 14> ButtonNames = {
        "menu", "view", "a", "b", "x", "y", "dpad_up", "dpad_down", "dpad_left", "dpad_right",
        "left_shoulder", "right_shoulder", "left_thumb", "right_thumb"
    };
    inline constexpr std::array<const char*, 8> AxisNames = {
        "left_left", "left_right", "left_down", "left_up", "right_left", "right_right", "right_down", "right_up"
    };

    inline Preset LoadPreset(const std::filesystem::path& path)
    {
        if (std::filesystem::file_size(path) > 65536) throw std::runtime_error("Preset is larger than 64 KB");
        std::ifstream input(path);
        if (!input) throw std::runtime_error("Cannot open preset: " + path.filename().string());
        std::map<std::string, std::string> values;
        std::string line, section;
        while (std::getline(input, line))
        {
            if (line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);
            line = Trim(line.substr(0, line.find_first_of(";#")));
            if (line.empty()) continue;
            if (line.front() == '[' && line.back() == ']') { section = line.substr(1, line.size() - 2); continue; }
            const auto equals = line.find('=');
            if (equals == std::string::npos) throw std::runtime_error("Expected key=value: " + line);
            const auto key = section + "." + Trim(line.substr(0, equals));
            if (!values.emplace(key, Trim(line.substr(equals + 1))).second) throw std::runtime_error("Duplicate setting: " + key);
        }
        auto take = [&](const std::string& key) {
            auto node = values.extract(key);
            if (node.empty()) throw std::runtime_error("Missing setting: " + key);
            return node.mapped();
        };
        auto number = [&](const std::string& key, double minimum, double maximum) {
            const auto text = take(key);
            size_t consumed = 0;
            const double value = std::stod(text, &consumed);
            if (consumed != text.size() || !std::isfinite(value) || value < minimum || value > maximum)
                throw std::runtime_error("Invalid range for " + key);
            return value;
        };
        Preset preset;
        preset.Name = take("preset.name");
        if (preset.Name.empty() || preset.Name.size() > 80) throw std::runtime_error("Preset name must be 1..80 bytes");
        for (size_t i = 0; i < ButtonNames.size(); ++i) preset.Buttons[i] = KeyCode(take(std::string("buttons.") + ButtonNames[i]));
        for (size_t i = 0; i < AxisNames.size(); ++i) preset.Axes[i] = KeyCode(take(std::string("axes.") + AxisNames[i]));
        preset.LeftTrigger = KeyCode(take("triggers.left"));
        preset.RightTrigger = KeyCode(take("triggers.right"));
        preset.StickScale = number("analog.stick_scale", 0.05, 1.0);
        preset.TriggerScale = number("analog.trigger_scale", 0.05, 1.0);
        preset.SteeringSeconds = number("analog.steering_seconds", 0.0, 2.0);
        if (!values.empty()) throw std::runtime_error("Unknown setting: " + values.begin()->first);
        return preset;
    }

    struct Reading
    {
        uint32_t Buttons = 0;
        double LeftX = 0, LeftY = 0, RightX = 0, RightY = 0, LeftTrigger = 0, RightTrigger = 0;
    };

    template<typename IsDown>
    Reading Evaluate(const Preset& preset, IsDown down, double deltaSeconds, double& steering)
    {
        Reading reading;
        auto pressed = [&](int key) { return key != 0 && down(key); };
        for (size_t i = 0; i < preset.Buttons.size(); ++i)
            if (pressed(preset.Buttons[i])) reading.Buttons |= 1u << i;
        auto axis = [&](int offset) { return (static_cast<int>(pressed(preset.Axes[offset + 1])) - static_cast<int>(pressed(preset.Axes[offset]))) * preset.StickScale; };
        const double target = axis(0);
        const double step = preset.SteeringSeconds > 0 ? std::clamp(deltaSeconds, 0.0, 0.1) / preset.SteeringSeconds : 2.0;
        steering += std::clamp(target - steering, -step, step);
        reading.LeftX = steering;
        reading.LeftY = axis(2);
        reading.RightX = axis(4);
        reading.RightY = axis(6);
        auto normalize = [&](double& x, double& y) {
            const double length = std::hypot(x, y);
            if (length > preset.StickScale) { x *= preset.StickScale / length; y *= preset.StickScale / length; }
        };
        normalize(reading.LeftX, reading.LeftY);
        normalize(reading.RightX, reading.RightY);
        reading.LeftTrigger = pressed(preset.LeftTrigger) ? preset.TriggerScale : 0;
        reading.RightTrigger = pressed(preset.RightTrigger) ? preset.TriggerScale : 0;
        return reading;
    }

    struct InputAccumulator
    {
        double Steering = 0;
        uint64_t PreviousTime = 0;

        void Clear() noexcept
        {
            Steering = 0;
            PreviousTime = 0;
        }

        template<typename IsDown>
        Reading Read(const Preset& preset, IsDown down, uint64_t now)
        {
            const double delta = PreviousTime == 0 || now < PreviousTime ? 0.0 : (now - PreviousTime) / 1000.0;
            PreviousTime = now;
            return Evaluate(preset, down, delta, Steering);
        }
    };
}
