#include "util/spotlight_detector.hpp"

#include <QTimer>

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
#endif

    poll_timer_->setInterval(kPresencePollIntervalMs);
    connect(poll_timer_, &QTimer::timeout, this, &SpotlightDetector::refresh);
    refresh();
    poll_timer_->start();
}

SpotlightDetector::~SpotlightDetector() {
#ifdef UIL_HAVE_HIDAPI
    if (detection_available_) {
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
    bool present = false;
#ifdef UIL_HAVE_HIDAPI
    if (detection_available_) {
        struct hid_device_info* devices = hid_enumerate(kLogitechVendorId, kSpotlightProductId);
        present = devices != nullptr;
        hid_free_enumeration(devices);
    }
#endif

    if (present == present_) {
        return;
    }

    present_ = present;
    emit presence_changed(present_);
}
