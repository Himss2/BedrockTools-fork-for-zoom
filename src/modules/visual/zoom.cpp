#include "zoom.hpp"

#include "core/memory/Hooks.hpp"

#include <bedrocktools/memory/Signatures.hpp>
#include <bedrocktools/sdk/Memory.hpp>

#include <pl/Input.hpp>
#include <pl/ModMenu.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>


// =============================================================================
// GLOBAL INSTANCE
// =============================================================================

static ZoomModule* g_zoomMod =
    nullptr;


// =============================================================================
// BUTTON
// =============================================================================

namespace {

constexpr std::string_view kZoomButtonId =
    "bedrocktools.zoom.button";

}


// =============================================================================
// ANDROID TOUCH ACTIONS
// =============================================================================

namespace {

constexpr int kActionDown =
    0;

constexpr int kActionUp =
    1;

constexpr int kActionMove =
    2;

constexpr int kActionCancel =
    3;

constexpr int kActionPointerDown =
    5;

constexpr int kActionPointerUp =
    6;

}


// =============================================================================
// RAW GAMEACTIVITY MOTION EVENT LAYOUT
// =============================================================================
//
// Dikonfirmasi dari:
// Minecraft Bedrock Android 1.26.45.1
//
// GameActivityMotionEvent_fromJava
//
// Output GameActivityMotionEvent:
//
// +0x38 = pointerCount
// +0x3C = pointer[0]
// stride pointer = 0xD0
//
// Pointer:
//
// +0x00 = pointerId
// +0x04 = toolType
// +0x08 = AXIS_X
// +0x0C = AXIS_Y
//
// Kita sengaja hanya membaca field minimum yang dibutuhkan.
// Tidak membuat struct penuh agar implementation tetap ringan.
//

namespace raw_motion {

constexpr std::size_t kPointerCountOffset =
    0x38;

constexpr std::size_t kPointerBaseOffset =
    0x3C;

constexpr std::size_t kPointerStride =
    0xD0;

constexpr std::size_t kPointerIdOffset =
    0x00;

constexpr std::size_t kPointerYOffset =
    0x0C;

constexpr int32_t kMaxReasonablePointers =
    16;


// -----------------------------------------------------------------------------
// Helper unaligned-safe.
//
// Walaupun field ini 4-byte aligned pada layout sekarang,
// memcpy menghindari strict-aliasing issue.
// -----------------------------------------------------------------------------

template <typename T>
T readValue(
    const std::uint8_t* base,
    std::size_t offset
) {

    T value {};

    std::memcpy(
        &value,
        base + offset,
        sizeof(T)
    );

    return value;
}

}


// =============================================================================
// TIME HELPER
// =============================================================================

static uint64_t nowMs() {

    using namespace std::chrono;


    return static_cast<uint64_t>(

        duration_cast<milliseconds>(

            steady_clock::now()
                .time_since_epoch()

        ).count()

    );
}


// =============================================================================
// FOV HOOK
// =============================================================================

static float (*_getFov_orig)(
    void*,
    float,
    int
) = nullptr;


static float _getFov_zoom_hook(
    void* _this,
    float a,
    int enableVariableFOV
) {

    float originalFov =
        0.0f;


    if (_getFov_orig) {

        originalFov =
            _getFov_orig(
                _this,
                a,
                enableVariableFOV
            );
    }


    if (!g_zoomMod) {

        return originalFov;
    }


    // =========================================================================
    // SPECIAL FOV FILTER
    // =========================================================================
    //
    // Belum kita ubah pada tahap raw input ini.
    //

    if (
        originalFov == 70.0f ||
        originalFov == 60.0f
    ) {

        return originalFov;
    }


    // =========================================================================
    // BASE FOV
    // =========================================================================

    g_zoomMod->m_baseFov =
        originalFov;


    // =========================================================================
    // FIRST FRAME
    // =========================================================================

    if (g_zoomMod->m_isFirstTime) {

        g_zoomMod->m_currentFov =
            originalFov;


        g_zoomMod->m_isFirstTime =
            false;
    }


    // =========================================================================
    // ZOOM ACTIVE
    // =========================================================================

    if (g_zoomMod->isZoomActive()) {

        g_zoomMod->m_animationFinished =
            false;


        g_zoomMod->m_currentFov =

            std::lerp(

                g_zoomMod->m_currentFov,

                g_zoomMod->m_targetZoomFov,

                g_zoomMod->m_animSpeed

            );


        return
            g_zoomMod->m_currentFov;
    }


    // =========================================================================
    // RETURN ANIMATION
    // =========================================================================

    if (!g_zoomMod->m_animationFinished) {

        g_zoomMod->m_currentFov =

            std::lerp(

                g_zoomMod->m_currentFov,

                originalFov,

                g_zoomMod->m_animSpeed

            );


        if (

            std::abs(

                g_zoomMod->m_currentFov -
                originalFov

            ) < 0.2f

        ) {

            g_zoomMod->m_animationFinished =
                true;


            g_zoomMod->m_currentFov =
                originalFov;
        }


        return
            g_zoomMod->m_currentFov;
    }


    return
        originalFov;
}


