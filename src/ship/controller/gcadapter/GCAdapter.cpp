#include "ship/controller/gcadapter/GCAdapter.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdlib>

#if defined(LUS_HAS_GCADAPTER)
#include <libusb.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#endif

namespace Ship {

namespace {

// GC sticks report 0..255 around ~128 with a gate reaching about +/-100; the
// analog triggers travel roughly 0..200.
constexpr float kGCStickRange = 100.0f;
constexpr float kGCTriggerRange = 200.0f;

// A first-sample origin further than this from 128 is treated as "stick was
// being held while plugging in" and ignored in favor of the nominal center.
constexpr int kOriginSanity = 40;

uint8_t SaneOrigin(uint8_t raw) {
    return std::abs(static_cast<int>(raw) - 128) > kOriginSanity ? 128 : raw;
}

} // namespace

bool GCAdapter::DecodePayload(const uint8_t* buf, size_t len, std::array<GCPortState, gGCAdapterPorts>& out) {
    if (buf == nullptr || len != gGCAdapterPayloadSize || buf[0] != gGCAdapterPayloadId) {
        return false;
    }
    for (int port = 0; port < gGCAdapterPorts; ++port) {
        const uint8_t* p = buf + 1 + port * 9;
        GCPortState& s = out[port];
        const uint8_t type = (p[0] >> 4) & 0x3;
        s.Type = type == 1 ? GCControllerType::Wired : type == 2 ? GCControllerType::Wavebird : GCControllerType::None;
        s.Buttons = static_cast<uint16_t>(p[1] | (p[2] << 8));
        s.StickX = p[3];
        s.StickY = p[4];
        s.CStickX = p[5];
        s.CStickY = p[6];
        s.TriggerL = p[7];
        s.TriggerR = p[8];
    }
    return true;
}

GCPortOrigin GCAdapter::CaptureOrigin(const GCPortState& state) {
    GCPortOrigin o;
    o.StickX = SaneOrigin(state.StickX);
    o.StickY = SaneOrigin(state.StickY);
    o.CStickX = SaneOrigin(state.CStickX);
    o.CStickY = SaneOrigin(state.CStickY);
    return o;
}

int GCAdapter::AxisValue(const GCSnapshot& snap, GCAxis axis) {
    const GCPortState& s = snap.State;
    const GCPortOrigin& o = snap.Origin;
    switch (axis) {
        case GCAxis_StickX:
            return static_cast<int>(s.StickX) - o.StickX;
        case GCAxis_StickY:
            return static_cast<int>(s.StickY) - o.StickY;
        case GCAxis_CStickX:
            return static_cast<int>(s.CStickX) - o.CStickX;
        case GCAxis_CStickY:
            return static_cast<int>(s.CStickY) - o.CStickY;
        case GCAxis_TriggerL:
            return s.TriggerL;
        case GCAxis_TriggerR:
            return s.TriggerR;
        default:
            return 0;
    }
}

float GCAdapter::AxisMagnitude(const GCSnapshot& snap, GCAxis axis, int sign) {
    const int v = AxisValue(snap, axis) * (sign < 0 ? -1 : 1);
    if (v <= 0) {
        return 0.0f;
    }
    const float range = (axis == GCAxis_TriggerL || axis == GCAxis_TriggerR) ? kGCTriggerRange : kGCStickRange;
    return std::min(1.0f, static_cast<float>(v) / range);
}

#if defined(LUS_HAS_GCADAPTER)

struct GCAdapter::Impl {
    libusb_context* Ctx = nullptr;
    libusb_device_handle* Handle = nullptr;
    libusb_hotplug_callback_handle HotplugHandle = 0;
    bool HotplugRegistered = false;
    uint8_t EpIn = 0x81;
    uint8_t EpOut = 0x02;

