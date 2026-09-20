#pragma once

// Mappings from a native GameCube adapter to N64 buttons / stick directions.
// They parallel the SDL* mappings: they read the adapter snapshots routed to
// their game port and are OR'd / max'd with every other device on the port.
//
//   GCAdapterButtonToAnyMapping             GC digital button (A, B, X, Y, Z, L, R, Start, D-pad)
//   GCAdapterAxisDirectionToAnyMapping      GC analog axis + direction (stick, C-stick, triggers)
//
//   GCAdapterButtonToButtonMapping          -> N64 button
//   GCAdapterAxisDirectionToButtonMapping   -> N64 button (past a threshold)
//   GCAdapterButtonToAxisDirectionMapping   -> N64 stick direction (full deflection)
//   GCAdapterAxisDirectionToAxisDirectionMapping -> N64 stick direction (analog)

#include <cstdint>
#include <string>

#include "ship/controller/controldevice/controller/mapping/ControllerAxisDirectionMapping.h"
#include "ship/controller/controldevice/controller/mapping/ControllerButtonMapping.h"
#include "ship/controller/controldevice/controller/mapping/ControllerInputMapping.h"
#include "ship/controller/gcadapter/GCAdapter.h"

namespace Ship {

class GCAdapterButtonToAnyMapping : virtual public ControllerInputMapping {
  public:
    // `gcButton` is a GCButton bit value.
    explicit GCAdapterButtonToAnyMapping(uint16_t gcButton);
    virtual ~GCAdapterButtonToAnyMapping();
    std::string GetPhysicalInputName() override;
    std::string GetPhysicalDeviceName() override;

  protected:
    bool IsPressed(uint8_t gamePort) const;

    uint16_t mGCButton;
};

class GCAdapterAxisDirectionToAnyMapping : virtual public ControllerInputMapping {
  public:
    // `axisDirection` is -1 (negative) or +1 (positive); Y axes are positive = up.
    GCAdapterAxisDirectionToAnyMapping(int32_t gcAxis, int32_t axisDirection);
    virtual ~GCAdapterAxisDirectionToAnyMapping();
    std::string GetPhysicalInputName() override;
    std::string GetPhysicalDeviceName() override;
    bool AxisIsTrigger() const;

  protected:
    // Largest magnitude (0..1) of this axis direction across the adapter ports
    // routed to `gamePort`.
    float GetMagnitude(uint8_t gamePort) const;

    GCAxis mGCAxis;
    int32_t mAxisDirection;
};

class GCAdapterButtonToButtonMapping final : public GCAdapterButtonToAnyMapping, public ControllerButtonMapping {
  public:
    GCAdapterButtonToButtonMapping(uint8_t portIndex, CONTROLLERBUTTONS_T bitmask, uint16_t gcButton);
    void UpdatePad(CONTROLLERBUTTONS_T& padButtons) override;
    int8_t GetMappingType() override;
    std::string GetButtonMappingId() override;
    void SaveToConfig() override;
    void EraseFromConfig() override;
    std::string GetPhysicalDeviceName() override;
    std::string GetPhysicalInputName() override;
};

class GCAdapterAxisDirectionToButtonMapping final : public GCAdapterAxisDirectionToAnyMapping,
                                                    public ControllerButtonMapping {
  public:
    GCAdapterAxisDirectionToButtonMapping(uint8_t portIndex, CONTROLLERBUTTONS_T bitmask, int32_t gcAxis,
                                          int32_t axisDirection);
    void UpdatePad(CONTROLLERBUTTONS_T& padButtons) override;
    int8_t GetMappingType() override;
    std::string GetButtonMappingId() override;
    void SaveToConfig() override;
    void EraseFromConfig() override;
    std::string GetPhysicalDeviceName() override;
    std::string GetPhysicalInputName() override;
};

class GCAdapterButtonToAxisDirectionMapping final : public GCAdapterButtonToAnyMapping,
                                                    public ControllerAxisDirectionMapping {
  public:
    GCAdapterButtonToAxisDirectionMapping(uint8_t portIndex, StickIndex stickIndex, Direction direction,
                                          uint16_t gcButton);
    float GetNormalizedAxisDirectionValue() override;
    std::string GetAxisDirectionMappingId() override;
    void SaveToConfig() override;
    void EraseFromConfig() override;
    int8_t GetMappingType() override;
    std::string GetPhysicalDeviceName() override;
    std::string GetPhysicalInputName() override;
};

class GCAdapterAxisDirectionToAxisDirectionMapping final : public ControllerAxisDirectionMapping,
                                                           public GCAdapterAxisDirectionToAnyMapping {
  public:
    GCAdapterAxisDirectionToAxisDirectionMapping(uint8_t portIndex, StickIndex stickIndex, Direction direction,
                                                 int32_t gcAxis, int32_t axisDirection);
    float GetNormalizedAxisDirectionValue() override;
    std::string GetAxisDirectionMappingId() override;
    void SaveToConfig() override;
    void EraseFromConfig() override;
    int8_t GetMappingType() override;
    std::string GetPhysicalDeviceName() override;
    std::string GetPhysicalInputName() override;
};

} // namespace Ship