// =============================================================================
// CAMERA SENSITIVITY
// =============================================================================

struct Vec2 {

    float x;
    float y;

};


static void (*_applyTurnDelta_orig)(
    void*,
    Vec2*
) = nullptr;


static void _applyTurnDelta_hook(
    void* _this,
    Vec2* rotationDelta
) {

    if (

        g_zoomMod &&

        rotationDelta &&

        (
            g_zoomMod->isZoomActive() ||
            !g_zoomMod->m_animationFinished
        ) &&

        g_zoomMod->m_lowSens &&

        g_zoomMod->m_baseFov > 0.1f

    ) {

        float zoomRatio =

            g_zoomMod->m_currentFov /
            g_zoomMod->m_baseFov;


        float strength =
            g_zoomMod->m_lowSensStrength;


        float multiplier =

            1.0f -

            (
                1.0f -
                zoomRatio
            ) *

            strength;


        multiplier =

            std::clamp(

                multiplier,

                0.01f,

                1.0f

            );


        Vec2 modifiedDelta {

            rotationDelta->x *
                multiplier,

            rotationDelta->y *
                multiplier

        };


        if (_applyTurnDelta_orig) {

            _applyTurnDelta_orig(
                _this,
                &modifiedDelta
            );
        }

    } else {

        if (_applyTurnDelta_orig) {

            _applyTurnDelta_orig(
                _this,
                rotationDelta
            );
        }
    }
}


// =============================================================================
// HIDE HAND
// =============================================================================

static bool (*_getHideItemInHand_orig)(
    void*
) = nullptr;


static bool _getHideItemInHand_hook(
    void* _this
) {

    bool hide =
        false;


    if (_getHideItemInHand_orig) {

        hide =

            _getHideItemInHand_orig(
                _this
            );
    }


    if (

        g_zoomMod &&

        g_zoomMod->isZoomActive() &&

        g_zoomMod->m_hideHand

    ) {

        return true;
    }


    return hide;
}


// =============================================================================
// RAW GameActivityMotionEvent_fromJava
// =============================================================================
//
// ABI hasil RE Minecraft 1.26.45.1:
//
// x0 = JNIEnv*
// x1 = Java MotionEvent object
// x2 = GameActivityMotionEvent* output
// w3 = pointerCount
// x4/w4 = history size / related count
//
// Kita tidak membutuhkan JNI type secara langsung,
// sehingga gunakan void* untuk argumen opaque.
//

using GameActivityMotionEventFromJavaFn =

    void (*)(
        void* env,
        void* javaMotionEvent,
        void* outputEvent,
        int32_t pointerCount,
        int32_t historySize
    );


static GameActivityMotionEventFromJavaFn
    _gameActivityMotionEventFromJava_orig =
        nullptr;


// -----------------------------------------------------------------------------
// Hook
// -----------------------------------------------------------------------------
//
// ORIGINAL DIPANGGIL DULU.
//
// Ini penting karena GameActivityMotionEvent_fromJava adalah fungsi yang
// mengisi outputEvent.
//
// Baru setelah selesai kita membaca pointer array.
//