    std::thread Thread;
    std::atomic<bool> Running{ false };
    std::atomic<bool> AttachHint{ true };
    std::atomic<bool> Connected{ false };
    std::atomic<uint8_t> RumbleMask{ 0 };
    std::atomic<bool> RumbleDirty{ false };

    mutable std::mutex StateLock;
    std::array<GCPortState, gGCAdapterPorts> State{};
    std::array<GCPortOrigin, gGCAdapterPorts> Origins{};
    std::array<uint8_t, gGCAdapterPorts> Routing = { 1 << 0, 1 << 1, 1 << 2, 1 << 3 };

    bool AccessWarned = false;
    bool OpenWarned = false;
    // Device is present but open/init failed or the link dropped; retry with backoff even under hotplug,
    // since the arrival callback only fires once per attach and transient IO errors are common on macOS.
    bool RetryPending = false;
    bool InitWarned = false;

    static int LIBUSB_CALL OnHotplug(libusb_context*, libusb_device*, libusb_hotplug_event, void* user) {
        static_cast<Impl*>(user)->AttachHint = true;
        return 0; // keep the callback registered
    }

    bool TryOpen();
    void CloseDevice();
    void SendRumble();
    void Run();
};

// Platform-specific fix for "the adapter is plugged in but we cannot open or
// claim it". Kept next to the warnings so the two stay in sync with
// BUILDING.md's "GameCube adapter device access" section.
#if defined(_WIN32)
constexpr const char* kGCAccessHint = "the adapter must be bound to the WinUSB driver (use Zadig or Dolphin's "
                                      "adapter driver installer)";
#elif defined(__linux__)
constexpr const char* kGCAccessHint = "install a udev rule granting access, e.g. SUBSYSTEM==\"usb\", "
                                      "ATTRS{idVendor}==\"057e\", ATTRS{idProduct}==\"0337\", MODE=\"0660\", "
                                      "TAG+=\"uaccess\" in /etc/udev/rules.d/51-gcadapter.rules, then replug "
                                      "(see BUILDING.md)";
#else
constexpr const char* kGCAccessHint = "another process may already own the device";
#endif

bool GCAdapter::Impl::TryOpen() {
    // Enumerate rather than using libusb_open_device_with_vid_pid: that helper
    // collapses "not plugged in" and "plugged in but we are not allowed to open
    // it" into a null handle, which used to make a permissions problem look
    // exactly like an absent adapter and produce no log line at all.
    libusb_device** list = nullptr;
    const ssize_t count = libusb_get_device_list(Ctx, &list);
    if (count < 0) {
        RetryPending = false;
        return false;
    }

    libusb_device* dev = nullptr;
    for (ssize_t i = 0; i < count; ++i) {
        libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(list[i], &desc) == 0 && desc.idVendor == gGCAdapterVid &&
            desc.idProduct == gGCAdapterPid) {
            dev = list[i];
            break;
        }
    }

    if (dev == nullptr) {
        libusb_free_device_list(list, 1);
        OpenWarned = false; // so a later failure is reported again
        RetryPending = false;
        return false;
    }

    libusb_device_handle* h = nullptr;
    const int openRc = libusb_open(dev, &h);
    libusb_free_device_list(list, 1);
    if (openRc != 0) {
        if (!OpenWarned) {
            OpenWarned = true;
            SPDLOG_WARN("[gcadapter] adapter is connected but could not be opened: {}. To use it natively, {}.",
                        libusb_error_name(openRc), kGCAccessHint);
        }
        RetryPending = true; // device is present; keep retrying
        return false;
    }
    OpenWarned = false;

    // Detach usbhid/hid-generic on Linux; reattached automatically on close.
    libusb_set_auto_detach_kernel_driver(h, 1);
    int rc = libusb_claim_interface(h, 0);
    if (rc != 0) {
        if (!AccessWarned) {
            AccessWarned = true;
            SPDLOG_WARN("[gcadapter] opened the adapter but could not claim interface 0: {}. The OS or another "
                        "process is holding it; {}. Leaving the adapter to the SDL input path.",
                        libusb_error_name(rc), kGCAccessHint);
        }
        libusb_close(h);
        RetryPending = true;
        return false;
    }

