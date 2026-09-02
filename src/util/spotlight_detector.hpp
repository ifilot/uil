#pragma once

#include <QObject>

#include <functional>

class QTimer;

/** @brief Polls for the Logitech Spotlight USB receiver without opening it. */
class SpotlightDetector final : public QObject {
    Q_OBJECT

public:
    using PresenceProbe = std::function<bool()>;

    /** @brief Constructs and starts the receiver-presence detector. */
    explicit SpotlightDetector(QObject* parent = nullptr);
    /** @brief Constructs a detector around an injectable presence probe. */
    SpotlightDetector(
        PresenceProbe presence_probe,
        int poll_interval_ms,
        QObject* parent = nullptr);
    /** @brief Releases the HID enumeration library. */
    ~SpotlightDetector() override;

    /** @brief Returns whether receiver enumeration is supported by this build. */
    bool detection_available() const;
    /** @brief Returns whether the Spotlight receiver is currently present. */
    bool is_present() const;

signals:
    /** @brief Emitted when receiver presence changes. */
    void presence_changed(bool present);

private:
    /** @brief Configures polling and performs the initial enumeration. */
    void start_polling(int poll_interval_ms);
    /** @brief Enumerates the known Spotlight USB vendor and product ID. */
    void refresh();

    QTimer* poll_timer_ = nullptr;
    PresenceProbe presence_probe_;
    bool detection_available_ = false;
    bool hidapi_initialized_ = false;
    bool present_ = false;
};