static void _gameActivityMotionEventFromJava_hook(
    void* env,
    void* javaMotionEvent,
    void* outputEvent,
    int32_t pointerCount,
    int32_t historySize
) {

    if (_gameActivityMotionEventFromJava_orig) {

        _gameActivityMotionEventFromJava_orig(

            env,
            javaMotionEvent,
            outputEvent,
            pointerCount,
            historySize

        );
    }


    // =========================================================================
    // FAST EXIT
    // =========================================================================
    //
    // Jalur normal gameplay hanya melewati beberapa branch.
    //

    if (!g_zoomMod) {

        return;
    }


    if (!g_zoomMod->enabled) {

        return;
    }


    if (!g_zoomMod->m_buttonZooming) {

        return;
    }


    if (g_zoomMod->m_zoomPointerId < 0) {

        return;
    }


    if (!outputEvent) {

        return;
    }


    g_zoomMod->processRawMotion(
        outputEvent
    );
}


// =============================================================================
// PL TOUCH CALLBACK
// =============================================================================
//
// TouchEvent launcher sekarang HANYA dipakai untuk:
//
//     DOWN
//     POINTER_DOWN
//     UP
//     POINTER_UP
//     CANCEL
//
// ACTION_MOVE sama sekali tidak dipakai untuk menghitung zoom.
//

static bool _onTouchBridge(
    const pl::input::TouchEvent& ev
) {

    if (!g_zoomMod) {

        return false;
    }


    return
        g_zoomMod->onTouchEvent(
            ev
        );
}


// =============================================================================
// BUTTON BUILDER EVENT
// =============================================================================

static void _onZoomButtonEvent(
    std::string_view buttonId,
    pl::modmenu::ButtonEvent event,
    float value
) {

    if (!g_zoomMod) {

        return;
    }


    if (buttonId != kZoomButtonId) {

        return;
    }


    switch (event) {

        // =====================================================================
        // HOLD START
        // =====================================================================

        case pl::modmenu::ButtonEvent::Down:
        {

            g_zoomMod->beginButtonZoom();

            break;
        }


        // =====================================================================
        // HOLD END
        // =====================================================================

        case pl::modmenu::ButtonEvent::Up:
        {

            g_zoomMod->endButtonZoom();

            break;
        }


        // =====================================================================
        // MOUSE / TRACKPAD WHEEL
        // =====================================================================

        case pl::modmenu::ButtonEvent::Scroll:
        {

            g_zoomMod->onScroll(
                value
            );

            break;
        }


        default:
            break;
    }
}


// =============================================================================
// CONSTRUCTOR
// =============================================================================

ZoomModule::ZoomModule()

    : Module(
        "Zoom",
        "Smoothly zooms your camera like OptiFine."
    )

{

    this->keybind =
        0;


    g_zoomMod =
        this;
}


// =============================================================================
// DESTRUCTOR
// =============================================================================

ZoomModule::~ZoomModule() {

    if (m_buttonRegistered) {

        pl::modmenu::unregisterButton(
            kZoomButtonId
        );


        m_buttonRegistered =
            false;
    }


    if (g_zoomMod == this) {

        g_zoomMod =
            nullptr;
    }
}


// =============================================================================
// INIT
// =============================================================================

