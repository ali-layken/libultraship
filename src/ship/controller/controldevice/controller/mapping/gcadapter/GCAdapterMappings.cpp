#include "ship/controller/controldevice/controller/mapping/gcadapter/GCAdapterMappings.h"

#include <algorithm>

#include "ship/Context.h"
#include "ship/config/ConsoleVariable.h"
#include "ship/controller/controldeck/ControlDeck.h"
#include "ship/utils/StringHelper.h"
#include "ship/window/gui/IconsFontAwesome4.h"

namespace Ship {

namespace {

std::shared_ptr<GCAdapter> Adapter() {
    return Context::GetInstance()->GetControlDeck()->GetGCAdapter();
}

std::string ConfigKey(const char* group, const std::string& id, const char* leaf) {
    return StringHelper::Sprintf("%s.%s.%s.%s", CVAR_PREFIX_CONTROLLERS, group, id.c_str(), leaf);
}

// Bit index of a single-bit GCButton value, used in mapping ids.
int ButtonIndex(uint16_t gcButton) {
    int i = 0;
    while (gcButton > 1) {
        gcButton >>= 1;
        ++i;
    }
    return i;
}

const char* AxisDirectionSuffix(int32_t axisDirection) {
    return axisDirection > 0 ? "P" : "N";
}

} // namespace

// ---- Button base ----

GCAdapterButtonToAnyMapping::GCAdapterButtonToAnyMapping(uint16_t gcButton)
    : ControllerInputMapping(PhysicalDeviceType::GameCubeAdapter), mGCButton(gcButton) {
}

GCAdapterButtonToAnyMapping::~GCAdapterButtonToAnyMapping() {
}

bool GCAdapterButtonToAnyMapping::IsPressed(uint8_t gamePort) const {
    auto adapter = Adapter();
    if (adapter == nullptr) {
        return false;
    }
    for (const auto& snap : adapter->GetSnapshotsForGamePort(gamePort)) {
        if (snap.State.Buttons & mGCButton) {
            return true;
        }
    }
    return false;
}

std::string GCAdapterButtonToAnyMapping::GetPhysicalInputName() {
    switch (mGCButton) {
        case GC_A:
            return "A";
        case GC_B:
            return "B";
        case GC_X:
            return "X";
        case GC_Y:
            return "Y";
        case GC_Z:
            return "Z";
        case GC_L:
            return "L";
        case GC_R:
            return "R";
        case GC_START:
            return "Start";
        case GC_DPAD_UP:
            return StringHelper::Sprintf("D-Pad %s", ICON_FA_ARROW_UP);
        case GC_DPAD_DOWN:
            return StringHelper::Sprintf("D-Pad %s", ICON_FA_ARROW_DOWN);
        case GC_DPAD_LEFT:
            return StringHelper::Sprintf("D-Pad %s", ICON_FA_ARROW_LEFT);
        case GC_DPAD_RIGHT:
            return StringHelper::Sprintf("D-Pad %s", ICON_FA_ARROW_RIGHT);
        default:
            return StringHelper::Sprintf("B%d", ButtonIndex(mGCButton));
    }
}

std::string GCAdapterButtonToAnyMapping::GetPhysicalDeviceName() {
    return "GameCube Adapter";
}

// ---- Axis base ----

GCAdapterAxisDirectionToAnyMapping::GCAdapterAxisDirectionToAnyMapping(int32_t gcAxis, int32_t axisDirection)
    : ControllerInputMapping(PhysicalDeviceType::GameCubeAdapter), mGCAxis(static_cast<GCAxis>(gcAxis)),
      mAxisDirection(axisDirection > 0 ? 1 : -1) {
}

GCAdapterAxisDirectionToAnyMapping::~GCAdapterAxisDirectionToAnyMapping() {
}

bool GCAdapterAxisDirectionToAnyMapping::AxisIsTrigger() const {
    return mGCAxis == GCAxis_TriggerL || mGCAxis == GCAxis_TriggerR;
}

float GCAdapterAxisDirectionToAnyMapping::GetMagnitude(uint8_t gamePort) const {
    auto adapter = Adapter();
    if (adapter == nullptr) {
        return 0.0f;
    }
    float best = 0.0f;
    for (const auto& snap : adapter->GetSnapshotsForGamePort(gamePort)) {
        best = std::max(best, GCAdapter::AxisMagnitude(snap, mGCAxis, mAxisDirection));
    }
    return best;
}

std::string GCAdapterAxisDirectionToAnyMapping::GetPhysicalInputName() {
    const bool positive = mAxisDirection > 0;
    switch (mGCAxis) {
        case GCAxis_StickX:
            return StringHelper::Sprintf("Stick %s", positive ? ICON_FA_ARROW_RIGHT : ICON_FA_ARROW_LEFT);
        case GCAxis_StickY:
            return StringHelper::Sprintf("Stick %s", positive ? ICON_FA_ARROW_UP : ICON_FA_ARROW_DOWN);
        case GCAxis_CStickX:
            return StringHelper::Sprintf("C-Stick %s", positive ? ICON_FA_ARROW_RIGHT : ICON_FA_ARROW_LEFT);
        case GCAxis_CStickY:
            return StringHelper::Sprintf("C-Stick %s", positive ? ICON_FA_ARROW_UP : ICON_FA_ARROW_DOWN);
        case GCAxis_TriggerL:
            return "L Trigger";
        case GCAxis_TriggerR:
            return "R Trigger";
        default:
            return StringHelper::Sprintf("Axis %d", static_cast<int>(mGCAxis));
    }
}

std::string GCAdapterAxisDirectionToAnyMapping::GetPhysicalDeviceName() {
    return "GameCube Adapter";
}

// ---- Button -> N64 button ----

GCAdapterButtonToButtonMapping::GCAdapterButtonToButtonMapping(uint8_t portIndex, CONTROLLERBUTTONS_T bitmask,
                                                               uint16_t gcButton)
    : ControllerInputMapping(PhysicalDeviceType::GameCubeAdapter), GCAdapterButtonToAnyMapping(gcButton),
      ControllerButtonMapping(PhysicalDeviceType::GameCubeAdapter, portIndex, bitmask) {
}

void GCAdapterButtonToButtonMapping::UpdatePad(CONTROLLERBUTTONS_T& padButtons) {
    if (Context::GetInstance()->GetControlDeck()->GamepadGameInputBlocked()) {
        return;
    }
    if (IsPressed(mPortIndex)) {
        padButtons |= mBitmask;
    }
}

int8_t GCAdapterButtonToButtonMapping::GetMappingType() {
    return MAPPING_TYPE_GAMEPAD;
}

std::string GCAdapterButtonToButtonMapping::GetButtonMappingId() {
    return StringHelper::Sprintf("P%d-B%d-GCB%d", mPortIndex, mBitmask, ButtonIndex(mGCButton));
}

void GCAdapterButtonToButtonMapping::SaveToConfig() {
    auto cvars = Context::GetInstance()->GetConsoleVariables();
    const std::string id = GetButtonMappingId();
    cvars->SetString(ConfigKey("ButtonMappings", id, "ButtonMappingClass").c_str(), "GCAdapterButtonToButtonMapping");
    cvars->SetInteger(ConfigKey("ButtonMappings", id, "Bitmask").c_str(), mBitmask);
    cvars->SetInteger(ConfigKey("ButtonMappings", id, "GCButton").c_str(), ButtonIndex(mGCButton));
    cvars->Save();
}

void GCAdapterButtonToButtonMapping::EraseFromConfig() {
    auto cvars = Context::GetInstance()->GetConsoleVariables();
    const std::string id = GetButtonMappingId();
    cvars->ClearVariable(ConfigKey("ButtonMappings", id, "ButtonMappingClass").c_str());
    cvars->ClearVariable(ConfigKey("ButtonMappings", id, "Bitmask").c_str());
    cvars->ClearVariable(ConfigKey("ButtonMappings", id, "GCButton").c_str());
    cvars->Save();
}

std::string GCAdapterButtonToButtonMapping::GetPhysicalDeviceName() {
    return GCAdapterButtonToAnyMapping::GetPhysicalDeviceName();
}

std::string GCAdapterButtonToButtonMapping::GetPhysicalInputName() {
    return GCAdapterButtonToAnyMapping::GetPhysicalInputName();
}

// ---- Axis direction -> N64 button ----

GCAdapterAxisDirectionToButtonMapping::GCAdapterAxisDirectionToButtonMapping(uint8_t portIndex,
                                                                             CONTROLLERBUTTONS_T bitmask,
                                                                             int32_t gcAxis, int32_t axisDirection)
    : ControllerInputMapping(PhysicalDeviceType::GameCubeAdapter),
      GCAdapterAxisDirectionToAnyMapping(gcAxis, axisDirection),
      ControllerButtonMapping(PhysicalDeviceType::GameCubeAdapter, portIndex, bitmask) {
}

void GCAdapterAxisDirectionToButtonMapping::UpdatePad(CONTROLLERBUTTONS_T& padButtons) {
    if (Context::GetInstance()->GetControlDeck()->GamepadGameInputBlocked()) {
        return;
    }

    // Same user-facing thresholds as SDL gamepads.
    auto settings = Context::GetInstance()->GetControlDeck()->GetGlobalSDLDeviceSettings();
    const int32_t thresholdPercentage =
        AxisIsTrigger() ? settings->GetTriggerAxisThresholdPercentage() : settings->GetStickAxisThresholdPercentage();
    if (GetMagnitude(mPortIndex) * 100.0f > static_cast<float>(thresholdPercentage)) {
        padButtons |= mBitmask;
    }
}

int8_t GCAdapterAxisDirectionToButtonMapping::GetMappingType() {
    return MAPPING_TYPE_GAMEPAD;
}

std::string GCAdapterAxisDirectionToButtonMapping::GetButtonMappingId() {
    return StringHelper::Sprintf("P%d-B%d-GCA%d-AD%s", mPortIndex, mBitmask, static_cast<int>(mGCAxis),
                                 AxisDirectionSuffix(mAxisDirection));
}

void GCAdapterAxisDirectionToButtonMapping::SaveToConfig() {
    auto cvars = Context::GetInstance()->GetConsoleVariables();
    const std::string id = GetButtonMappingId();
    cvars->SetString(ConfigKey("ButtonMappings", id, "ButtonMappingClass").c_str(),
                     "GCAdapterAxisDirectionToButtonMapping");
    cvars->SetInteger(ConfigKey("ButtonMappings", id, "Bitmask").c_str(), mBitmask);
    cvars->SetInteger(ConfigKey("ButtonMappings", id, "GCAxis").c_str(), static_cast<int>(mGCAxis));
    cvars->SetInteger(ConfigKey("ButtonMappings", id, "AxisDirection").c_str(), mAxisDirection);
    cvars->Save();
}

void GCAdapterAxisDirectionToButtonMapping::EraseFromConfig() {
    auto cvars = Context::GetInstance()->GetConsoleVariables();
    const std::string id = GetButtonMappingId();
    cvars->ClearVariable(ConfigKey("ButtonMappings", id, "ButtonMappingClass").c_str());
    cvars->ClearVariable(ConfigKey("ButtonMappings", id, "Bitmask").c_str());
    cvars->ClearVariable(ConfigKey("ButtonMappings", id, "GCAxis").c_str());
    cvars->ClearVariable(ConfigKey("ButtonMappings", id, "AxisDirection").c_str());
    cvars->Save();
}

std::string GCAdapterAxisDirectionToButtonMapping::GetPhysicalDeviceName() {
    return GCAdapterAxisDirectionToAnyMapping::GetPhysicalDeviceName();
}

std::string GCAdapterAxisDirectionToButtonMapping::GetPhysicalInputName() {
    return GCAdapterAxisDirectionToAnyMapping::GetPhysicalInputName();
}

// ---- Button -> N64 stick direction ----

GCAdapterButtonToAxisDirectionMapping::GCAdapterButtonToAxisDirectionMapping(uint8_t portIndex, StickIndex stickIndex,
                                                                             Direction direction, uint16_t gcButton)
    : ControllerInputMapping(PhysicalDeviceType::GameCubeAdapter), GCAdapterButtonToAnyMapping(gcButton),
      ControllerAxisDirectionMapping(PhysicalDeviceType::GameCubeAdapter, portIndex, stickIndex, direction) {
}

float GCAdapterButtonToAxisDirectionMapping::GetNormalizedAxisDirectionValue() {
    if (Context::GetInstance()->GetControlDeck()->GamepadGameInputBlocked()) {
        return 0.0f;
    }
    return IsPressed(mPortIndex) ? MAX_AXIS_RANGE : 0.0f;
}

std::string GCAdapterButtonToAxisDirectionMapping::GetAxisDirectionMappingId() {
    return StringHelper::Sprintf("P%d-S%d-D%d-GCB%d", mPortIndex, mStickIndex, mDirection, ButtonIndex(mGCButton));
}

void GCAdapterButtonToAxisDirectionMapping::SaveToConfig() {
    auto cvars = Context::GetInstance()->GetConsoleVariables();
    const std::string id = GetAxisDirectionMappingId();
    cvars->SetString(ConfigKey("AxisDirectionMappings", id, "AxisDirectionMappingClass").c_str(),
                     "GCAdapterButtonToAxisDirectionMapping");
    cvars->SetInteger(ConfigKey("AxisDirectionMappings", id, "Stick").c_str(), mStickIndex);
    cvars->SetInteger(ConfigKey("AxisDirectionMappings", id, "Direction").c_str(), mDirection);
    cvars->SetInteger(ConfigKey("AxisDirectionMappings", id, "GCButton").c_str(), ButtonIndex(mGCButton));
    cvars->Save();
}

void GCAdapterButtonToAxisDirectionMapping::EraseFromConfig() {
    auto cvars = Context::GetInstance()->GetConsoleVariables();
    const std::string id = GetAxisDirectionMappingId();
    cvars->ClearVariable(ConfigKey("AxisDirectionMappings", id, "AxisDirectionMappingClass").c_str());
    cvars->ClearVariable(ConfigKey("AxisDirectionMappings", id, "Stick").c_str());
    cvars->ClearVariable(ConfigKey("AxisDirectionMappings", id, "Direction").c_str());
    cvars->ClearVariable(ConfigKey("AxisDirectionMappings", id, "GCButton").c_str());
    cvars->Save();
}

int8_t GCAdapterButtonToAxisDirectionMapping::GetMappingType() {
    return MAPPING_TYPE_GAMEPAD;
}

std::string GCAdapterButtonToAxisDirectionMapping::GetPhysicalDeviceName() {
    return GCAdapterButtonToAnyMapping::GetPhysicalDeviceName();
}

std::string GCAdapterButtonToAxisDirectionMapping::GetPhysicalInputName() {
    return GCAdapterButtonToAnyMapping::GetPhysicalInputName();
}

// ---- Axis direction -> N64 stick direction ----

GCAdapterAxisDirectionToAxisDirectionMapping::GCAdapterAxisDirectionToAxisDirectionMapping(
    uint8_t portIndex, StickIndex stickIndex, Direction direction, int32_t gcAxis, int32_t axisDirection)
    : ControllerInputMapping(PhysicalDeviceType::GameCubeAdapter),
      ControllerAxisDirectionMapping(PhysicalDeviceType::GameCubeAdapter, portIndex, stickIndex, direction),
      GCAdapterAxisDirectionToAnyMapping(gcAxis, axisDirection) {
}

float GCAdapterAxisDirectionToAxisDirectionMapping::GetNormalizedAxisDirectionValue() {
    if (Context::GetInstance()->GetControlDeck()->GamepadGameInputBlocked()) {
        return 0.0f;
    }
    return GetMagnitude(mPortIndex) * MAX_AXIS_RANGE;
}

std::string GCAdapterAxisDirectionToAxisDirectionMapping::GetAxisDirectionMappingId() {
    return StringHelper::Sprintf("P%d-S%d-D%d-GCA%d-AD%s", mPortIndex, mStickIndex, mDirection,
                                 static_cast<int>(mGCAxis), AxisDirectionSuffix(mAxisDirection));
}

void GCAdapterAxisDirectionToAxisDirectionMapping::SaveToConfig() {
    auto cvars = Context::GetInstance()->GetConsoleVariables();
    const std::string id = GetAxisDirectionMappingId();
    cvars->SetString(ConfigKey("AxisDirectionMappings", id, "AxisDirectionMappingClass").c_str(),
                     "GCAdapterAxisDirectionToAxisDirectionMapping");
    cvars->SetInteger(ConfigKey("AxisDirectionMappings", id, "Stick").c_str(), mStickIndex);
    cvars->SetInteger(ConfigKey("AxisDirectionMappings", id, "Direction").c_str(), mDirection);
    cvars->SetInteger(ConfigKey("AxisDirectionMappings", id, "GCAxis").c_str(), static_cast<int>(mGCAxis));
    cvars->SetInteger(ConfigKey("AxisDirectionMappings", id, "AxisDirection").c_str(), mAxisDirection);
    cvars->Save();
}

void GCAdapterAxisDirectionToAxisDirectionMapping::EraseFromConfig() {
    auto cvars = Context::GetInstance()->GetConsoleVariables();
    const std::string id = GetAxisDirectionMappingId();
    cvars->ClearVariable(ConfigKey("AxisDirectionMappings", id, "AxisDirectionMappingClass").c_str());
    cvars->ClearVariable(ConfigKey("AxisDirectionMappings", id, "Stick").c_str());
    cvars->ClearVariable(ConfigKey("AxisDirectionMappings", id, "Direction").c_str());
    cvars->ClearVariable(ConfigKey("AxisDirectionMappings", id, "GCAxis").c_str());
    cvars->ClearVariable(ConfigKey("AxisDirectionMappings", id, "AxisDirection").c_str());
    cvars->Save();
}

int8_t GCAdapterAxisDirectionToAxisDirectionMapping::GetMappingType() {
    return MAPPING_TYPE_GAMEPAD;
}

std::string GCAdapterAxisDirectionToAxisDirectionMapping::GetPhysicalDeviceName() {
    return GCAdapterAxisDirectionToAnyMapping::GetPhysicalDeviceName();
}

std::string GCAdapterAxisDirectionToAxisDirectionMapping::GetPhysicalInputName() {
    return GCAdapterAxisDirectionToAnyMapping::GetPhysicalInputName();
}

} // namespace Ship
