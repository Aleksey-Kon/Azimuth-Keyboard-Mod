#define NOMINMAX
#include "Preset.h"
#include "Settings.h"
#include <iostream>
#include <set>

void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

int main(int argc, char** argv)
{
    try
    {
        const std::filesystem::path root = argc > 1 ? argv[1] : "source/presets";
        int count = 0;
        const auto settings = std::filesystem::absolute(root).parent_path() / ("settings-test-" + std::to_string(GetCurrentProcessId()) + ".ini");
        struct Cleanup
        {
            std::filesystem::path Path;
            ~Cleanup() { std::error_code ignored; std::filesystem::remove(Path, ignored); }
        } cleanup{settings};
        Require(gamepadmod::LoadSelectedPreset(settings) == 0 && !std::filesystem::exists(settings),
            "Missing settings must default to Off without creating or overwriting a file");
        for (int index = 0; index <= 3; ++index)
        {
            gamepadmod::SaveSelectedPreset(settings, index);
            Require(gamepadmod::LoadSelectedPreset(settings) == index, "Every preset including Off must survive save/load");
        }
        for (const wchar_t* invalid : {L"-1", L"4", L"invalid", L"1trailing", L"", L"99999999999999999999999999999999999"})
        {
            Require(WritePrivateProfileStringW(L"mod", L"preset", invalid, settings.c_str()) != 0, "Cannot prepare invalid settings");
            Require(gamepadmod::LoadSelectedPreset(settings) == 0, "Invalid saved index must safely fall back to Off");
        }
        bool rejected = false;
        try { gamepadmod::SaveSelectedPreset(settings, 4); }
        catch (const std::out_of_range&) { rejected = true; }
        Require(rejected, "Invalid selection must not be saved");
        rejected = false;
        try { gamepadmod::SaveSelectedPreset(settings / L"missing" / L"settings.ini", 1); }
        catch (const std::runtime_error&) { rejected = true; }
        Require(rejected, "Save failures must be reported instead of silently changing runtime selection");
        for (const auto* name : {"01-wasd.ini", "02-arrows.ini", "03-smooth.ini"})
        {
            auto preset = gamepadmod::LoadPreset(root / name);
            std::set<int> held;
            auto down = [&](int key) { return held.count(key) != 0; };
            double steering = 0;
            auto state = gamepadmod::Evaluate(preset, down, 0.016, steering);
            Require(state.Buttons == 0 && state.LeftX == 0 && state.RightTrigger == 0, "Idle state must be neutral");
            held.insert(preset.RightTrigger);
            state = gamepadmod::Evaluate(preset, down, 0.016, steering);
            Require(state.RightTrigger == preset.TriggerScale && state.LeftTrigger == 0, "Throttle must activate only right trigger");
            held.clear(); held.insert(preset.Axes[0]); held.insert(preset.Axes[1]);
            state = gamepadmod::Evaluate(preset, down, 0.016, steering);
            Require(state.LeftX == 0, "Opposite directions must cancel");
            held.clear(); held.insert(preset.Buttons[2]);
            state = gamepadmod::Evaluate(preset, down, 0.016, steering);
            Require((state.Buttons & 4) != 0, "A must use Windows.Gaming.Input bit 2");
            held.clear(); held.insert(preset.Axes[1]);
            steering = 0;
            state = gamepadmod::Evaluate(preset, down, 0.016, steering);
            if (preset.SteeringSeconds > 0)
            {
                Require(state.LeftX > 0 && state.LeftX < preset.StickScale, "Smooth steering must ramp up");
                held.clear();
                state = gamepadmod::Evaluate(preset, down, 0.1, steering);
                Require(state.LeftX == 0, "Released steering must return to center");
            }
            else Require(state.LeftX == preset.StickScale, "Digital steering must reach configured maximum");
            ++count;
        }
        try { gamepadmod::KeyCode("UNKNOWN"); throw std::logic_error("Bad key accepted"); }
        catch (const std::runtime_error&) {}
        auto preset = gamepadmod::LoadPreset(root / "01-wasd.ini");
        preset.Axes[3] = gamepadmod::KeyCode("Z");
        double steering = 0;
        const auto diagonal = gamepadmod::Evaluate(preset, [&](int key) { return key == preset.Axes[1] || key == preset.Axes[3]; }, 0.1, steering);
        Require(std::hypot(diagonal.LeftX, diagonal.LeftY) <= 1.000001, "Diagonal must stay inside stick range");
        auto smooth = gamepadmod::LoadPreset(root / "03-smooth.ini");
        gamepadmod::InputAccumulator input;
        bool holding = true;
        auto heldRight = [&](int key) { return holding && key == smooth.Axes[1]; };
        for (uint64_t time = 1000; time <= 2000; time += 16) input.Read(smooth, heldRight, time);
        Require(input.Steering == smooth.StickScale, "Regression setup must reach full steering");
        // Simulate a focus/menu notification while the game is no longer polling.
        input.Clear();
        holding = false;
        const auto afterFocus = input.Read(smooth, heldRight, 10000);
        Require(afterFocus.LeftX == 0 && input.Steering == 0, "Focus/menu reset must clear stale steering even without background polling");
        std::cout << "PASS: saved selection round trips, invalid/missing settings and save failures; " << count << " presets, neutral input, triggers, button masks, opposing directions, smoothing, key validation, diagonal normalization, focus reset without background polling\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