void ZoomModule::onInit() {

    // =========================================================================
    // FOV
    // =========================================================================

    if (!m_fovHooked) {

        uintptr_t addr =

            bedrocktools::memory::resolve(

                bedrocktools::memory::
                    SignatureId::GetFov

            );


        if (addr != 0) {

            auto hook =

                bedrocktools::hooks::install(

                    reinterpret_cast<void*>(
                        addr
                    ),

                    reinterpret_cast<void*>(
                        _getFov_zoom_hook
                    ),

                    reinterpret_cast<void**>(
                        &_getFov_orig
                    )

                );


            m_fovHooked =
                hook != nullptr;
        }
    }


    // =========================================================================
    // TURN DELTA
    // =========================================================================

    if (!m_turnDeltaHooked) {

        uintptr_t addr =

            bedrocktools::memory::resolve(

                bedrocktools::memory::
                    SignatureId::
                        LocalPlayerApplyTurnDelta

            );


        if (addr != 0) {

            auto hook =

                bedrocktools::hooks::install(

                    reinterpret_cast<void*>(
                        addr
                    ),

                    reinterpret_cast<void*>(
                        _applyTurnDelta_hook
                    ),

                    reinterpret_cast<void**>(
                        &_applyTurnDelta_orig
                    )

                );


            m_turnDeltaHooked =
                hook != nullptr;
        }
    }


    // =========================================================================
    // HIDE HAND
    // =========================================================================

    if (!m_hideHandHooked) {

        uintptr_t addr =

            bedrocktools::memory::resolve(

                bedrocktools::memory::
                    SignatureId::
                        BaseOptionRegistryGetHideItemInHand

            );


        if (addr != 0) {

            auto hook =

                bedrocktools::hooks::install(

                    reinterpret_cast<void*>(
                        addr
                    ),

                    reinterpret_cast<void*>(
                        _getHideItemInHand_hook
                    ),

                    reinterpret_cast<void**>(
                        &_getHideItemInHand_orig
                    )

                );


            m_hideHandHooked =
                hook != nullptr;
        }
    }


    // =========================================================================
    // TOUCH CALLBACK
    // =========================================================================
    //
    // Tetap hanya satu callback dan tidak ada unregister API.
    //

    if (!m_touchHooked) {

        pl::input::registerTouchCallback(
            _onTouchBridge
        );


        m_touchHooked =
            true;
    }


    // =========================================================================
    // RAW GAMEACTIVITY MOTION HOOK
    // =========================================================================
    //
    // Priority:
    //
    // 1. Cari exported symbol via dlsym.
    // 2. Tidak hard-code RVA 0x0787D080.
    //
    // Dengan begitu jika update Minecraft hanya menggeser RVA,
    // implementation masih punya peluang tetap bekerja.
    //

    if (!m_rawMotionHooked) {

        auto handle =

            bedrocktools::hooks::openLibrary(
                "libminecraftpe.so"
            );


        if (handle) {

            uintptr_t addr =

                bedrocktools::hooks::symbol(

                    handle,

                    "GameActivityMotionEvent_fromJava"

                );


            if (addr != 0) {

                auto hook =

                    bedrocktools::hooks::install(

                        reinterpret_cast<void*>(
                            addr
                        ),

                        reinterpret_cast<void*>(
                            _gameActivityMotionEventFromJava_hook
                        ),

                        reinterpret_cast<void**>(
                            &_gameActivityMotionEventFromJava_orig
                        )

                    );


                m_rawMotionHooked =
                    hook != nullptr;
            }


            bedrocktools::hooks::closeLibrary(
                handle
            );
        }
    }


    // =========================================================================
    // BUTTON BUILDER
    // =========================================================================

    if (!m_buttonRegistered) {

        m_buttonRegistered =

            pl::modmenu::ButtonBuilder(

                std::string(
                    kZoomButtonId
                ),

                "Zoom"

            )


            .moduleId(
                this->moduleId
            )


            .label(
                "ZM"
            )


            .behavior(
                pl::modmenu::
                    ButtonBehavior::Hold
            )


            .defaultVisible(
                true
            )


            .stylePreset(
                pl::modmenu::
                    ButtonStylePreset::Keycap
            )


            // -----------------------------------------------------------------
            // Transparent launcher wrapper.
            //
            // Nilai harus != 0 supaya launcher tidak fallback ke Keycap color.
            // -----------------------------------------------------------------

            .styleColors(

                0x00000001U,

                0x00000001U,

                0x00000001U

            )


            .textColor(
                0x00000001U
            )


            .activeTextColor(
                0x00000001U
            )


            .sizeScale(
                1.0f,
                1.0f
            )


            .resourceIcon(

                "ic_zoom_normal,ic_zoom_pressed",

                true

            )


            .onEvent(
                _onZoomButtonEvent
            )


            .registerButton();
    }
}


// =============================================================================
// ENABLE
// =============================================================================

void ZoomModule::onEnable() {

    m_isFirstTime =
        true;


    m_animationFinished =
        false;


    m_candidatePointerId =
        -1;


    m_candidateValid =
        false;


    m_waitingForZoomPointer =
        false;


    resetZoomPointer();
}


// =============================================================================
// DISABLE
// =============================================================================

