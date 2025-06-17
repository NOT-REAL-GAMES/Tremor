#include "sequencer_ui.h"
#include "../main.h"

namespace tremor::gfx {

    SequencerUI::SequencerUI(UIRenderer* uiRenderer) 
        : m_uiRenderer(uiRenderer)
        , m_pattern{false}
        , m_stepButtonIds{0}
        , m_playButtonId(0) {
        updateStepDuration();
        m_lastStepTime = std::chrono::steady_clock::now();
    }

    SequencerUI::~SequencerUI() {
        for (auto id : m_stepButtonIds) {
            if (id != 0) {
                m_uiRenderer->removeElement(id);
            }
        }
        if (m_playButtonId != 0) {
            m_uiRenderer->removeElement(m_playButtonId);
        }
    }

    void SequencerUI::initialize() {
        createUI();
        updateButtonStates();
    }

    void SequencerUI::createUI() {
        float startX = 50.0f;
        
        for (int i = 0; i < 16; ++i) {
            float x = startX + i * (BUTTON_SIZE + BUTTON_SPACING);
            
            if (i > 0 && i % 4 == 0) {
                x += BUTTON_SPACING * 2;
            }
            
            m_stepButtonIds[i] = m_uiRenderer->addButton(
                std::to_string(i + 1),
                glm::vec2(x, SEQUENCER_Y),
                glm::vec2(BUTTON_SIZE, BUTTON_SIZE),
                [this, i]() {
                    m_pattern[i] = !m_pattern[i];
                    updateButtonStates();
                    Logger::get().info("Step {} toggled: {}", i + 1, m_pattern[i] ? "ON" : "OFF");
                }
            );
        }
        
        m_playButtonId = m_uiRenderer->addButton(
            m_playing ? "Pause" : "Play",
            glm::vec2(startX, PLAY_BUTTON_Y),
            glm::vec2(100, 40),
            [this]() {
                togglePlaying();
                if (m_playing) {
                    m_lastStepTime = std::chrono::steady_clock::now();
                    Logger::get().info("Sequencer started");
                } else {
                    Logger::get().info("Sequencer paused");
                }
                updateButtonStates();
            }
        );
        
        m_uiRenderer->addLabel("BPM: 120", glm::vec2(startX + 120, PLAY_BUTTON_Y + 10));
    }

    void SequencerUI::update() {
        if (!m_playing) return;
        
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastStepTime);
        
        if (elapsed >= m_stepDuration) {
            m_currentStep = (m_currentStep + 1) % 16;
            m_lastStepTime = now;
            
            if (m_pattern[m_currentStep] && m_stepCallback) {
                m_stepCallback(m_currentStep);
            }
            
            updateButtonStates();
        }
    }

    void SequencerUI::updateButtonStates() {
        for (int i = 0; i < 16; ++i) {
            auto* button = static_cast<UIButton*>(m_uiRenderer->getElement(m_stepButtonIds[i]));
            if (button) {
                if (m_playing && i == m_currentStep) {
                    button->backgroundColor = COLOR_STEP_CURRENT;
                } else if (m_pattern[i]) {
                    button->backgroundColor = COLOR_STEP_ON;
                } else {
                    button->backgroundColor = COLOR_STEP_OFF;
                }
            }
        }
        
        auto* playButton = static_cast<UIButton*>(m_uiRenderer->getElement(m_playButtonId));
        if (playButton) {
            playButton->text = m_playing ? "Pause" : "Play";
            playButton->backgroundColor = m_playing ? COLOR_PAUSE : COLOR_PLAY;
        }
    }

    void SequencerUI::setBPM(float bpm) {
        m_bpm = std::max(60.0f, std::min(300.0f, bpm));
        updateStepDuration();
    }

    void SequencerUI::updateStepDuration() {
        float beatsPerSecond = m_bpm / 60.0f;
        float stepsPerSecond = beatsPerSecond * 4;
        m_stepDuration = std::chrono::milliseconds(static_cast<int>(1000.0f / stepsPerSecond));
    }

    void SequencerUI::reset() {
        m_playing = false;
        m_currentStep = 0;
        m_pattern.fill(false);
        updateButtonStates();
    }

} // namespace tremor::gfx