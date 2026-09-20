#pragma once

// GCAdapterRumbleMapping
// ----------------------
// Rumble for a controller plugged into a native GCAdapter port. The adapter's
// rumble is binary (on/off per port), so the low/high intensity percentages are
// stored for config compatibility but ignored. Holds a weak_ptr so a torn-down
// adapter decays to a silent no-op. Not persisted: ControlDeck::Init installs it
// each launch from live state.

#include <cstdint>
#include <memory>
#include <string>

#include "ship/controller/controldevice/controller/mapping/ControllerRumbleMapping.h"

namespace Ship {

class GCAdapter;

class GCAdapterRumbleMapping final : public ControllerRumbleMapping {
  public:
    GCAdapterRumbleMapping(uint8_t portIndex, uint8_t lowFrequencyIntensityPercentage,
                           uint8_t highFrequencyIntensityPercentage, std::weak_ptr<GCAdapter> adapter,
                           uint8_t adapterPort);
    ~GCAdapterRumbleMapping() = default;

    void StartRumble() override;
    void StopRumble() override;

    std::string GetRumbleMappingId() override;
    void SaveToConfig() override;    // intentional no-op
    void EraseFromConfig() override; // intentional no-op

    std::string GetPhysicalDeviceName() override;

  private:
    std::weak_ptr<GCAdapter> mAdapter;
    uint8_t mAdapterPort;
};

} // namespace Ship