void ZoomModule::onDisable() {

    m_animationFinished =
        false;


    m_keyZooming =
        false;


    m_buttonZooming =
        false;


    m_candidatePointerId =
        -1;


    m_candidateValid =
        false;


    m_waitingForZoomPointer =
        false;


    resetZoomPointer();
}


// =============================================================================
// ACTIVE STATE
// =============================================================================

bool ZoomModule::isZoomActive() {

    if (!enabled) {

        return false;
    }


    return

        m_keyZooming ||
        m_buttonZooming;
}


// =============================================================================
// CAPTURE POINTER
// =============================================================================

void ZoomModule::captureZoomPointer(
    int32_t pointerId,
    float initialY
) {

    if (pointerId < 0) {

        return;
    }


    m_zoomPointerId =
        pointerId;


    m_lastRawY =
        initialY;


    //
    // Jangan langsung anggap initialY berasal dari coordinate system
    // yang sama persis dengan raw GameActivity Y.
    //
    // Frame raw pertama digunakan sebagai baseline.
    //

    m_hasLastRawY =
        false;


    m_waitingForZoomPointer =
        false;
}


// =============================================================================
// RESET POINTER
// =============================================================================

void ZoomModule::resetZoomPointer() {

    m_zoomPointerId =
        -1;


    m_lastRawY =
        0.0f;


    m_hasLastRawY =
        false;
}


// =============================================================================
// BUTTON DOWN
// =============================================================================

void ZoomModule::beginButtonZoom() {

    if (!enabled) {

        return;
    }


    m_isFirstTime =
        true;


    m_animationFinished =
        false;


    m_targetZoomFov =
        m_defaultZoomFov;


    m_buttonZooming =
        true;


    // =========================================================================
    // POINTER HANDSHAKE
    // =========================================================================
    //
    // Candidate hanya valid dalam jendela waktu pendek.
    //
    // Ini mencegah pointer joystick lama dianggap sebagai pointer Zoom.
    //

    constexpr uint64_t kCandidateMaxAgeMs =
        180;


    const uint64_t currentTime =
        nowMs();


    const bool candidateFresh =

        m_candidateValid &&

        currentTime >=
            m_candidateTimeMs &&

        (
            currentTime -
            m_candidateTimeMs
        ) <= kCandidateMaxAgeMs;


    if (candidateFresh) {

        captureZoomPointer(

            m_candidatePointerId,

            m_candidateY

        );


        m_candidateValid =
            false;


        m_candidatePointerId =
            -1;

    } else {

        // ---------------------------------------------------------------------
        // Kalau ButtonBuilder DOWN datang lebih dulu,
        // TouchEvent DOWN berikutnya akan di-claim.
        // ---------------------------------------------------------------------

        resetZoomPointer();


        m_waitingForZoomPointer =
            true;
    }
}


// =============================================================================
// BUTTON UP
// =============================================================================

void ZoomModule::endButtonZoom() {

    m_buttonZooming =
        false;


    m_waitingForZoomPointer =
        false;


    m_candidateValid =
        false;


    m_candidatePointerId =
        -1;


    resetZoomPointer();
}


// =============================================================================
// TOUCH EVENT
// =============================================================================
//
// Sekarang fungsi ini sangat ringan.
//
// Tidak ada ACTION_MOVE handling.
// Tidak ada hit-test.
// Tidak ada perhitungan drag.
//
// Hanya melakukan pointer handshake.
//

