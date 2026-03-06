#include "usbhostmanager.h"
#include "storagemanager.h"
#include "peripheralmanager.h"
#include "eventmanager.h"

#include "pico/time.h"
#include "pico/stdlib.h"
#include "pio_usb.h"
#include "tusb.h"

#include "host/usbh.h"
#include "host/usbh_pvt.h"

#include "drivers/shared/xinput_host.h"

void* USBHostManager::_pio_usb_alarm_pool = nullptr;

void USBHostManager::setPioUsbAlarmPool(void* alarm_pool) {
    _pio_usb_alarm_pool = alarm_pool;
}

void USBHostManager::start() {
    // This will happen after Gamepad has initialized
    if (PeripheralManager::getInstance().isUSBEnabled(0) && listeners.size() > 0) {
        // Give hub time to power up before we start host (workaround: some hubs need settling)
        sleep_ms(USB_HOST_HUB_STARTUP_DELAY_MS);
        pio_usb_configuration_t* pio_cfg = PeripheralManager::getInstance().getUSB(0)->getController();
        // Use alarm pool from core 1 so SOF timer keeps running when core 0 blocks in enum delays (hub workaround)
        if (_pio_usb_alarm_pool != nullptr) {
            pio_cfg->alarm_pool = _pio_usb_alarm_pool;
        }
        tuh_configure(1, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, pio_cfg);
        tuh_init(BOARD_TUH_RHPORT);
        sleep_us(10); // ensure we are ready
        // Poll interrupt endpoints (e.g. hub status) every 8ms instead of hub's 255ms (hub workaround)
        set_interval_override(8);
        tuh_ready = true;
    } else {
        tuh_ready = false;
    }
}

// Shut down the USB bus if we are running USB right now
void USBHostManager::shutdown() {
    if ( tuh_ready ) {
        tuh_deinit(BOARD_TUH_RHPORT);
    }
}

void USBHostManager::pushListener(USBListener * usbListener) { // If anything needs to update in the gpconfig driver
    listeners.push_back(usbListener);
}

// Host manager should call tuh_task as fast as possible
void USBHostManager::process() {
    if ( !tuh_ready ) return;
    tryHubRecovery();
    // Drain the USB host event queue so hub status and enumeration complete (workaround for
    // devices behind USB hub). Fixed extra calls when waiting for HID + drain until queue empty.
    const int max_drain = 64; // cap to avoid blocking the main loop
    int n = 0;
    do {
        tuh_task();
        n++;
    } while ( n < max_drain && tuh_task_event_ready() );
}

void USBHostManager::device_mounted() {
    _mounted_device_count++;
}

void USBHostManager::device_unmounted() {
    if ( _mounted_device_count > 0 )
        _mounted_device_count--;
}

const char* USBHostManager::getUsbHostStatus() const {
    if ( !tuh_ready ) return "off";
    if ( _mounted_hid_count > 0 ) return "hid_ok";
    if ( _mounted_device_count > 0 ) return "hub_only";  // hub seen but no HID yet
    return "ready";  // host up, nothing connected
}

void USBHostManager::tryHubRecovery() {
    // Workaround: if something is connected (e.g. hub) but no HID device has appeared for a while,
    // re-init the host to retry enumeration. First attempt after EARLY_MS, then every RECOVERY_TIMEOUT_MS.
    if ( _mounted_device_count == 0 || _mounted_hid_count > 0 ) {
        _hub_recovery_timer_ms = 0;
        return;
    }
    uint32_t now_ms = to_ms_since_boot(get_absolute_time());
    if ( _hub_recovery_timer_ms == 0 )
        _hub_recovery_timer_ms = now_ms;
    uint32_t elapsed = now_ms - _hub_recovery_timer_ms;
    uint32_t threshold = _hub_early_recovery_done ? (uint32_t)USB_HOST_HUB_RECOVERY_TIMEOUT_MS : (uint32_t)USB_HOST_HUB_EARLY_RECOVERY_MS;
    if ( elapsed < threshold )
        return;

    // Re-init USB host to give hub/downstream another chance
    _hub_recovery_timer_ms = 0;
    _mounted_device_count = 0;
    _mounted_hid_count = 0;
    _hub_early_recovery_done = true;

    tuh_deinit(BOARD_TUH_RHPORT);
    pio_usb_configuration_t* pio_cfg = PeripheralManager::getInstance().getUSB(0)->getController();
    tuh_configure(1, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, pio_cfg);
    tuh_init(BOARD_TUH_RHPORT);
    sleep_us(10);
}

void USBHostManager::hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len) {
    _mounted_hid_count++;
    if ( listeners.size() == 0 ) return;
    for( std::vector<USBListener*>::iterator it = listeners.begin(); it != listeners.end(); it++ ){
        (*it)->mount(dev_addr, instance, desc_report, desc_len);
    }
}

void USBHostManager::hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
    if ( _mounted_hid_count > 0 )
        _mounted_hid_count--;
    if ( listeners.size() == 0 ) return;
    for( std::vector<USBListener*>::iterator it = listeners.begin(); it != listeners.end(); it++ ){
        (*it)->unmount(dev_addr);
    }
}

void USBHostManager::hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) {
    if ( listeners.size() == 0 ) return;
    for( std::vector<USBListener*>::iterator it = listeners.begin(); it != listeners.end(); it++ ){
        (*it)->report_received(dev_addr, instance, report, len);
    }
}

