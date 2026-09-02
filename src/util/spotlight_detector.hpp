#pragma once

#include <QObject>

class QTimer;

/** @brief Polls for the Logitech Spotlight USB receiver without opening it. */
class SpotlightDetector final : public QObject {
    Q_OBJECT

public:
    /** @brief Constructs and starts the receiver-presence detector. */
    explicit SpotlightDetector(QObject* parent = nullptr);
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
    /** @brief Enumerates the known Spotlight USB vendor and product ID. */
    void refresh();

    QTimer* poll_timer_ = nullptr;
    bool detection_available_ = false;
    bool present_ = false;
};
