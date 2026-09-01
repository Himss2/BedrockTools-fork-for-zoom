#pragma once

#include "../Module.hpp"

#include <pl/Input.hpp>


class ZoomModule : public Module {
public:

    // =========================================================================
    // ZOOM SETTINGS
    // =========================================================================

    float m_defaultZoomFov  = 20.58f;
    float m_targetZoomFov   = 20.58f;

    float m_currentFov      = 90.0f;
    float m_baseFov         = 90.0f;

    float m_animSpeed       = 0.25f;


    // =========================================================================
    // LOW SENSITIVITY
    // =========================================================================

    bool  m_lowSens         = true;
    float m_lowSensStrength = 0.9f;


    // =========================================================================
    // HIDE HAND
    // =========================================================================

    bool m_hideHand =
        true;


    // =========================================================================
    // ZOOM RUNTIME STATE
    // =========================================================================

    bool m_animationFinished =
        true;

    bool m_isFirstTime =
        true;


    bool m_keyZooming =
        false;

    bool m_buttonZooming =
        false;


    // =========================================================================
    // TOUCH / SWIPE TRACKING
    // =========================================================================

    //
    // Pointer yang sekarang menjadi milik tombol Zoom.
    //
    // Nilai -1 berarti tidak ada pointer yang sedang ditrack.
    //

    int m_trackedPointerId =
        -1;


    //
    // Y dari event MOVE sebelumnya.
    //

    float m_lastTouchY =
        0.0f;


    // =========================================================================
    // PENDING BUTTON DOWN
    // =========================================================================

    //
    // PreloaderInput menerima ACTION_DOWN SEBELUM ButtonBuilder.
    //
    // Karena callback ButtonBuilder::Down tidak memberikan pointerId,
    // kita cache pointer dari TouchEvent terlebih dahulu.
    //
    // Setelah ButtonBuilder mengirim event Down,
    // pointer ini dijadikan m_trackedPointerId.
    //

    int m_pendingPointerId =
        -1;

    float m_pendingTouchY =
        0.0f;

    bool m_pendingTouchValid =
        false;


    // =========================================================================
    // CONSTRUCTOR / DESTRUCTOR
    // =========================================================================

    ZoomModule();

    ~ZoomModule() override;


    // =========================================================================
    // MODULE LIFECYCLE
    // =========================================================================

    void onInit() override;

    void onEnable() override;

    void onDisable() override;


    // =========================================================================
    // KEYBIND
    // =========================================================================

    void onKeybindEvent(
        const std::string& key,
        bool isDown
    ) override;


    // =========================================================================
    // CONFIG
    // =========================================================================

    void loadConfig(
        const nlohmann::json& j
    ) override;


    void saveConfig(
        nlohmann::json& j
    ) override;


    // =========================================================================
    // ZOOM STATE
    // =========================================================================

    bool isZoomActive();


    // =========================================================================
    // TOUCH
    // =========================================================================

    bool onTouchEvent(
        const pl::input::TouchEvent& ev
    );


    // =========================================================================
    // SWIPE
    // =========================================================================

    void updateDrag(
        float deltaY
    );


    // =========================================================================
    // GENERIC SCROLL
    // =========================================================================

    void onScroll(
        float scrollDelta
    );


private:

    // =========================================================================
    // HOOK STATE
    // =========================================================================

    bool m_fovHooked =
        false;

    bool m_turnDeltaHooked =
        false;

    bool m_hideHandHooked =
        false;

    bool m_touchHooked =
        false;

    bool m_buttonRegistered =
        false;
};