    // Prefer the endpoints from the descriptor; fall back to the well-known
    // 0x81 / 0x02 if the descriptor can't be read.
    libusb_config_descriptor* cfg = nullptr;
    if (libusb_get_active_config_descriptor(libusb_get_device(h), &cfg) == 0 && cfg != nullptr) {
        if (cfg->bNumInterfaces > 0 && cfg->interface[0].num_altsetting > 0) {
            const libusb_interface_descriptor& alt = cfg->interface[0].altsetting[0];
            for (int i = 0; i < alt.bNumEndpoints; ++i) {
                const uint8_t addr = alt.endpoint[i].bEndpointAddress;
                if (addr & LIBUSB_ENDPOINT_IN) {
                    EpIn = addr;
                } else {
                    EpOut = addr;
                }
            }
        }
        libusb_free_config_descriptor(cfg);
    }

    uint8_t init = gGCAdapterCmdInit;
    int sent = 0;
    rc = libusb_interrupt_transfer(h, EpOut, &init, 1, &sent, 100);
    if (rc != 0) {
        if (!InitWarned) {
            InitWarned = true;
            SPDLOG_WARN("[gcadapter] failed to send init (0x13): {}; retrying", libusb_error_name(rc));
        }
        libusb_release_interface(h, 0);
        libusb_close(h);
        RetryPending = true;
        return false;
    }

    Handle = h;
    AccessWarned = false;
    InitWarned = false;
    RetryPending = false;
    RumbleDirty = true; // re-assert rumble state on the fresh connection
    Connected = true;
    SPDLOG_INFO("[gcadapter] adapter opened (in=0x{:02x} out=0x{:02x})", EpIn, EpOut);
    return true;
}

void GCAdapter::Impl::CloseDevice() {
    if (Handle != nullptr) {
        const uint8_t off[5] = { gGCAdapterCmdRumble, 0, 0, 0, 0 };
        int sent = 0;
        libusb_interrupt_transfer(Handle, EpOut, const_cast<uint8_t*>(off), sizeof(off), &sent, 50);
        libusb_release_interface(Handle, 0);
        libusb_close(Handle);
        Handle = nullptr;
    }
    Connected = false;
    std::lock_guard<std::mutex> lock(StateLock);
    State = {};
}

void GCAdapter::Impl::SendRumble() {
    const uint8_t mask = RumbleMask.load();
    uint8_t cmd[5] = { gGCAdapterCmdRumble, static_cast<uint8_t>(mask & 1), static_cast<uint8_t>((mask >> 1) & 1),
                       static_cast<uint8_t>((mask >> 2) & 1), static_cast<uint8_t>((mask >> 3) & 1) };
    int sent = 0;
    const int rc = libusb_interrupt_transfer(Handle, EpOut, cmd, sizeof(cmd), &sent, 50);
    if (rc != 0 && rc != LIBUSB_ERROR_TIMEOUT) {
        SPDLOG_DEBUG("[gcadapter] rumble write failed: {}", libusb_error_name(rc));
    }
}

