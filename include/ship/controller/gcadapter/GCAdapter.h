#pragma once

// GCAdapter
// ---------
// Native driver for the Nintendo Wii U / Switch GameCube Controller Adapter
// (WUP-028, USB 057e:0337) and clones that speak the same protocol. Talks to
// the adapter directly over libusb, the same way Dolphin/Slippi do:
//
//   * The adapter is claimed with libusb (auto-detaching usbhid on Linux), so
//     it needs no hidraw node. This matters on Linux, where the common
//     "gcadapter" udev rule detaches the kernel HID driver, leaving no
//     /dev/hidraw* for hidapi (Raphnet's transport) to open.
//   * A background thread does 37-byte interrupt reads (~125 Hz) and keeps the
//     latest state of all four adapter ports; the game thread reads the latest
//     snapshots without ever blocking on USB.
//   * The adapter is a regular physical device for the mapping system
//     (PhysicalDeviceType::GameCubeAdapter): the GCAdapter*Mapping classes read
//     the snapshots routed to their game port, exactly like SDL gamepads, so
//     buttons/sticks are remapped in the input editor and OR'd with every other
//     device on the port. By default adapter port N feeds game port N; the
//     routing is changeable per game port.
//
// Wire protocol (host -> adapter on the OUT endpoint, adapter -> host on IN):
//   TX 0x13                 start polling (must be sent once after claiming)
//   TX 0x11 r0 r1 r2 r3     rumble on/off per port
//   RX 0x21 + 4 x 9 bytes   per port: status, buttons1, buttons2, lx, ly,
//                           cx, cy, lTrig, rTrig
//
// Compiled to a stub (Start() returns false) when libusb is unavailable
// (LUS_HAS_GCADAPTER undefined): Android, iOS and the UWP/Xbox build.

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace Ship {

constexpr uint16_t gGCAdapterVid = 0x057e;
constexpr uint16_t gGCAdapterPid = 0x0337;
constexpr int gGCAdapterPorts = 4;
constexpr size_t gGCAdapterPayloadSize = 37;

constexpr uint8_t gGCAdapterCmdRumble = 0x11;
constexpr uint8_t gGCAdapterCmdInit = 0x13;
constexpr uint8_t gGCAdapterPayloadId = 0x21;

// Raw GameCube button bits, as (buttons1 | buttons2 << 8).
enum GCButton : uint16_t {
    GC_A = 1 << 0,
    GC_B = 1 << 1,
    GC_X = 1 << 2,
    GC_Y = 1 << 3,
    GC_DPAD_LEFT = 1 << 4,
    GC_DPAD_RIGHT = 1 << 5,
    GC_DPAD_DOWN = 1 << 6,
    GC_DPAD_UP = 1 << 7,
    GC_START = 1 << 8,
    GC_Z = 1 << 9,
    GC_R = 1 << 10,
    GC_L = 1 << 11,
};

enum class GCControllerType : uint8_t { None = 0, Wired = 1, Wavebird = 2 };

// Decoded state of one adapter port.
struct GCPortState {
    GCControllerType Type = GCControllerType::None;
    uint16_t Buttons = 0;
    uint8_t StickX = 128;
    uint8_t StickY = 128;
    uint8_t CStickX = 128;
    uint8_t CStickY = 128;
    uint8_t TriggerL = 0;
    uint8_t TriggerR = 0;
};

// Analog inputs, as seen by the mapping layer. Stick/C-stick axes are signed
// deflection from the neutral point (Y is positive = up); triggers are 0..255.
enum GCAxis : uint8_t {
    GCAxis_StickX,
    GCAxis_StickY,
    GCAxis_CStickX,
    GCAxis_CStickY,
    GCAxis_TriggerL,
    GCAxis_TriggerR,
    GCAxis_Count,
};

// Stick/C-stick neutral position captured when a controller is first seen.
struct GCPortOrigin {
    uint8_t StickX = 128;
    uint8_t StickY = 128;
    uint8_t CStickX = 128;
    uint8_t CStickY = 128;
};

// One adapter port's decoded state together with its neutral point.
struct GCSnapshot {
    uint8_t AdapterPort = 0;
    GCPortState State;
    GCPortOrigin Origin;
};

class GCAdapter {
  public:
    GCAdapter();
    ~GCAdapter();

    GCAdapter(const GCAdapter&) = delete;
    GCAdapter& operator=(const GCAdapter&) = delete;

    // Initializes libusb and starts the reader thread. Returns false only if
    // libusb is unavailable or failed to initialize; "no adapter plugged in" is
    // success (the thread waits for a hotplug). Idempotent.
    bool Start();

    // Stops the thread, turns rumble off, releases the interface. Idempotent.
    void Stop();

    bool IsConnected() const;

    // Snapshots of every adapter port that is routed to `gamePort` and has a
    // controller plugged in.
    std::vector<GCSnapshot> GetSnapshotsForGamePort(uint8_t gamePort) const;

    // Routing: which adapter ports feed a game port (bit N = adapter port N).
    // Defaults to adapter port N -> game port N.
    void SetPortRouting(uint8_t gamePort, uint8_t adapterPortMask);
    uint8_t GetPortRouting(uint8_t gamePort) const;

    // Type of the controller plugged into adapter port `port` (None if empty).
    GCControllerType GetPortType(uint8_t port) const;

    // Rumble on/off for one port. The adapter needs its second USB cable
    // (5V) connected for rumble to physically work; otherwise this is a no-op
    // on the hardware side.
    void SetRumble(uint8_t port, bool on);

    // ---- Pure helpers (no USB access; unit-testable) ----

    // Decodes a 37-byte payload. Returns false if `len` is wrong or the
    // payload id is not 0x21.
    static bool DecodePayload(const uint8_t* buf, size_t len, std::array<GCPortState, gGCAdapterPorts>& out);

    // Signed deflection (or 0..255 for triggers) of `axis` in `snapshot`.
    static int AxisValue(const GCSnapshot& snapshot, GCAxis axis);

    // Magnitude 0..1 of `axis` in direction `sign` (+1 / -1); 0 if it is
    // deflected the other way. Sticks span +/-100, triggers 0..200.
    static float AxisMagnitude(const GCSnapshot& snapshot, GCAxis axis, int sign);

    // Captures the stick neutral point from a first sample, falling back to
    // 128 for any axis that reads implausibly far from center.
    static GCPortOrigin CaptureOrigin(const GCPortState& state);

  private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace Ship
