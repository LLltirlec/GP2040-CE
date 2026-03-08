#include "addons/keyboard_host.h"
#include "addons/keyboard_host_listener.h"
#include "storagemanager.h"
#include "drivermanager.h"
#include "usbhostmanager.h"
#include "peripheralmanager.h"
#include "class/hid/hid_host.h"

bool KeyboardHostAddon::available() {
  const KeyboardHostOptions& keyboardHostOptions = Storage::getInstance().getAddonOptions().keyboardHostOptions;
	// Keyboard/mouse can be on either USB host port (0 or 1)
	return keyboardHostOptions.enabled && (PeripheralManager::getInstance().isUSBEnabled(0) || PeripheralManager::getInstance().isUSBEnabled(1));
}

void KeyboardHostAddon::setup() {
  listener = new KeyboardHostListener();
  ((KeyboardHostListener*)listener)->setup();
}

void KeyboardHostAddon::preprocess() {
  ((KeyboardHostListener*)listener)->process();
}