bool ZoomModule::onTouchEvent(
    const pl::input::TouchEvent& ev
) {

    if (!enabled) {

        return false;
    }


    switch (ev.action) {

        // =====================================================================
        // POINTER DOWN
        // =====================================================================

        case kActionDown:
        case kActionPointerDown:
        {

            const int32_t pointerId =

                static_cast<int32_t>(
                    ev.pointerId
                );


            // -----------------------------------------------------------------
            // Kalau ButtonBuilder sudah mengirim DOWN,
            // pointer ini langsung menjadi Zoom pointer.
            // -----------------------------------------------------------------

            if (

                m_buttonZooming &&
                m_waitingForZoomPointer

            ) {

                captureZoomPointer(

                    pointerId,

                    ev.y

                );


                return false;
            }


            // -----------------------------------------------------------------
            // Kalau TouchEvent datang lebih dahulu,
            // simpan sebagai candidate.
            // -----------------------------------------------------------------

            m_candidatePointerId =
                pointerId;


            m_candidateY =
                ev.y;


            m_candidateTimeMs =
                nowMs();


            m_candidateValid =
                true;


            return false;
        }


        // =====================================================================
        // POINTER UP
        // =====================================================================

        case kActionUp:
        case kActionPointerUp:
        {

            const int32_t pointerId =

                static_cast<int32_t>(
                    ev.pointerId
                );


            if (
                pointerId ==
                m_zoomPointerId
            ) {

                resetZoomPointer();
            }


            if (
                pointerId ==
                m_candidatePointerId
            ) {

                m_candidatePointerId =
                    -1;


                m_candidateValid =
                    false;
            }


            return false;
        }


        // =====================================================================
        // CANCEL
        // =====================================================================

        case kActionCancel:
        {

            m_candidatePointerId =
                -1;


            m_candidateValid =
                false;


            m_waitingForZoomPointer =
                false;


            resetZoomPointer();


            return false;
        }


        // =====================================================================
        // MOVE
        // =====================================================================
        //
        // Sengaja TIDAK diproses.
        //
        // MOVE ditangani GameActivityMotionEvent_fromJava.
        //

        case kActionMove:
        default:
            break;
    }


    return false;
}


// =============================================================================
// PROCESS RAW MOTION
// =============================================================================

void ZoomModule::processRawMotion(
    const void* motionEvent
) {

    if (!motionEvent) {

        return;
    }


    if (!m_buttonZooming) {

        return;
    }


    if (m_zoomPointerId < 0) {

        return;
    }


    const auto* base =

        static_cast<
            const std::uint8_t*
        >(
            motionEvent
        );


    // =========================================================================
    // POINTER COUNT
    // =========================================================================

    const int32_t pointerCount =

        raw_motion::readValue<int32_t>(

            base,

            raw_motion::
                kPointerCountOffset

        );


    // =========================================================================
    // BASIC SANITY
    // =========================================================================
    //
    // BetterZoom sendiri juga membatasi jumlah pointer sebelum memproses.
    //
    // Kita gunakan limit kecil supaya pointerCount corrupt tidak pernah
    // membuat loop besar / invalid memory traversal.
    //

    if (

        pointerCount <= 0 ||

        pointerCount >
            raw_motion::
                kMaxReasonablePointers

    ) {

        return;
    }


    // =========================================================================
    // FIND ZOOM POINTER
    // =========================================================================

    for (
        int32_t i = 0;
        i < pointerCount;
        ++i
    ) {

        const std::size_t pointerOffset =

            raw_motion::
                kPointerBaseOffset +

            static_cast<std::size_t>(i) *

            raw_motion::
                kPointerStride;


        const auto* pointer =

            base +
            pointerOffset;


        const int32_t pointerId =

            raw_motion::readValue<int32_t>(

                pointer,

                raw_motion::
                    kPointerIdOffset

            );


        if (
            pointerId !=
            m_zoomPointerId
        ) {

            continue;
        }


        // =====================================================================
        // RAW Y
        // =====================================================================
        //
        // AXIS_Y adalah +0x0C relatif terhadap pointer record.
        //
        // Ini dikonfirmasi dari disassembly 1.26.45.1:
        // GameActivityMotionEvent_fromJava mengisi axis array mulai +0x08,
        // sehingga axis 1/Y berada di +0x0C.
        //

        const float currentY =

            raw_motion::readValue<float>(

                pointer,

                raw_motion::
                    kPointerYOffset

            );


        if (!std::isfinite(currentY)) {

            return;
        }


        // =====================================================================
        // FIRST RAW SAMPLE
        // =====================================================================

        if (!m_hasLastRawY) {

            m_lastRawY =
                currentY;


            m_hasLastRawY =
                true;


            return;
        }


        // =====================================================================
        // DELTA
        // =====================================================================

        const float deltaY =

            currentY -
            m_lastRawY;


        m_lastRawY =
            currentY;


        // =====================================================================
        // IGNORE TINY FLOAT NOISE
        // =====================================================================

        if (
            std::abs(deltaY) <
            0.01f
        ) {

            return;
        }


        // =====================================================================
        // APPLY
        // =====================================================================

        updateDrag(
            deltaY
        );


        return;
    }
}


