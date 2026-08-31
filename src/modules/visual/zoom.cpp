#include "zoom.hpp"

#include "core/memory/Hooks.hpp"

#include <bedrocktools/memory/Signatures.hpp>
#include <bedrocktools/sdk/Memory.hpp>

#include <pl/ModMenu.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>


// =============================================================================
// GLOBAL MODULE INSTANCE
// =============================================================================

static ZoomModule* g_zoomMod = nullptr;


// =============================================================================
// ZOOM BUTTON
// =============================================================================

namespace {

constexpr std::string_view kZoomButtonId =
    "bedrocktools.zoom.button";

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
    float originalFov = 0.0f;

    if (_getFov_orig) {
        originalFov = _getFov_orig(
            _this,
            a,
            enableVariableFOV
        );
    }


    if (!g_zoomMod) {
        return originalFov;
    }


    // -------------------------------------------------------------------------
    // Abaikan FOV khusus UI / hand / inventory.
    //
    // Logic ini dipertahankan sama seperti implementasi sebelumnya.
    // -------------------------------------------------------------------------

    if (
        originalFov == 70.0f ||
        originalFov == 60.0f
    ) {
        return originalFov;
    }


    // -------------------------------------------------------------------------
    // Simpan base FOV Minecraft.
    // -------------------------------------------------------------------------

    g_zoomMod->m_baseFov = originalFov;


    // -------------------------------------------------------------------------
    // Saat zoom dimulai untuk pertama kali,
    // current FOV harus dimulai dari FOV Minecraft sekarang.
    // -------------------------------------------------------------------------

    if (g_zoomMod->m_isFirstTime) {

        g_zoomMod->m_currentFov =
            originalFov;

        g_zoomMod->m_isFirstTime =
            false;
    }


    // -------------------------------------------------------------------------
    // Zoom aktif.
    // -------------------------------------------------------------------------

    if (g_zoomMod->isZoomActive()) {

        g_zoomMod->m_animationFinished =
            false;


        g_zoomMod->m_currentFov =
            std::lerp(
                g_zoomMod->m_currentFov,
                g_zoomMod->m_targetZoomFov,
                g_zoomMod->m_animSpeed
            );


        return g_zoomMod->m_currentFov;
    }


    // -------------------------------------------------------------------------
    // Zoom dilepas.
    //
    // Smoothly kembali menuju FOV Minecraft.
    // -------------------------------------------------------------------------

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


