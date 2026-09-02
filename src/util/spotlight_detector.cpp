#include "util/spotlight_detector.hpp"

#include <QTimer>

#include <algorithm>
#include <utility>

#ifdef UIL_HAVE_HIDAPI
#include <hidapi/hidapi.h>
#endif

namespace {
constexpr unsigned short kLogitechVendorId = 0x046d;
constexpr unsigned short kSpotlightProductId = 0xc53e;
constexpr int kPresencePollIntervalMs = 2000;
}

SpotlightDetector::SpotlightDetector(QObject* parent)
    : QObject(parent),
      poll_timer_(new QTimer(this)) {
#ifdef UIL_HAVE_HIDAPI
    detection_available_ = hid_init() == 0;
    hidapi_initialized_ = detection_available_;
    if (detection_available_) {
        presence_probe_ = [] {
            struct hid_device_info* devices = hid_enumerate(
                kLogitechVendorId,
                kSpotlightProductId);
            const bool present = devices != nullptr;
            hid_free_enumeration(devices);
            return present;
        };
    }
#endif

    start_polling(kPresencePollIntervalMs);
}

SpotlightDetector::SpotlightDetector(
    PresenceProbe presence_probe,
    int poll_interval_ms,
    QObject* parent)
    : QObject(parent),
      poll_timer_(new QTimer(this)),
      presence_probe_(std::move(presence_probe)),
      detection_available_(bool(presence_probe_)) {
    start_polling(poll_interval_ms);
}

void SpotlightDetector::start_polling(int poll_interval_ms) {
    poll_timer_->setInterval(std::max(1, poll_interval_ms));
    connect(poll_timer_, &QTimer::timeout, this, &SpotlightDetector::refresh);
    refresh();
    poll_timer_->start();
}

SpotlightDetector::~SpotlightDetector() {
#ifdef UIL_HAVE_HIDAPI
    if (hidapi_initialized_) {
        hid_exit();
    }
#endif
}

bool SpotlightDetector::detection_available() const {
    return detection_available_;
}

bool SpotlightDetector::is_present() const {
    return present_;
}

void SpotlightDetector::refresh() {
    const bool present = detection_available_ && presence_probe_
        ? presence_probe_()
        : false;

    if (present == present_) {
        return;
    }

    present_ = present;
    emit presence_changed(present_);
}