// =============================================================================
// DRAG -> FOV
// =============================================================================

void ZoomModule::updateDrag(
    float deltaY
) {

    if (!isZoomActive()) {

        return;
    }


    // =========================================================================
    // DIRECTION
    // =========================================================================
    //
    // UP:
    //
    // Y turun
    // deltaY negatif
    // FOV turun
    // Zoom IN
    //
    //
    // DOWN:
    //
    // Y naik
    // deltaY positif
    // FOV naik
    // Zoom OUT
    //

    constexpr float kDragSensitivity =
        0.08f;


    const float change =

        deltaY *
        kDragSensitivity;


    // =========================================================================
    // LIMITS
    // =========================================================================

    constexpr float minLimit =
        3.0f;


    const float maxLimit =

        std::max(

            minLimit + 5.0f,

            m_baseFov - 5.0f

        );


    m_targetZoomFov =

        std::clamp(

            m_targetZoomFov +
                change,

            minLimit,

            maxLimit

        );
}


// =============================================================================
// GENERIC WHEEL
// =============================================================================

void ZoomModule::onScroll(
    float scrollDelta
) {

    if (!isZoomActive()) {

        return;
    }


    const float change =

        -scrollDelta *
        2.5f;


    constexpr float minLimit =
        3.0f;


    const float maxLimit =

        std::max(

            minLimit + 5.0f,

            m_baseFov - 5.0f

        );


    m_targetZoomFov =

        std::clamp(

            m_targetZoomFov +
                change,

            minLimit,

            maxLimit

        );
}


// =============================================================================
// KEYBIND
// =============================================================================

void ZoomModule::onKeybindEvent(
    const std::string& key,
    bool isDown
) {

    if (key != "keybind") {

        return;
    }


    if (

        isDown &&
        !m_keyZooming

    ) {

        m_isFirstTime =
            true;


        m_animationFinished =
            false;


        m_targetZoomFov =
            m_defaultZoomFov;
    }


    m_keyZooming =
        isDown;
}


// =============================================================================
// LOAD CONFIG
// =============================================================================

void ZoomModule::loadConfig(
    const nlohmann::json& j
) {

    Module::loadConfig(
        j
    );


    if (
        j.contains(
            "m_defaultZoomFov"
        )
    ) {

        m_defaultZoomFov =

            j[
                "m_defaultZoomFov"
            ].get<float>();
    }


    if (
        j.contains(
            "m_targetZoomFov"
        )
    ) {

        m_targetZoomFov =

            j[
                "m_targetZoomFov"
            ].get<float>();
    }


    if (
        j.contains(
            "m_animSpeed"
        )
    ) {

        m_animSpeed =

            j[
                "m_animSpeed"
            ].get<float>();
    }


    if (
        j.contains(
            "m_lowSens"
        )
    ) {

        m_lowSens =

            j[
                "m_lowSens"
            ].get<bool>();
    }


    if (
        j.contains(
            "m_lowSensStrength"
        )
    ) {

        m_lowSensStrength =

            j[
                "m_lowSensStrength"
            ].get<float>();
    }


    if (
        j.contains(
            "m_hideHand"
        )
    ) {

        m_hideHand =

            j[
                "m_hideHand"
            ].get<bool>();
    }
}


// =============================================================================
// SAVE CONFIG
// =============================================================================

void ZoomModule::saveConfig(
    nlohmann::json& j
) {

    Module::saveConfig(
        j
    );


    j[
        "m_defaultZoomFov"
    ] =
        m_defaultZoomFov;


    j[
        "m_targetZoomFov"
    ] =
        m_targetZoomFov;


    j[
        "m_animSpeed"
    ] =
        m_animSpeed;


    j[
        "m_lowSens"
    ] =
        m_lowSens;


    j[
        "m_lowSensStrength"
    ] =
        m_lowSensStrength;


    j[
        "m_hideHand"
    ] =
        m_hideHand;
}
