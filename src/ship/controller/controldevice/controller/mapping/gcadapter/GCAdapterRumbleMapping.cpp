#include "ship/controller/controldevice/controller/mapping/gcadapter/GCAdapterRumbleMapping.h"

#include "ship/controller/gcadapter/GCAdapter.h"
#include "ship/utils/StringHelper.h"

namespace Ship {

GCAdapterRumbleMapping::GCAdapterRumbleMapping(uint8_t portIndex, uint8_t lowFrequencyIntensityPercentage,
                                               uint8_t highFrequencyIntensityPercentage,
                                               std::weak_ptr<GCAdapter> adapter, uint8_t adapterPort)
    : ControllerRumbleMapping(PhysicalDeviceType::GameCubeAdapter, portIndex, lowFrequencyIntensityPercentage,
                              highFrequencyIntensityPercentage),
      mAdapter(std::move(adapter)), mAdapterPort(adapterPort) {
}

void GCAdapterRumbleMapping::StartRumble() {
    if (auto a = mAdapter.lock()) {
        a->SetRumble(mAdapterPort, true);
    }
}

void GCAdapterRumbleMapping::StopRumble() {
    if (auto a = mAdapter.lock()) {
        a->SetRumble(mAdapterPort, false);
    }
}

std::string GCAdapterRumbleMapping::GetRumbleMappingId() {
    return StringHelper::Sprintf("GCAdapterP%d", mPortIndex);
}

void GCAdapterRumbleMapping::SaveToConfig() {
}

void GCAdapterRumbleMapping::EraseFromConfig() {
}

std::string GCAdapterRumbleMapping::GetPhysicalDeviceName() {
    return "GameCube Adapter (native)";
}

} // namespace Ship
