#pragma once

#include "ui_renderer.h"
#include <array>
#include <chrono>

namespace tremor::gfx {

    class SequencerUI {
    public:
        SequencerUI(UIRenderer* uiRenderer);
        ~SequencerUI();

        void initialize();
        void update();
        void reset();
        
        bool isPlaying() const { return m_playing; }
        void setPlaying(bool playing) { m_playing = playing; }
        void togglePlaying() { m_playing = !m_playing; }
        
        void setBPM(float bpm);
        float getBPM() const { return m_bpm; }
        
        const std::array<bool, 16>& getPattern() const { return m_pattern; }
        bool getStep(int index) const { return index >= 0 && index < 16 ? m_pattern[index] : false; }
        
        void onStepTriggered(std::function<void(int)> callback) { m_stepCallback = callback; }
        
    private:
        UIRenderer* m_uiRenderer;
        
        std::array<uint32_t, 16> m_stepButtonIds;
        std::array<bool, 16> m_pattern;
        uint32_t m_playButtonId;
        
        bool m_playing = false;
        int m_currentStep = 0;
        float m_bpm = 120.0f;
        
        std::chrono::steady_clock::time_point m_lastStepTime;
        std::chrono::milliseconds m_stepDuration;
        
        std::function<void(int)> m_stepCallback;
        
        void updateStepDuration();
        void updateButtonStates();
        void createUI();
        
        static constexpr float BUTTON_SIZE = 40.0f;
        static constexpr float BUTTON_SPACING = 5.0f;
        static constexpr float SEQUENCER_Y = 300.0f;
        static constexpr float PLAY_BUTTON_Y = 400.0f;
        
        static constexpr uint32_t COLOR_STEP_OFF = 0x404040FF;
        static constexpr uint32_t COLOR_STEP_ON = 0x00FF00FF;
        static constexpr uint32_t COLOR_STEP_CURRENT = 0xFF8800FF;
        static constexpr uint32_t COLOR_PLAY = 0x00AA00FF;
        static constexpr uint32_t COLOR_PAUSE = 0xAA0000FF;
    };

} // namespace tremor::gfx