        return g_zoomMod->m_currentFov;
    }


    return originalFov;
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

        // ---------------------------------------------------------------------
        // Hitung rasio FOV.
        // ---------------------------------------------------------------------

        float zoomRatio =
            g_zoomMod->m_currentFov /
            g_zoomMod->m_baseFov;


        float strength =
            g_zoomMod->m_lowSensStrength;


        // ---------------------------------------------------------------------
        // Kurangi camera sensitivity berdasarkan level zoom.
        // ---------------------------------------------------------------------

        float multiplier =
            1.0f -
            (
                1.0f -
                zoomRatio
            ) * strength;


        multiplier =
            std::clamp(
                multiplier,
                0.01f,
                1.0f
            );


        Vec2 modifiedDelta = {

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

    bool hide = false;


    if (_getHideItemInHand_orig) {

        hide =
            _getHideItemInHand_orig(
                _this
            );

    }


    // -------------------------------------------------------------------------
    // Jika Zoom aktif dan Hide Hand aktif,
    // force Minecraft untuk menyembunyikan tangan/item.
    // -------------------------------------------------------------------------

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

        // ---------------------------------------------------------------------
        // Launcher ButtonBuilder:
        //
        // Finger mulai menekan tombol.
        // ---------------------------------------------------------------------

        case pl::modmenu::ButtonEvent::Down:
        {

            if (!g_zoomMod->enabled) {
                return;
            }


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


        // ---------------------------------------------------------------------
        // Finger dilepas dari tombol.
        // ---------------------------------------------------------------------

        case pl::modmenu::ButtonEvent::Up:
        {

            g_zoomMod->m_buttonZooming =
                false;


            break;
        }


        // ---------------------------------------------------------------------
        // Scroll event.
        //
        // Dipertahankan untuk compatibility.
        // ---------------------------------------------------------------------

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
// MODULE CONSTRUCTOR
// =============================================================================

ZoomModule::ZoomModule()
    : Module(
        "Zoom",
        "Smoothly zooms your camera like OptiFine."
    )
{

    this->keybind = 0;


    g_zoomMod = this;
}


// =============================================================================
// MODULE DESTRUCTOR
// =============================================================================

ZoomModule::~ZoomModule() {

    // -------------------------------------------------------------------------
    // Lepaskan ButtonBuilder ketika module object dihancurkan.
    // -------------------------------------------------------------------------

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
    // TURN DELTA / CAMERA SENSITIVITY HOOK
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
    // HIDE ITEM IN HAND HOOK
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
    // LEVI LAUNCHER BUTTON BUILDER
    // =========================================================================

    if (!m_buttonRegistered) {

        m_buttonRegistered =

            pl::modmenu::ButtonBuilder(

                std::string(
                    kZoomButtonId
                ),

                "Zoom"

            )


            // -----------------------------------------------------------------
            // Hubungkan button ke module Zoom.
            //
            // Launcher akan mengatur runtime visibility berdasarkan
            // moduleId ini.
            // -----------------------------------------------------------------

            .moduleId(
                this->moduleId
            )


            // -----------------------------------------------------------------
            // Label fallback.
            //
            // Tidak akan terlihat selama icon launcher berhasil dimuat.
            // -----------------------------------------------------------------

            .label(
                "ZM"
            )


            // -----------------------------------------------------------------
            // Zoom bekerja sebagai HOLD button.
            // -----------------------------------------------------------------

            .behavior(
                pl::modmenu::
                    ButtonBehavior::Hold
            )


            // -----------------------------------------------------------------
            // Tombol tampil secara default ketika module Zoom aktif.
            // -----------------------------------------------------------------

            .defaultVisible(
                true
            )


            // -----------------------------------------------------------------
            // Gunakan infrastructure Keycap launcher.
            //
            // Background kemudian dibuat transparan karena Zoom bawaan
            // launcher merupakan image-only button.
            // -----------------------------------------------------------------

            .stylePreset(
                pl::modmenu::
                    ButtonStylePreset::Keycap
            )


            // -----------------------------------------------------------------
            // Hilangkan background ExternalButtonOverlay.
            //
            // Yang terlihat hanya icon Zoom bawaan launcher.
            // -----------------------------------------------------------------

            .styleColors(

                0x00000000U,
                0x00000000U,
                0x00000000U

            )


            // -----------------------------------------------------------------
            // Text juga transparan.
            //
            // Label otomatis disembunyikan saat resourceIcon ditemukan,
            // tetapi ini menjadi fallback supaya tidak muncul ZM di belakang.
            // -----------------------------------------------------------------

            .textColor(
                0x00000000U
            )


            .activeTextColor(
                0x00000000U
            )


            // -----------------------------------------------------------------
            // Ukuran dasar launcher.
            //
            // Posisi, size, dan opacity berikutnya dikelola HUD Editor
            // launcher, bukan Mod Menu BedrockTools.
            // -----------------------------------------------------------------

            .sizeScale(
                1.0f,
                1.0f
            )


            // -----------------------------------------------------------------
            // ICON ASLI ZOOM LEVI LAUNCHER
            //
            // Resource mode ExternalButtonOverlay menerima:
            //
            // "normalResource,activeResource"
            //
            // Normal:
            //     ic_zoom_normal
            //
            // Pressed:
            //     ic_zoom_pressed
            // -----------------------------------------------------------------

            .resourceIcon(

                "ic_zoom_normal,ic_zoom_pressed",

                true

            )


            // -----------------------------------------------------------------
            // Native callback.
            // -----------------------------------------------------------------

            .onEvent(
                _onZoomButtonEvent
            )


            // -----------------------------------------------------------------
            // Register ke launcher.
            // -----------------------------------------------------------------

            .registerButton();
    }
}


// =============================================================================
// MODULE ENABLE
// =============================================================================

void ZoomModule::onEnable() {

    m_isFirstTime =
        true;


    m_animationFinished =
        false;
}


// =============================================================================
// MODULE DISABLE
// =============================================================================

void ZoomModule::onDisable() {

    m_animationFinished =
        false;


    m_keyZooming =
        false;


    m_buttonZooming =
        false;
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
// BUTTON SCROLL
// =============================================================================

void ZoomModule::onScroll(
    float scrollDelta
) {

    if (!isZoomActive()) {
        return;
    }


    float change =
        -scrollDelta *
        2.5f;


    float minLimit =
        3.0f;


    float maxLimit =
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