void USBHostManager::hid_set_report_complete_cb(uint8_t dev_addr, uint8_t instance, uint8_t report_id, uint8_t report_type, uint16_t len) {
    if ( listeners.size() == 0 ) return;
    for( std::vector<USBListener*>::iterator it = listeners.begin(); it != listeners.end(); it++ ){
        (*it)->set_report_complete(dev_addr, instance, report_id, report_type, len);
    }
}

void USBHostManager::hid_get_report_complete_cb(uint8_t dev_addr, uint8_t instance, uint8_t report_id, uint8_t report_type, uint16_t len) {
    if ( listeners.size() == 0 ) return;
    for( std::vector<USBListener*>::iterator it = listeners.begin(); it != listeners.end(); it++ ){
        (*it)->get_report_complete(dev_addr, instance, report_id, report_type, len);
    }
}

void USBHostManager::xinput_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t controllerType, uint8_t subtype) {
    _mounted_hid_count++;
    if ( listeners.size() == 0 ) return;
    for( std::vector<USBListener*>::iterator it = listeners.begin(); it != listeners.end(); it++ ){
        (*it)->xmount(dev_addr, instance, controllerType, subtype);
    }
}

void USBHostManager::xinput_umount_cb(uint8_t dev_addr) {
    if ( _mounted_hid_count > 0 )
        _mounted_hid_count--;
    if ( listeners.size() == 0 ) return;
    for( std::vector<USBListener*>::iterator it = listeners.begin(); it != listeners.end(); it++ ){
        (*it)->unmount(dev_addr);
    }
}

void USBHostManager::xinput_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) {
    if ( listeners.size() == 0 ) return;
    for( std::vector<USBListener*>::iterator it = listeners.begin(); it != listeners.end(); it++ ){
        (*it)->report_received(dev_addr, instance, report, len);
    }
}

void USBHostManager::xinput_report_sent_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) {
    if ( listeners.size() == 0 ) return;
    for( std::vector<USBListener*>::iterator it = listeners.begin(); it != listeners.end(); it++ ){
        (*it)->report_sent(dev_addr, instance, report, len);
    }
}

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len)
{
    USBHostManager::getInstance().hid_mount_cb(dev_addr, instance, desc_report, desc_len);
    if ( !tuh_hid_receive_report(dev_addr, instance) ) {
        // Error: cannot request report
    }
}

void tuh_mount_cb(uint8_t dev_addr) {
    USBHostManager::getInstance().device_mounted();
    uint16_t vid, pid;
    if (!tuh_vid_pid_get(dev_addr, &vid, &pid)) {
        vid = 0xFFFF;
        pid = 0xFFFF;
    }
    EventManager::getInstance().triggerEvent(new GPUSBHostMountEvent(dev_addr, vid, pid));
}

void tuh_umount_cb(uint8_t dev_addr) {
    USBHostManager::getInstance().device_unmounted();
    uint16_t vid, pid;
    if (!tuh_vid_pid_get(dev_addr, &vid, &pid)) {
        vid = 0xFFFF;
        pid = 0xFFFF;
    }
    EventManager::getInstance().triggerEvent(new GPUSBHostUnmountEvent(dev_addr, vid, pid));
}

/// Invoked when device is unmounted (bus reset/unplugged)
void tuh_hid_umount_cb(uint8_t daddr, uint8_t instance)
{
    USBHostManager::getInstance().hid_umount_cb(daddr, instance);
}

// Invoked when received report from device via interrupt endpoint
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len)
{
    USBHostManager::getInstance().hid_report_received_cb(dev_addr, instance, report, len);

    if ( !tuh_hid_receive_report(dev_addr, instance) ) {
        //Error: cannot request report
    }
}

// On IN/OUT/FEATURE set report callback
void tuh_hid_set_report_complete_cb(uint8_t dev_addr, uint8_t instance, uint8_t report_id, uint8_t report_type, uint16_t len) {
    if ( len != 0 )
        USBHostManager::getInstance().hid_set_report_complete_cb(dev_addr, instance, report_id, report_type, len);
}


// GET REPORT FEATURE
void tuh_hid_get_report_complete_cb(uint8_t dev_addr, uint8_t instance, uint8_t report_id, uint8_t report_type, uint16_t len) {
    if ( len != 0 )
        USBHostManager::getInstance().hid_get_report_complete_cb(dev_addr, instance, report_id, report_type, len);
}

// USB Host: X-Input
// Add X-Input Driver
void tuh_xinput_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t controllerType, uint8_t subtype) {
    USBHostManager::getInstance().xinput_mount_cb(dev_addr, instance, controllerType, subtype);
}

void tuh_xinput_umount_cb(uint8_t dev_addr, uint8_t instance) {
    // send to xinput_unmount_cb in usb host manager
    USBHostManager::getInstance().xinput_umount_cb(dev_addr);
}

void tuh_xinput_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *report, uint16_t len) {
    // report received from xinput device
    USBHostManager::getInstance().xinput_report_received_cb(dev_addr, instance, report, len);
}

void tuh_xinput_report_sent_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *report, uint16_t len) {
    // report sent to xinput device
    USBHostManager::getInstance().xinput_report_sent_cb(dev_addr, instance, report, len);
}

usbh_class_driver_t driver_host[] = {
    {
#if CFG_TUSB_DEBUG >= 2
        .name = "XInput_Host_HID",
#endif
        .init = xinputh_init,
        .open = xinputh_open,
        .set_config = xinputh_set_config,
        .xfer_cb = xinputh_xfer_cb,
        .close = xinputh_close}
};

usbh_class_driver_t const *usbh_app_driver_get_cb(uint8_t *driver_count) {
    *driver_count = 1;
    return driver_host;
}
