#ifndef _USBHOSTMANAGER_H_
#define _USBHOSTMANAGER_H_

#include "usblistener.h"
#include <vector>
#include <cstdint>

#include "pio_usb.h"

#include "host/usbh.h"
#include "host/usbh_pvt.h"

// USB Host manager decides on TinyUSB Host driver
usbh_class_driver_t const* usbh_app_driver_get_cb(uint8_t *driver_count);

// Delay before starting USB host so hub has time to power up (workaround for hubs that need settling).
#ifndef USB_HOST_HUB_STARTUP_DELAY_MS
#define USB_HOST_HUB_STARTUP_DELAY_MS     300
#endif
// When a hub is connected but no HID appears for this long, re-init host to recover (workaround for hub IRQ stall).
#ifndef USB_HOST_HUB_RECOVERY_TIMEOUT_MS
#define USB_HOST_HUB_RECOVERY_TIMEOUT_MS  15000
#endif
// First re-init attempt sooner when hub present but no HID (gives enumeration a second chance).
#ifndef USB_HOST_HUB_EARLY_RECOVERY_MS
#define USB_HOST_HUB_EARLY_RECOVERY_MS    4000
#endif

class USBHostManager {
public:
	USBHostManager(USBHostManager const&) = delete;
	void operator=(USBHostManager const&)  = delete;
    static USBHostManager& getInstance() {// Thread-safe storage ensures cross-thread talk
		static USBHostManager instance; // Guaranteed to be destroyed. // Instantiated on first use.
		return instance;
	}
    void start();               // Start USB Host
    void shutdown();            // Called on system reboot
    // Call from core 1 setup so SOF timer runs on core 1 (avoids SOF stall when core 0 blocks in enum delays)
    static void setPioUsbAlarmPool(void* alarm_pool);
    void pushListener(USBListener *); // If anything needs to update in the gpconfig driver
    void process();
    void device_mounted();      // Called from tuh_mount_cb (any device, including hubs)
    void device_unmounted();    // Called from tuh_umount_cb
    void hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len);
    void hid_umount_cb(uint8_t daddr, uint8_t instance);
    void hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len);
    void hid_set_report_complete_cb(uint8_t dev_addr, uint8_t instance, uint8_t report_id, uint8_t report_type, uint16_t len);
    void hid_get_report_complete_cb(uint8_t dev_addr, uint8_t instance, uint8_t report_id, uint8_t report_type, uint16_t len);
    void xinput_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t controllerType, uint8_t subtype);
    void xinput_umount_cb(uint8_t dev_addr);
    void xinput_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len);
    void xinput_report_sent_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len);

    // For debug without UART: returns short status string (e.g. "off", "ready", "hub_only", "hid_ok")
    const char* getUsbHostStatus() const;

private:
    void tryHubRecovery();

    USBHostManager() : tuh_ready(false), core0Ready(false), core1Ready(false),
                       _mounted_device_count(0), _mounted_hid_count(0), _hub_recovery_timer_ms(0), _hub_early_recovery_done(false) {}
    std::vector<USBListener*> listeners;
    usb_device_t *usb_device;
    uint8_t dataPin;
    bool tuh_ready;
    bool core0Ready;
    bool core1Ready;
    uint8_t _mounted_device_count;   // Any USB device (including hubs)
    uint8_t _mounted_hid_count;      // HID + XInput input devices
    uint32_t _hub_recovery_timer_ms; // When we started waiting for HID behind hub (0 = not waiting)
    bool _hub_early_recovery_done;   // true after first early re-init attempt
    static void* _pio_usb_alarm_pool; // alarm pool created on core 1 for SOF timer
};

#endif