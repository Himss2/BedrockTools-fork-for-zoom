#pragma once

#include "../Module.hpp"

#include <cstdint>
#include <pl/Input.hpp>

class ZoomModule : public Module {
public:
    // Zoom behaviour
    float m_defaultZoomFov  = 20.58f;
    float m_targetZoomFov   = 20.58f;
    float m_currentFov      = 90.0f;
    float m_baseFov         = 90.0f;
    float m_animSpeed       = 0.25f;

    // Camera behaviour while zooming
    bool  m_lowSens         = true;
    float m_lowSensStrength = 0.9f;
    bool  m_hideHand        = true;

    // Manual Zoom button settings. These are intentionally owned by
    // BedrockTools again; ButtonBuilder is no longer used.
    bool  m_overlayToggle   = true;
    float m_posX            = 60.0f;
    float m_posY            = 120.0f;
    float m_scale           = 1.0f;
    float m_opacity         = 0.85f;

    // Runtime zoom state
    bool m_animationFinished = true;
    bool m_isFirstTime       = true;
    bool m_keyZooming        = false;
    bool m_buttonZooming     = false;

    // The exact Android pointer that pressed the manual Zoom button.
    int32_t m_trackedPointerId = -1;
    float   m_lastRawY          = 0.0f;
    bool    m_hasLastRawY       = false;

    ZoomModule();
    ~ZoomModule() override;

    void onInit() override;
    void onEnable() override;
    void onDisable() override;
    void onFrame() override;
    void onKeybindEvent(const std::string& key, bool isDown) override;
    void loadConfig(const nlohmann::json& j) override;
    void saveConfig(nlohmann::json& j) override;

    bool isZoomActive();

    // Manual button UI
    bool contains(float x, float y) const;
    void renderButton();
    void clearButtonOverlay();
    bool loadButtonImages();

    // Touch/raw-motion path
    bool onTouchEvent(const pl::input::TouchEvent& ev);
    void processRawMotion(const void* motionEvent);
    void updateDrag(float deltaY);
    void resetButtonPointer();

private:
    bool m_fovHooked       = false;
    bool m_turnDeltaHooked = false;
    bool m_hideHandHooked  = false;
    bool m_touchHooked     = false;
    bool m_rawMotionHooked = false;
    bool m_imagesLoaded    = false;
    bool m_overlayDrawn    = false;
};