void GCAdapter::Impl::Run() {
    using clock = std::chrono::steady_clock;
    auto lastPollTry = clock::now() - std::chrono::seconds(10);

    while (Running) {
        if (Handle == nullptr) {
            // Hotplug fires AttachHint; without hotplug support, retry once a second.
            const auto sinceTry = clock::now() - lastPollTry;
            const bool retry = AttachHint.exchange(false) ||
                               (!HotplugRegistered && sinceTry >= std::chrono::seconds(1)) ||
                               (RetryPending && sinceTry >= std::chrono::milliseconds(500));
            if (retry) {
                lastPollTry = clock::now();
                TryOpen();
            }
            if (Handle == nullptr) {
                if (HotplugRegistered) {
                    // Field-wise rather than brace-init: clang-format reads
                    // "struct timeval tv{ ... }" as a struct definition and
                    // reflows it across three lines.
                    struct timeval tv;
                    tv.tv_sec = 0;
                    tv.tv_usec = 250000;
                    libusb_handle_events_timeout_completed(Ctx, &tv, nullptr);
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(250));
                }
                continue;
            }
        }

        if (RumbleDirty.exchange(false)) {
            SendRumble();
        }

        uint8_t buf[gGCAdapterPayloadSize] = {};
        int got = 0;
        const int rc = libusb_interrupt_transfer(Handle, EpIn, buf, sizeof(buf), &got, 100);
        if (rc == 0 && got == static_cast<int>(gGCAdapterPayloadSize)) {
            std::array<GCPortState, gGCAdapterPorts> next{};
            if (!DecodePayload(buf, got, next)) {
                continue;
            }
            std::lock_guard<std::mutex> lock(StateLock);
            for (int i = 0; i < gGCAdapterPorts; ++i) {
                if (State[i].Type == GCControllerType::None && next[i].Type != GCControllerType::None) {
                    Origins[i] = CaptureOrigin(next[i]);
                    SPDLOG_INFO("[gcadapter] port {} controller connected (type={}, origin={},{} c={},{})", i,
                                static_cast<int>(next[i].Type), Origins[i].StickX, Origins[i].StickY,
                                Origins[i].CStickX, Origins[i].CStickY);
                } else if (State[i].Type != GCControllerType::None && next[i].Type == GCControllerType::None) {
                    SPDLOG_INFO("[gcadapter] port {} controller disconnected", i);
                }
            }
            State = next;
        } else if (rc == LIBUSB_ERROR_TIMEOUT) {
            continue;
        } else if (rc == LIBUSB_ERROR_NO_DEVICE || rc == LIBUSB_ERROR_IO || rc == LIBUSB_ERROR_PIPE ||
                   rc == LIBUSB_ERROR_OTHER) {
            SPDLOG_WARN("[gcadapter] adapter lost: {}", libusb_error_name(rc));
            CloseDevice();
            RetryPending = true; // reopens if still attached; cleared once TryOpen finds no device
        }
    }
    CloseDevice();
}

GCAdapter::GCAdapter() : mImpl(std::make_unique<Impl>()) {
}

GCAdapter::~GCAdapter() {
    Stop();
}

bool GCAdapter::Start() {
    if (mImpl->Running) {
        return true;
    }
    int rc = libusb_init(&mImpl->Ctx);
    if (rc != 0) {
        SPDLOG_WARN("[gcadapter] libusb_init failed: {}; native GC adapter support disabled", libusb_error_name(rc));
        mImpl->Ctx = nullptr;
        return false;
    }
#if LIBUSB_API_VERSION >= 0x01000106
    libusb_set_option(mImpl->Ctx, LIBUSB_OPTION_LOG_LEVEL, LIBUSB_LOG_LEVEL_WARNING);
#endif
    if (libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG)) {
        rc = libusb_hotplug_register_callback(mImpl->Ctx, LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED, LIBUSB_HOTPLUG_ENUMERATE,
                                              gGCAdapterVid, gGCAdapterPid, LIBUSB_HOTPLUG_MATCH_ANY, &Impl::OnHotplug,
                                              mImpl.get(), &mImpl->HotplugHandle);
        mImpl->HotplugRegistered = rc == LIBUSB_SUCCESS;
    }
    SPDLOG_INFO("[gcadapter] started (hotplug={})", mImpl->HotplugRegistered);

    // One synchronous attempt before the reader thread starts, so callers can
    // tell whether we actually own the adapter while there is still time to act
    // on it -- ControlDeck::PreInitGCAdapter only hides the device from SDL if
    // this succeeded, and it runs before SDL_Init.
    mImpl->TryOpen();

    mImpl->AttachHint = false;
    mImpl->Running = true;
    mImpl->Thread = std::thread([this] { mImpl->Run(); });
    return true;
}

