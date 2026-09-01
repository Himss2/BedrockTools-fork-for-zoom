#include "zoom.hpp"

#include "core/Runtime.hpp"
#include "core/memory/Hooks.hpp"

#include <bedrocktools/memory/Signatures.hpp>
#include <bedrocktools/sdk/Memory.hpp>

#include <pl/Input.hpp>
#include <pl/ModMenu.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string_view>
#include <vector>

namespace {

ZoomModule* g_zoomMod = nullptr;

constexpr std::string_view kNormalImageId  = "bedrocktools.zoom.normal";
constexpr std::string_view kPressedImageId = "bedrocktools.zoom.pressed";

constexpr int kZoomImageWidth  = 256;
constexpr int kZoomImageHeight = 256;
constexpr std::size_t kZoomImageBytes =
    static_cast<std::size_t>(kZoomImageWidth) *
    static_cast<std::size_t>(kZoomImageHeight) * 4u;

constexpr float kBaseButtonSize = 56.0f;
constexpr float kDragSensitivity = 0.08f;

constexpr int kActionMask        = 0xFF;
constexpr int kActionDown        = 0;
constexpr int kActionUp          = 1;
constexpr int kActionMove        = 2;
constexpr int kActionCancel      = 3;
constexpr int kActionPointerDown = 5;
constexpr int kActionPointerUp   = 6;

// Minimal GameActivityMotionEvent layout required by Zoom.
// Verified against Minecraft Bedrock Android 1.26.45.1:
//   event + 0x08 = action
//   event + 0x38 = pointerCount
//   event + 0x3C = pointer[0]
//   pointer stride = 0xD0
//   pointer + 0x00 = pointerId
//   pointer + 0x08 = AXIS_X
//   pointer + 0x0C = AXIS_Y
namespace raw_motion {
constexpr std::size_t kActionOffset       = 0x08;
constexpr std::size_t kPointerCountOffset = 0x38;
constexpr std::size_t kPointerBaseOffset  = 0x3C;
constexpr std::size_t kPointerStride      = 0xD0;
constexpr std::size_t kPointerIdOffset    = 0x00;
constexpr std::size_t kPointerYOffset     = 0x0C;
constexpr int32_t kMaxPointers            = 8;

template <typename T>
T read(const std::uint8_t* base, std::size_t offset) {
    T value{};
    std::memcpy(&value, base + offset, sizeof(T));
    return value;
}

template <typename T>
void write(std::uint8_t* base, std::size_t offset, const T& value) {
    std::memcpy(base + offset, &value, sizeof(T));
}
} // namespace raw_motion

uint32_t alphaWhite(float opacity) {
    const auto alpha = static_cast<uint32_t>(
        std::clamp(opacity, 0.0f, 1.0f) * 255.0f
    ) & 0xFFu;
    return (alpha << 24u) | 0x00FFFFFFu;
}

bool readExactFile(const std::filesystem::path& path,
                   std::vector<unsigned char>& out,
                   std::size_t expectedSize) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return false;

    const auto size = file.tellg();
    if (size < 0 || static_cast<std::size_t>(size) != expectedSize) return false;

    out.resize(expectedSize);
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
    return file.good() || file.gcount() == static_cast<std::streamsize>(out.size());
}

} // namespace

// =============================================================================
// FOV HOOK
// =============================================================================

static float (*_getFov_orig)(void*, float, int) = nullptr;

