#pragma once

#include "../Module.hpp"

#include <cstdint>

#include <pl/Input.hpp>


class ZoomModule : public Module {
public:

    // =========================================================================
    // ZOOM
    // =========================================================================

    float m_defaultZoomFov =
        20.58f;

    float m_targetZoomFov =
        20.58f;

    float m_currentFov =
        90.0f;

    float m_baseFov =
        90.0f;

    float m_animSpeed =
        0.25f;


    // =========================================================================
    // SENSITIVITY
    // =========================================================================

    bool m_lowSens =
        true;

    float m_lowSensStrength =
        0.9f;


    // =========================================================================
    // HIDE HAND
    // =========================================================================

    bool m_hideHand =
        true;


    // =========================================================================
    // ZOOM STATE
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
    // RAW TOUCH TRACKING
    // =========================================================================

    //
    // Pointer yang benar-benar dimiliki tombol Zoom.
    //

    int32_t m_zoomPointerId =
        -1;


    //
    // Posisi Y terakhir dari GameActivityMotionEvent.
    //

    float m_lastRawY =
        0.0f;

    bool m_hasLastRawY =
        false;


    // =========================================================================
    // CANDIDATE POINTER
    // =========================================================================

    //
    // TouchEvent DOWN tetap digunakan, tetapi HANYA untuk mengetahui pointerId.
    //
    // MOVE tidak lagi menggunakan pl::input::TouchEvent.
    //

    int32_t m_candidatePointerId =
        -1;

    float m_candidateY =
        0.0f;

    uint64_t m_candidateTimeMs =
        0;

    bool m_candidateValid =
        false;


    //
    // Digunakan jika ButtonBuilder::Down tiba sebelum TouchEvent DOWN.
    //

    bool m_waitingForZoomPointer =
        false;


    // =========================================================================
    // LIFECYCLE
    // =========================================================================

    ZoomModule();

    ~ZoomModule() override;

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
    // TOUCH CANDIDATE
    // =========================================================================

    bool onTouchEvent(
        const pl::input::TouchEvent& ev
    );


    // =========================================================================
    // BUTTON POINTER
    // =========================================================================

    void beginButtonZoom();

    void endButtonZoom();

    void captureZoomPointer(
        int32_t pointerId,
        float initialY
    );

    void resetZoomPointer();


    // =========================================================================
    // RAW MOTION
    // =========================================================================

    void processRawMotion(
        const void* motionEvent
    );


    // =========================================================================
    // DRAG
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

    bool m_fovHooked =
        false;

    bool m_turnDeltaHooked =
        false;

    bool m_hideHandHooked =
        false;

    bool m_touchHooked =
        false;

    bool m_rawMotionHooked =
        false;

    bool m_buttonRegistered =
        false;
};