void GCAdapter::Stop() {
    if (!mImpl->Running.exchange(false)) {
        return;
    }
    if (mImpl->Thread.joinable()) {
        mImpl->Thread.join();
    }
    if (mImpl->HotplugRegistered) {
        libusb_hotplug_deregister_callback(mImpl->Ctx, mImpl->HotplugHandle);
        mImpl->HotplugRegistered = false;
    }
    libusb_exit(mImpl->Ctx);
    mImpl->Ctx = nullptr;
    SPDLOG_INFO("[gcadapter] stopped");
}

bool GCAdapter::IsConnected() const {
    return mImpl->Connected;
}

std::vector<GCSnapshot> GCAdapter::GetSnapshotsForGamePort(uint8_t gamePort) const {
    std::vector<GCSnapshot> out;
    if (gamePort >= gGCAdapterPorts) {
        return out;
    }
    std::lock_guard<std::mutex> lock(mImpl->StateLock);
    const uint8_t mask = mImpl->Routing[gamePort];
    for (uint8_t p = 0; p < gGCAdapterPorts; ++p) {
        if ((mask & (1 << p)) && mImpl->State[p].Type != GCControllerType::None) {
            out.push_back({ p, mImpl->State[p], mImpl->Origins[p] });
        }
    }
    return out;
}

void GCAdapter::SetPortRouting(uint8_t gamePort, uint8_t adapterPortMask) {
    if (gamePort >= gGCAdapterPorts) {
        return;
    }
    std::lock_guard<std::mutex> lock(mImpl->StateLock);
    mImpl->Routing[gamePort] = adapterPortMask & 0x0F;
}

uint8_t GCAdapter::GetPortRouting(uint8_t gamePort) const {
    if (gamePort >= gGCAdapterPorts) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(mImpl->StateLock);
    return mImpl->Routing[gamePort];
}

GCControllerType GCAdapter::GetPortType(uint8_t port) const {
    if (port >= gGCAdapterPorts) {
        return GCControllerType::None;
    }
    std::lock_guard<std::mutex> lock(mImpl->StateLock);
    return mImpl->State[port].Type;
}

void GCAdapter::SetRumble(uint8_t port, bool on) {
    if (port >= gGCAdapterPorts) {
        return;
    }
    uint8_t mask = mImpl->RumbleMask.load();
    const uint8_t next = on ? (mask | (1 << port)) : (mask & ~(1 << port));
    if (next != mask) {
        mImpl->RumbleMask = next;
        mImpl->RumbleDirty = true;
    }
}

#else // !LUS_HAS_GCADAPTER

struct GCAdapter::Impl {};

GCAdapter::GCAdapter() : mImpl(std::make_unique<Impl>()) {
}
GCAdapter::~GCAdapter() = default;

bool GCAdapter::Start() {
    SPDLOG_INFO("[gcadapter] built without libusb; native GC adapter support unavailable");
    return false;
}
void GCAdapter::Stop() {
}

// The rest of the interface still has to link: ControlDeck, the mapping
// factories and the input editor call these unconditionally (the sources are
// globbed on every platform), and Start() returning false only stops them from
// running, not from being compiled.
bool GCAdapter::IsConnected() const {
    return false;
}
std::vector<GCSnapshot> GCAdapter::GetSnapshotsForGamePort(uint8_t) const {
    return {};
}
void GCAdapter::SetPortRouting(uint8_t, uint8_t) {
}
uint8_t GCAdapter::GetPortRouting(uint8_t) const {
    return 0;
}
GCControllerType GCAdapter::GetPortType(uint8_t) const {
    return GCControllerType::None;
}
void GCAdapter::SetRumble(uint8_t, bool) {
}
#endif

} // namespace Ship