static float _getFov_zoom_hook(void* self, float value, int enableVariableFov) {
    const float originalFov = _getFov_orig
        ? _getFov_orig(self, value, enableVariableFov)
        : 0.0f;

    if (!g_zoomMod) return originalFov;

    // Keep the existing BedrockTools filter for hand/UI FOV calls.
    if (originalFov == 70.0f || originalFov == 60.0f) {
        return originalFov;
    }

    g_zoomMod->m_baseFov = originalFov;

    if (g_zoomMod->m_isFirstTime) {
        g_zoomMod->m_currentFov = originalFov;
        g_zoomMod->m_isFirstTime = false;
    }

    if (g_zoomMod->isZoomActive()) {
        g_zoomMod->m_animationFinished = false;
        g_zoomMod->m_currentFov = std::lerp(
            g_zoomMod->m_currentFov,
            g_zoomMod->m_targetZoomFov,
            g_zoomMod->m_animSpeed
        );
        return g_zoomMod->m_currentFov;
    }

    if (!g_zoomMod->m_animationFinished) {
        g_zoomMod->m_currentFov = std::lerp(
            g_zoomMod->m_currentFov,
            originalFov,
            g_zoomMod->m_animSpeed
        );

        if (std::abs(g_zoomMod->m_currentFov - originalFov) < 0.2f) {
            g_zoomMod->m_animationFinished = true;
            g_zoomMod->m_currentFov = originalFov;
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

static void (*_applyTurnDelta_orig)(void*, Vec2*) = nullptr;

static void _applyTurnDelta_hook(void* self, Vec2* rotationDelta) {
    if (!rotationDelta) {
        if (_applyTurnDelta_orig) _applyTurnDelta_orig(self, rotationDelta);
        return;
    }

    if (g_zoomMod &&
        (g_zoomMod->isZoomActive() || !g_zoomMod->m_animationFinished) &&
        g_zoomMod->m_lowSens &&
        g_zoomMod->m_baseFov > 0.1f) {

        const float zoomRatio = g_zoomMod->m_currentFov / g_zoomMod->m_baseFov;
        const float strength = g_zoomMod->m_lowSensStrength;
        float multiplier = 1.0f - (1.0f - zoomRatio) * strength;
        multiplier = std::clamp(multiplier, 0.01f, 1.0f);

        Vec2 modifiedDelta{
            rotationDelta->x * multiplier,
            rotationDelta->y * multiplier
        };

        if (_applyTurnDelta_orig) {
            _applyTurnDelta_orig(self, &modifiedDelta);
        }
        return;
    }

    if (_applyTurnDelta_orig) {
        _applyTurnDelta_orig(self, rotationDelta);
    }
}

// =============================================================================
// HIDE HAND HOOK
// =============================================================================

static bool (*_getHideItemInHand_orig)(void*) = nullptr;

static bool _getHideItemInHand_hook(void* self) {
    const bool original = _getHideItemInHand_orig
        ? _getHideItemInHand_orig(self)
        : false;

    if (g_zoomMod && g_zoomMod->isZoomActive() && g_zoomMod->m_hideHand) {
        return true;
    }

    return original;
}

// =============================================================================
// TOUCH CALLBACK
// =============================================================================

static bool _onTouchBridge(const pl::input::TouchEvent& event) {
    if (!g_zoomMod) return false;
    return g_zoomMod->onTouchEvent(event);
}

// =============================================================================
// RAW GameActivityMotionEvent_fromJava HOOK
// =============================================================================

// AArch64 ABI observed in Minecraft 1.26.45.1:
// x0 JNIEnv*, x1 Java MotionEvent, x2 output GameActivityMotionEvent,
// w3 pointer count, x4 history-related count.
using GameActivityMotionEventFromJavaFn =
    void (*)(void*, void*, void*, int32_t, int64_t);

static GameActivityMotionEventFromJavaFn _gameActivityMotionEventFromJava_orig = nullptr;

static void _gameActivityMotionEventFromJava_hook(
    void* env,
    void* javaMotionEvent,
    void* outputEvent,
    int32_t pointerCount,
    int64_t historyCount
) {
    if (_gameActivityMotionEventFromJava_orig) {
        _gameActivityMotionEventFromJava_orig(
            env,
            javaMotionEvent,
            outputEvent,
            pointerCount,
            historyCount
        );
    }

    // Extremely cheap fast path during ordinary gameplay.
    if (!g_zoomMod ||
        !g_zoomMod->enabled ||
        !g_zoomMod->m_buttonZooming ||
        g_zoomMod->m_trackedPointerId < 0 ||
        !outputEvent) {
        return;
    }

    g_zoomMod->processRawMotion(outputEvent);
}

// =============================================================================
// MODULE LIFECYCLE
// =============================================================================

ZoomModule::ZoomModule()
    : Module("Zoom", "Smoothly zooms your camera like OptiFine.") {
    keybind = 0;
    g_zoomMod = this;
}

ZoomModule::~ZoomModule() {
    clearButtonOverlay();
    if (g_zoomMod == this) g_zoomMod = nullptr;
}

void ZoomModule::onInit() {
    if (!m_fovHooked) {
        const uintptr_t address = bedrocktools::memory::resolve(
            bedrocktools::memory::SignatureId::GetFov
        );
        if (address != 0) {
            m_fovHooked = bedrocktools::hooks::install(
                reinterpret_cast<void*>(address),
                reinterpret_cast<void*>(_getFov_zoom_hook),
                reinterpret_cast<void**>(&_getFov_orig)
            ) != nullptr;
        }
    }

    if (!m_turnDeltaHooked) {
        const uintptr_t address = bedrocktools::memory::resolve(
            bedrocktools::memory::SignatureId::LocalPlayerApplyTurnDelta
        );
        if (address != 0) {
            m_turnDeltaHooked = bedrocktools::hooks::install(
                reinterpret_cast<void*>(address),
                reinterpret_cast<void*>(_applyTurnDelta_hook),
                reinterpret_cast<void**>(&_applyTurnDelta_orig)
            ) != nullptr;
        }
    }

    if (!m_hideHandHooked) {
        const uintptr_t address = bedrocktools::memory::resolve(
            bedrocktools::memory::SignatureId::BaseOptionRegistryGetHideItemInHand
        );
        if (address != 0) {
            m_hideHandHooked = bedrocktools::hooks::install(
                reinterpret_cast<void*>(address),
                reinterpret_cast<void*>(_getHideItemInHand_hook),
                reinterpret_cast<void**>(&_getHideItemInHand_orig)
            ) != nullptr;
        }
    }

    if (!m_touchHooked) {
        pl::input::registerTouchCallback(_onTouchBridge);
        m_touchHooked = true;
    }

    if (!m_rawMotionHooked) {
        auto handle = bedrocktools::hooks::openLibrary("libminecraftpe.so");
        if (handle) {
            const uintptr_t address = bedrocktools::hooks::symbol(
                handle,
                "GameActivityMotionEvent_fromJava"
            );

            if (address != 0) {
                m_rawMotionHooked = bedrocktools::hooks::install(
                    reinterpret_cast<void*>(address),
                    reinterpret_cast<void*>(_gameActivityMotionEventFromJava_hook),
                    reinterpret_cast<void**>(&_gameActivityMotionEventFromJava_orig)
                ) != nullptr;
            }

            bedrocktools::hooks::closeLibrary(handle);
        }
    }

    loadButtonImages();
}

void ZoomModule::onEnable() {
    m_isFirstTime = true;
    m_animationFinished = false;
    resetButtonPointer();
    loadButtonImages();
}

void ZoomModule::onDisable() {
    m_animationFinished = false;
    m_keyZooming = false;
    m_buttonZooming = false;
    resetButtonPointer();
    clearButtonOverlay();
}

void ZoomModule::onFrame() {
    if (!enabled || !m_overlayToggle) {
        clearButtonOverlay();
        return;
    }

    if (!m_imagesLoaded) {
        loadButtonImages();
    }

    renderButton();
}

bool ZoomModule::isZoomActive() {
    if (!enabled) return false;
    return m_keyZooming || m_buttonZooming;
}

// =============================================================================
// INBUILT-STYLE BUTTON IMAGE
// =============================================================================

bool ZoomModule::loadButtonImages() {
    if (m_imagesLoaded) return true;

    const auto& resourceRoot = bedrocktools::core::Runtime::get().resourceDirectory();
    const auto normalPath = resourceRoot / "zoom" / "ic_zoom_normal.rgba";
    const auto pressedPath = resourceRoot / "zoom" / "ic_zoom_pressed.rgba";

    std::vector<unsigned char> normalPixels;
    std::vector<unsigned char> pressedPixels;

    if (!readExactFile(normalPath, normalPixels, kZoomImageBytes) ||
        !readExactFile(pressedPath, pressedPixels, kZoomImageBytes)) {
        return false;
    }

    const bool normalOk = pl::modmenu::registerImage(
        kNormalImageId,
        normalPixels,
        kZoomImageWidth,
        kZoomImageHeight
    );

    const bool pressedOk = pl::modmenu::registerImage(
        kPressedImageId,
        pressedPixels,
        kZoomImageWidth,
        kZoomImageHeight
    );

    m_imagesLoaded = normalOk && pressedOk;
    return m_imagesLoaded;
}

bool ZoomModule::contains(float x, float y) const {
    const float size = kBaseButtonSize * std::clamp(m_scale, 0.1f, 5.0f);
    return x >= m_posX && x <= (m_posX + size) &&
           y >= m_posY && y <= (m_posY + size);
}

void ZoomModule::renderButton() {
    if (!enabled || !m_overlayToggle || !m_imagesLoaded) {
        clearButtonOverlay();
        return;
    }

    const float size = kBaseButtonSize * std::clamp(m_scale, 0.1f, 5.0f);

    std::array<pl::modmenu::DrawCommand, 1> commands{};
    auto& command = commands[0];
    command.type = pl::modmenu::DrawCommandType::Image;
    command.x = m_posX;
    command.y = m_posY;
    command.w = size;
    command.h = size;
    command.color = alphaWhite(m_opacity);
    command.imageId = m_buttonZooming
        ? std::string(kPressedImageId)
        : std::string(kNormalImageId);

    pl::modmenu::submitDrawCommands(moduleId, commands);
    m_overlayDrawn = true;
}

void ZoomModule::clearButtonOverlay() {
    if (!m_overlayDrawn) return;

    const std::array<pl::modmenu::DrawCommand, 0> empty{};
    pl::modmenu::submitDrawCommands(moduleId, empty);
    m_overlayDrawn = false;
}

// =============================================================================
// TOUCH DOWN/UP — ONLY CLAIM THE BUTTON POINTER
// =============================================================================

bool ZoomModule::onTouchEvent(const pl::input::TouchEvent& event) {
    if (!enabled || !m_overlayToggle) return false;

    const int action = event.action & kActionMask;

    switch (action) {
        case kActionDown:
        case kActionPointerDown: {
            if (m_trackedPointerId == -1 && contains(event.x, event.y)) {
                m_trackedPointerId = static_cast<int32_t>(event.pointerId);
                m_lastRawY = event.y;
                m_hasLastRawY = false;

                m_buttonZooming = true;
                m_isFirstTime = true;
                m_animationFinished = false;
                m_targetZoomFov = m_defaultZoomFov;
            }
            break;
        }

        case kActionUp:
        case kActionPointerUp: {
            if (static_cast<int32_t>(event.pointerId) == m_trackedPointerId) {
                m_buttonZooming = false;
                resetButtonPointer();
            }
            break;
        }

        case kActionCancel:
            m_buttonZooming = false;
            resetButtonPointer();
            break;

        case kActionMove:
        default:
            // MOVE intentionally ignored here. Levi's simplified TouchEvent
            // does not expose every pointer during Android ACTION_MOVE.
            break;
    }

    // Important: BetterZoom also leaves this false. We observe the event but
    // do not globally consume it, so other pointers keep driving analog/camera.
    return false;
}

void ZoomModule::resetButtonPointer() {
    m_trackedPointerId = -1;
    m_lastRawY = 0.0f;
    m_hasLastRawY = false;
}

// =============================================================================
// RAW ACTION_MOVE — FIND THE SAME POINTER THAT PRESSED ZOOM
// =============================================================================

void ZoomModule::processRawMotion(void* motionEvent) {
    if (!motionEvent || !m_buttonZooming || m_trackedPointerId < 0) return;

    auto* event = static_cast<std::uint8_t*>(motionEvent);

    const int32_t action = raw_motion::read<int32_t>(
        event,
        raw_motion::kActionOffset
    ) & kActionMask;

    const int32_t pointerCount = raw_motion::read<int32_t>(
        event,
        raw_motion::kPointerCountOffset
    );

    if (pointerCount <= 0 || pointerCount > raw_motion::kMaxPointers) return;

    // BetterZoom's important trick is not to consume the entire Android
    // MotionEvent. Instead, only the pointer owned by the Zoom button is
    // removed from the GameActivityMotionEvent pointer array. Other pointers
    // remain untouched and continue to drive movement/camera normally.
    for (int32_t index = 0; index < pointerCount; ++index) {
        auto* pointer = event +
            raw_motion::kPointerBaseOffset +
            static_cast<std::size_t>(index) * raw_motion::kPointerStride;

        const int32_t pointerId = raw_motion::read<int32_t>(
            pointer,
            raw_motion::kPointerIdOffset
        );

        if (pointerId != m_trackedPointerId) continue;

        // First use the Zoom pointer ourselves.
        if (action == kActionMove) {
            const float currentY = raw_motion::read<float>(
                pointer,
                raw_motion::kPointerYOffset
            );

            if (std::isfinite(currentY)) {
                if (!m_hasLastRawY) {
                    m_lastRawY = currentY;
                    m_hasLastRawY = true;
                } else {
                    const float deltaY = currentY - m_lastRawY;
                    m_lastRawY = currentY;

                    if (std::abs(deltaY) >= 0.01f) {
                        updateDrag(deltaY);
                    }
                }
            }
        }

        // Then hide only this pointer from Minecraft.
        //
        // Equivalent to BetterZoom's raw-touch filtering:
        //   memmove(pointer[i], pointer[i + 1], remaining * 0xD0)
        //   --pointerCount
        //
        // This is selective: joystick/camera fingers stay in the event.
        const int32_t remainingPointers =
            pointerCount - index - 1;

        if (remainingPointers > 0) {
            std::memmove(
                pointer,
                pointer + raw_motion::kPointerStride,
                static_cast<std::size_t>(remainingPointers) *
                    raw_motion::kPointerStride
            );
        }

        const int32_t filteredPointerCount =
            pointerCount - 1;

        raw_motion::write<int32_t>(
            event,
            raw_motion::kPointerCountOffset,
            filteredPointerCount
        );

        return;
    }
}

void ZoomModule::updateDrag(float deltaY) {
    if (!isZoomActive()) return;

    constexpr float minFov = 3.0f;
    const float maxFov = std::max(minFov + 5.0f, m_baseFov - 5.0f);

    // Android Y gets smaller while swiping upward:
    // negative delta -> smaller FOV -> stronger zoom.
    m_targetZoomFov = std::clamp(
        m_targetZoomFov + deltaY * kDragSensitivity,
        minFov,
        maxFov
    );
}

// =============================================================================
// KEYBIND
// =============================================================================

void ZoomModule::onKeybindEvent(const std::string& key, bool isDown) {
    if (key != "keybind") return;

    if (isDown && !m_keyZooming) {
        m_isFirstTime = true;
        m_animationFinished = false;
        m_targetZoomFov = m_defaultZoomFov;
    }

    m_keyZooming = isDown;
}

// =============================================================================
// CONFIG
// =============================================================================

void ZoomModule::loadConfig(const nlohmann::json& j) {
    Module::loadConfig(j);

    if (j.contains("m_defaultZoomFov"))  m_defaultZoomFov  = j["m_defaultZoomFov"].get<float>();
    if (j.contains("m_targetZoomFov"))   m_targetZoomFov   = j["m_targetZoomFov"].get<float>();
    if (j.contains("m_animSpeed"))       m_animSpeed       = j["m_animSpeed"].get<float>();
    if (j.contains("m_lowSens"))         m_lowSens         = j["m_lowSens"].get<bool>();
    if (j.contains("m_lowSensStrength")) m_lowSensStrength = j["m_lowSensStrength"].get<float>();
    if (j.contains("m_hideHand"))        m_hideHand        = j["m_hideHand"].get<bool>();
    if (j.contains("m_overlayToggle"))   m_overlayToggle   = j["m_overlayToggle"].get<bool>();
    if (j.contains("m_posX"))            m_posX            = j["m_posX"].get<float>();
    if (j.contains("m_posY"))            m_posY            = j["m_posY"].get<float>();
    if (j.contains("m_scale"))           m_scale           = j["m_scale"].get<float>();
    if (j.contains("m_opacity"))         m_opacity         = j["m_opacity"].get<float>();

    m_defaultZoomFov  = std::clamp(m_defaultZoomFov, 1.0f, 179.0f);
    m_targetZoomFov   = std::clamp(m_targetZoomFov, 1.0f, 179.0f);
    m_animSpeed       = std::clamp(m_animSpeed, 0.05f, 1.0f);
    m_lowSensStrength = std::clamp(m_lowSensStrength, 0.05f, 1.0f);
    m_scale           = std::clamp(m_scale, 0.1f, 5.0f);
    m_opacity         = std::clamp(m_opacity, 0.0f, 1.0f);
}

void ZoomModule::saveConfig(nlohmann::json& j) {
    Module::saveConfig(j);

    j["m_defaultZoomFov"]  = m_defaultZoomFov;
    j["m_targetZoomFov"]   = m_targetZoomFov;
    j["m_animSpeed"]       = m_animSpeed;
    j["m_lowSens"]         = m_lowSens;
    j["m_lowSensStrength"] = m_lowSensStrength;
    j["m_hideHand"]        = m_hideHand;
    j["m_overlayToggle"]   = m_overlayToggle;
    j["m_posX"]            = m_posX;
    j["m_posY"]            = m_posY;
    j["m_scale"]           = m_scale;
    j["m_opacity"]         = m_opacity;
}
