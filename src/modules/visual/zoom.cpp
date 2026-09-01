#include "zoom.hpp"

#include "core/memory/Hooks.hpp"

#include <bedrocktools/memory/Signatures.hpp>
#include <bedrocktools/sdk/Memory.hpp>

#include <pl/Input.hpp>
#include <pl/ModMenu.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>


// =============================================================================
// GLOBAL MODULE INSTANCE
// =============================================================================

static ZoomModule* g_zoomMod =
    nullptr;


// =============================================================================
// BUTTON ID
// =============================================================================

namespace {

constexpr std::string_view kZoomButtonId =
    "bedrocktools.zoom.button";

}


// =============================================================================
// ANDROID MOTION EVENT CONSTANTS
// =============================================================================
//
// Android MotionEvent:
//
// ACTION_DOWN         = 0
// ACTION_UP           = 1
// ACTION_MOVE         = 2
// ACTION_CANCEL       = 3
// ACTION_POINTER_DOWN = 5
// ACTION_POINTER_UP   = 6
//
// pl::input::TouchEvent.action menerima actionMasked dari launcher.
//

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
    // FILTER SPECIAL FOV
    // =========================================================================
    //
    // Logic ini sengaja belum kita ubah.
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
    // FIRST ZOOM FRAME
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
    // ZOOM RELEASE ANIMATION
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
// CAMERA SENSITIVITY HOOK
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

        (
            g_zoomMod->isZoomActive() ||
            !g_zoomMod->m_animationFinished
        ) &&

        g_zoomMod->m_lowSens &&

        g_zoomMod->m_baseFov > 0.1f
    ) {

        // =====================================================================
        // ZOOM RATIO
        // =====================================================================

        float zoomRatio =

            g_zoomMod->m_currentFov /
            g_zoomMod->m_baseFov;


        float strength =

            g_zoomMod->m_lowSensStrength;


        // =====================================================================
        // SENSITIVITY MULTIPLIER
        // =====================================================================

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
// HIDE HAND HOOK
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
// TOUCH INPUT BRIDGE
// =============================================================================
//
// Callback ini dipanggil oleh PreloaderInput.
//
// PENTING:
//
// Jangan consume ACTION_MOVE milik Zoom.
// Kita tetap return false agar Android View milik ButtonBuilder terus menerima
// stream touch yang sama sampai ACTION_UP.
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
        // BUTTON DOWN
        // =====================================================================
        //
        // Urutan launcher:
        //
        // PreloaderInput ACTION_DOWN
        //         ↓
        // cache pointerId + y
        //         ↓
        // ExternalButtonOverlay ACTION_DOWN
        //         ↓
        // ButtonEvent::Down
        //
        // Karena itu di titik ini kita sudah mengetahui pointerId tombol.
        //

        case pl::modmenu::ButtonEvent::Down:
        {

            if (!g_zoomMod->enabled) {

                return;
            }


            // -----------------------------------------------------------------
            // Ambil pointer yang tadi dicache oleh PreloaderInput.
            // -----------------------------------------------------------------

            if (g_zoomMod->m_pendingTouchValid) {

                g_zoomMod->m_trackedPointerId =
                    g_zoomMod->m_pendingPointerId;


                g_zoomMod->m_lastTouchY =
                    g_zoomMod->m_pendingTouchY;

            } else {

                //
                // Seharusnya jarang terjadi.
                //
                // -1 berarti drag tidak dijalankan sampai pointer diketahui.
                //

                g_zoomMod->m_trackedPointerId =
                    -1;
            }


            // -----------------------------------------------------------------
            // Pending pointer sudah digunakan.
            // -----------------------------------------------------------------

            g_zoomMod->m_pendingTouchValid =
                false;


            g_zoomMod->m_pendingPointerId =
                -1;


            // -----------------------------------------------------------------
            // Mulai Zoom.
            // -----------------------------------------------------------------

            if (!g_zoomMod->m_buttonZooming) {

                g_zoomMod->m_isFirstTime =
                    true;


                g_zoomMod->m_animationFinished =
                    false;


                g_zoomMod->m_targetZoomFov =
                    g_zoomMod->m_defaultZoomFov;


                g_zoomMod->m_buttonZooming =
                    true;
            }


            break;
        }


        // =====================================================================
        // BUTTON UP
        // =====================================================================

        case pl::modmenu::ButtonEvent::Up:
        {

            g_zoomMod->m_buttonZooming =
                false;


            g_zoomMod->m_trackedPointerId =
                -1;


            g_zoomMod->m_pendingPointerId =
                -1;


            g_zoomMod->m_pendingTouchValid =
                false;


            break;
        }


        // =====================================================================
        // GENERIC SCROLL
        // =====================================================================
        //
        // Ini BUKAN touchscreen swipe.
        //
        // Tetap kita pertahankan untuk mouse wheel / trackpad.
        //

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
// MODULE INIT
// =============================================================================

void ZoomModule::onInit() {

    // =========================================================================
    // FOV HOOK
    // =========================================================================

    if (!m_fovHooked) {

        uintptr_t addr =

            bedrocktools::memory::resolve(

                bedrocktools::memory::
                    SignatureId::GetFov

            );


        if (addr != 0) {

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
                true;
        }
    }


    // =========================================================================
    // CAMERA TURN DELTA HOOK
    // =========================================================================

    if (!m_turnDeltaHooked) {

        uintptr_t addr =

            bedrocktools::memory::resolve(

                bedrocktools::memory::
                    SignatureId::
                        LocalPlayerApplyTurnDelta

            );


        if (addr != 0) {

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
                true;
        }
    }


    // =========================================================================
    // HIDE HAND HOOK
    // =========================================================================

    if (!m_hideHandHooked) {

        uintptr_t addr =

            bedrocktools::memory::resolve(

                bedrocktools::memory::
                    SignatureId::
                        BaseOptionRegistryGetHideItemInHand

            );


        if (addr != 0) {

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
                true;
        }
    }


    // =========================================================================
    // TOUCH INPUT
    // =========================================================================
    //
    // Register sekali per process.
    //
    // API Levi belum menyediakan unregisterTouchCallback(),
    // jadi callback tetap terdaftar dan mengecek g_zoomMod / enabled.
    //

    if (!m_touchHooked) {

        pl::input::registerTouchCallback(
            _onTouchBridge
        );


        m_touchHooked =
            true;
    }


    // =========================================================================
    // LEVI BUTTON BUILDER
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
            // Transparent wrapper.
            //
            // Jangan ganti menjadi 0x00000000.
            // Nilai 0 dianggap "gunakan default style" oleh launcher.
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


            // -----------------------------------------------------------------
            // Zoom icon asli launcher.
            // -----------------------------------------------------------------

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


    m_trackedPointerId =
        -1;


    m_pendingPointerId =
        -1;


    m_pendingTouchValid =
        false;
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


    m_trackedPointerId =
        -1;


    m_pendingPointerId =
        -1;


    m_pendingTouchValid =
        false;
}


// =============================================================================
// ZOOM ACTIVE
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
// TOUCHSCREEN SWIPE
// =============================================================================

bool ZoomModule::onTouchEvent(
    const pl::input::TouchEvent& ev
) {

    // =========================================================================
    // Module disabled
    // =========================================================================

    if (!enabled) {

        return false;
    }


    const int action =
        ev.action;


    switch (action) {

        // =====================================================================
        // DOWN / POINTER DOWN
        // =====================================================================
        //
        // Kita belum tahu apakah DOWN ini berada pada tombol Zoom.
        //
        // Karena PreloaderInput dipanggil sebelum Android View/ButtonBuilder,
        // cukup cache dulu.
        //
        // Kalau DOWN memang tombol Zoom, beberapa saat setelah ini
        // ButtonEvent::Down akan mengambil cache tersebut.
        //

        case kActionDown:
        case kActionPointerDown:
        {

            m_pendingPointerId =
                ev.pointerId;


            m_pendingTouchY =
                ev.y;


            m_pendingTouchValid =
                true;


            // -----------------------------------------------------------------
            // JANGAN consume.
            //
            // ButtonBuilder masih harus menerima ACTION_DOWN ini.
            // -----------------------------------------------------------------

            return false;
        }


        // =====================================================================
        // MOVE
        // =====================================================================

        case kActionMove:
        {

            // -----------------------------------------------------------------
            // Hanya lakukan drag jika:
            //
            // 1. Zoom Button sedang ditahan.
            // 2. Kita memiliki pointer tombol.
            // 3. Pointer MOVE adalah pointer yang sama.
            // -----------------------------------------------------------------

            if (
                m_buttonZooming &&
                m_trackedPointerId != -1 &&
                ev.pointerId == m_trackedPointerId
            ) {

                const float deltaY =

                    ev.y -
                    m_lastTouchY;


                m_lastTouchY =
                    ev.y;


                updateDrag(
                    deltaY
                );
            }


            // -----------------------------------------------------------------
            // PENTING:
            //
            // Jangan return true.
            //
            // Kalau MOVE dikonsumsi di sini, ExternalButtonOverlay bisa
            // kehilangan stream MotionEvent dan event UP/pressed-state
            // menjadi bermasalah.
            // -----------------------------------------------------------------

            return false;
        }


        // =====================================================================
        // UP / POINTER UP
        // =====================================================================

        case kActionUp:
        case kActionPointerUp:
        {

            // -----------------------------------------------------------------
            // Jangan mematikan m_buttonZooming di sini.
            //
            // Biarkan ButtonBuilder::Up menjadi source-of-truth untuk release
            // tombol dan icon pressed state.
            // -----------------------------------------------------------------

            if (
                ev.pointerId ==
                m_trackedPointerId
            ) {

                m_trackedPointerId =
                    -1;
            }


            if (
                ev.pointerId ==
                m_pendingPointerId
            ) {

                m_pendingPointerId =
                    -1;


                m_pendingTouchValid =
                    false;
            }


            return false;
        }


        // =====================================================================
        // CANCEL
        // =====================================================================

        case kActionCancel:
        {

            m_trackedPointerId =
                -1;


            m_pendingPointerId =
                -1;


            m_pendingTouchValid =
                false;


            return false;
        }


        default:
            break;
    }


    return false;
}


// =============================================================================
// TOUCHSCREEN DRAG -> FOV
// =============================================================================

void ZoomModule::updateDrag(
    float deltaY
) {

    if (!isZoomActive()) {

        return;
    }


    // =========================================================================
    // DRAG SENSITIVITY
    // =========================================================================
    //
    // Android Y:
    //
    // swipe UP:
    //     Y mengecil
    //     deltaY negatif
    //
    // swipe DOWN:
    //     Y membesar
    //     deltaY positif
    //
    //
    // FOV:
    //
    // lebih kecil = zoom IN
    // lebih besar = zoom OUT
    //
    //
    // Jadi:
    //
    // swipe UP
    //     ↓
    // delta negatif
    //     ↓
    // target FOV mengecil
    //     ↓
    // ZOOM IN
    //
    // swipe DOWN
    //     ↓
    // delta positif
    //     ↓
    // target FOV membesar
    //     ↓
    // ZOOM OUT
    // =========================================================================

    constexpr float kDragSensitivity =
        0.08f;


    const float change =

        deltaY *
        kDragSensitivity;


    // =========================================================================
    // FOV LIMIT
    // =========================================================================

    constexpr float minLimit =
        3.0f;


    const float maxLimit =

        std::max(

            minLimit + 5.0f,

            m_baseFov - 5.0f

        );


    // =========================================================================
    // APPLY
    // =========================================================================

    m_targetZoomFov =

        std::clamp(

            m_targetZoomFov +
                change,

            minLimit,

            maxLimit

        );
}


// =============================================================================
// GENERIC SCROLL
// =============================================================================
//
// Mouse wheel / trackpad.
//
// Touchscreen swipe TIDAK menggunakan fungsi ini.
//

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
