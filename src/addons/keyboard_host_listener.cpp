#include "addons/keyboard_host_listener.h"
#include "drivermanager.h"
#include "storagemanager.h"
#include "mouse_report_debug.h"
#include "class/hid/hid_host.h"
#include "host/usbh_pvt.h"
#include <algorithm>

#define DEV_ADDR_NONE 0xFF
#define MAX_KEYBOARD_MOUSE_SLOTS 2
#define MOUSE_SCALE_FACTOR (GAMEPAD_JOYSTICK_MID / 127)
#define MOUSE_STICK_HOLD_MS 32  // Return to center after no mouse move (short = camera stops when you stop; was 16ms, 32ms avoids reset on brief report gaps)
#define GAMEPAD_JOYSTICK_MIN_I32 static_cast<int32_t>(GAMEPAD_JOYSTICK_MIN)
#define GAMEPAD_JOYSTICK_MAX_I32 static_cast<int32_t>(GAMEPAD_JOYSTICK_MAX)

void KeyboardHostListener::setup() {
  const KeyboardHostOptions& keyboardHostOptions = Storage::getInstance().getAddonOptions().keyboardHostOptions;
  const KeyboardMapping& keyboardMapping = keyboardHostOptions.mapping;

  _keyboard_host_mapDpadUp.setMask(GAMEPAD_MASK_UP);
  _keyboard_host_mapDpadDown.setMask(GAMEPAD_MASK_DOWN);
  _keyboard_host_mapDpadLeft.setMask(GAMEPAD_MASK_LEFT);
  _keyboard_host_mapDpadRight.setMask(GAMEPAD_MASK_RIGHT);
  _keyboard_host_mapButtonB1.setMask(GAMEPAD_MASK_B1);
  _keyboard_host_mapButtonB2.setMask(GAMEPAD_MASK_B2);
  _keyboard_host_mapButtonB3.setMask(GAMEPAD_MASK_B3);
  _keyboard_host_mapButtonB4.setMask(GAMEPAD_MASK_B4);
  _keyboard_host_mapButtonL1.setMask(GAMEPAD_MASK_L1);
  _keyboard_host_mapButtonR1.setMask(GAMEPAD_MASK_R1);
  _keyboard_host_mapButtonL2.setMask(GAMEPAD_MASK_L2);
  _keyboard_host_mapButtonR2.setMask(GAMEPAD_MASK_R2);
  _keyboard_host_mapButtonS1.setMask(GAMEPAD_MASK_S1);
  _keyboard_host_mapButtonS2.setMask(GAMEPAD_MASK_S2);
  _keyboard_host_mapButtonL3.setMask(GAMEPAD_MASK_L3);
  _keyboard_host_mapButtonR3.setMask(GAMEPAD_MASK_R3);
  _keyboard_host_mapButtonA1.setMask(GAMEPAD_MASK_A1);
  _keyboard_host_mapButtonA2.setMask(GAMEPAD_MASK_A2);
  _keyboard_host_mapDpadUp.setKey(keyboardMapping.keyDpadUp);
  _keyboard_host_mapDpadDown.setKey(keyboardMapping.keyDpadDown);
  _keyboard_host_mapDpadLeft.setKey(keyboardMapping.keyDpadLeft);
  _keyboard_host_mapDpadRight.setKey(keyboardMapping.keyDpadRight);
  _keyboard_host_mapButtonB1.setKey(keyboardMapping.keyButtonB1);
  _keyboard_host_mapButtonB2.setKey(keyboardMapping.keyButtonB2);
  _keyboard_host_mapButtonR2.setKey(keyboardMapping.keyButtonR2);
  _keyboard_host_mapButtonL2.setKey(keyboardMapping.keyButtonL2);
  _keyboard_host_mapButtonB3.setKey(keyboardMapping.keyButtonB3);
  _keyboard_host_mapButtonB4.setKey(keyboardMapping.keyButtonB4);
  _keyboard_host_mapButtonR1.setKey(keyboardMapping.keyButtonR1);
  _keyboard_host_mapButtonL1.setKey(keyboardMapping.keyButtonL1);
  _keyboard_host_mapButtonS1.setKey(keyboardMapping.keyButtonS1);
  _keyboard_host_mapButtonS2.setKey(keyboardMapping.keyButtonS2);
  _keyboard_host_mapButtonL3.setKey(keyboardMapping.keyButtonL3);
  _keyboard_host_mapButtonR3.setKey(keyboardMapping.keyButtonR3);
  _keyboard_host_mapButtonA1.setKey(keyboardMapping.keyButtonA1);
  _keyboard_host_mapButtonA2.setKey(keyboardMapping.keyButtonA2);
  _keyboard_host_mapButtonA3.setKey(keyboardMapping.keyButtonA3);
  _keyboard_host_mapButtonA4.setKey(keyboardMapping.keyButtonA4);
  _keyboard_host_mapLeftStickUp.setKey(static_cast<uint8_t>(keyboardMapping.keyLeftStickUp));
  _keyboard_host_mapLeftStickDown.setKey(static_cast<uint8_t>(keyboardMapping.keyLeftStickDown));
  _keyboard_host_mapLeftStickLeft.setKey(static_cast<uint8_t>(keyboardMapping.keyLeftStickLeft));
  _keyboard_host_mapLeftStickRight.setKey(static_cast<uint8_t>(keyboardMapping.keyLeftStickRight));
  _keyboard_host_mapRightStickUp.setKey(static_cast<uint8_t>(keyboardMapping.keyRightStickUp));
  _keyboard_host_mapRightStickDown.setKey(static_cast<uint8_t>(keyboardMapping.keyRightStickDown));
  _keyboard_host_mapRightStickLeft.setKey(static_cast<uint8_t>(keyboardMapping.keyRightStickLeft));
  _keyboard_host_mapRightStickRight.setKey(static_cast<uint8_t>(keyboardMapping.keyRightStickRight));

  mouseLeftMapping = keyboardHostOptions.mouseLeft;
  mouseMiddleMapping = keyboardHostOptions.mouseMiddle;
  mouseRightMapping = keyboardHostOptions.mouseRight;
  mouseSensitivity = keyboardHostOptions.mouseSensitivity;
  mouseMovementMode = keyboardHostOptions.movementMode;
  mouseReportLayout = keyboardHostOptions.mouseReportLayout;
  mouseSensitivityScale = mouseSensitivity / 10.0f;
  mouseResetMS = MOUSE_STICK_HOLD_MS;
  mouseResetNextTimer = 0;

  joystickMid = DriverManager::getInstance().getDriver() != nullptr ?
      DriverManager::getInstance().getDriver()->GetJoystickMidValue() : GAMEPAD_JOYSTICK_MID;

  _keyboard_slot_count = 0;
  _mouse_slot_count = 0;
  for (uint8_t i = 0; i < MAX_KEYBOARD_MOUSE_SLOTS; i++) {
    _keyboard_dev_addr[i] = DEV_ADDR_NONE;
    _keyboard_instance[i] = 0;
    _mouse_dev_addr[i] = DEV_ADDR_NONE;
    _mouse_instance[i] = 0;
    _keyboard_host_state[i].dpad = 0;
    _keyboard_host_state[i].buttons = 0;
    _keyboard_host_state[i].lx = joystickMid;
    _keyboard_host_state[i].ly = joystickMid;
    _keyboard_host_state[i].rx = joystickMid;
    _keyboard_host_state[i].ry = joystickMid;
    _keyboard_host_state[i].lt = 0;
    _keyboard_host_state[i].rt = 0;
    _mouse_host_state[i].dpad = 0;
    _mouse_host_state[i].buttons = 0;
    _mouse_host_state[i].lx = joystickMid;
    _mouse_host_state[i].ly = joystickMid;
    _mouse_host_state[i].rx = joystickMid;
    _mouse_host_state[i].ry = joystickMid;
    _mouse_host_state[i].lt = 0;
    _mouse_host_state[i].rt = 0;
  }
  for (uint8_t i = 0; i < MAX_KEYBOARD_MOUSE_SLOTS; i++) {
    _mouse_accumulator_lx[i] = joystickMid;
    _mouse_accumulator_ly[i] = joystickMid;
    _mouse_accumulator_rx[i] = joystickMid;
    _mouse_accumulator_ry[i] = joystickMid;
  }

  mouseX = 0;
  mouseY = 0;
  mouseZ = 0;
  mouseActive = false;
}

void KeyboardHostListener::process() {
  Gamepad *gamepad = Storage::getInstance().GetGamepad();
  if (_keyboard_slot_count > 0 || _mouse_slot_count > 0) {
    // Merge state from all keyboard slots (OR buttons/dpad, axes from first non-mid)
    GamepadState merged_kb;
    merged_kb.dpad = 0;
    merged_kb.buttons = 0;
    merged_kb.lx = joystickMid;
    merged_kb.ly = joystickMid;
    merged_kb.rx = joystickMid;
    merged_kb.ry = joystickMid;
    merged_kb.lt = 0;
    merged_kb.rt = 0;
    for (uint8_t i = 0; i < _keyboard_slot_count; i++) {
      merged_kb.dpad |= _keyboard_host_state[i].dpad;
      merged_kb.buttons |= _keyboard_host_state[i].buttons;
      merged_kb.lt |= _keyboard_host_state[i].lt;
      merged_kb.rt |= _keyboard_host_state[i].rt;
      // Merge each axis independently so keyboard left stick + mouse right stick work together
      if (merged_kb.lx == joystickMid && _keyboard_host_state[i].lx != joystickMid)
        merged_kb.lx = _keyboard_host_state[i].lx;
      if (merged_kb.ly == joystickMid && _keyboard_host_state[i].ly != joystickMid)
        merged_kb.ly = _keyboard_host_state[i].ly;
      if (merged_kb.rx == joystickMid && _keyboard_host_state[i].rx != joystickMid)
        merged_kb.rx = _keyboard_host_state[i].rx;
      if (merged_kb.ry == joystickMid && _keyboard_host_state[i].ry != joystickMid)
        merged_kb.ry = _keyboard_host_state[i].ry;
    }
    for (uint8_t i = 0; i < _mouse_slot_count; i++) {
      merged_kb.dpad |= _mouse_host_state[i].dpad;
      merged_kb.buttons |= _mouse_host_state[i].buttons;
      merged_kb.lt |= _mouse_host_state[i].lt;
      merged_kb.rt |= _mouse_host_state[i].rt;
      // Merge each axis independently so keyboard left stick + mouse right stick work together
      if (merged_kb.lx == joystickMid && _mouse_host_state[i].lx != joystickMid)
        merged_kb.lx = _mouse_host_state[i].lx;
      if (merged_kb.ly == joystickMid && _mouse_host_state[i].ly != joystickMid)
        merged_kb.ly = _mouse_host_state[i].ly;
      if (merged_kb.rx == joystickMid && _mouse_host_state[i].rx != joystickMid)
        merged_kb.rx = _mouse_host_state[i].rx;
      if (merged_kb.ry == joystickMid && _mouse_host_state[i].ry != joystickMid)
        merged_kb.ry = _mouse_host_state[i].ry;
    }
    gamepad->state.dpad     |= merged_kb.dpad;
    gamepad->state.buttons  |= merged_kb.buttons;
    gamepad->state.lx       = merged_kb.lx;
    gamepad->state.ly       = merged_kb.ly;
    gamepad->state.rx       = merged_kb.rx;
    gamepad->state.ry       = merged_kb.ry;
    if (!gamepad->hasAnalogTriggers) {
        gamepad->state.lt       |= merged_kb.lt;
        gamepad->state.rt       |= merged_kb.rt;
    }
  }

  if ( _mouse_slot_count > 0 ) {
    gamepad->auxState.sensors.mouse.active = mouseActive;

    if ( mouseActive == true ) {
        gamepad->auxState.sensors.mouse.active = true;
        gamepad->auxState.sensors.mouse.x = mouseX;
        gamepad->auxState.sensors.mouse.y = mouseY;
        gamepad->auxState.sensors.mouse.z = mouseZ;
        mouseActive = false;
    } else if(mouseResetNextTimer < getMillis()) {
        // Return stick to center only after no mouse movement for MOUSE_STICK_HOLD_MS (PC-like hold)
        for (uint8_t i = 0; i < _mouse_slot_count; i++) {
          _mouse_host_state[i].lx = joystickMid;
          _mouse_host_state[i].ly = joystickMid;
          _mouse_host_state[i].rx = joystickMid;
          _mouse_host_state[i].ry = joystickMid;
          _mouse_accumulator_lx[i] = joystickMid;
          _mouse_accumulator_ly[i] = joystickMid;
          _mouse_accumulator_rx[i] = joystickMid;
          _mouse_accumulator_ry[i] = joystickMid;
        }
    }
  }
}

void KeyboardHostListener::mount(uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len) {
    // Interface protocol (hid_interface_protocol_enum_t)
    uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);

    // tuh_hid_report_received_cb() will be invoked when report is available
    if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD && _keyboard_slot_count < MAX_KEYBOARD_MOUSE_SLOTS) {
        uint8_t slot = _keyboard_slot_count++;
        _keyboard_dev_addr[slot] = dev_addr;
        _keyboard_instance[slot] = instance;
    } else if (itf_protocol == HID_ITF_PROTOCOL_MOUSE && _mouse_slot_count < MAX_KEYBOARD_MOUSE_SLOTS) {
        if (_mouse_slot_count == 0) {
            Gamepad *gamepad = Storage::getInstance().GetGamepad();
            gamepad->auxState.sensors.mouse.enabled = true;
        }
        uint8_t slot = _mouse_slot_count++;
        _mouse_dev_addr[slot] = dev_addr;
        _mouse_instance[slot] = instance;
    }
}

void KeyboardHostListener::unmount(uint8_t dev_addr) {
    for (uint8_t i = 0; i < _keyboard_slot_count; i++) {
        if (_keyboard_dev_addr[i] == dev_addr) {
            _keyboard_dev_addr[i] = DEV_ADDR_NONE;
            _keyboard_instance[i] = 0;
            if (i < _keyboard_slot_count - 1) {
                _keyboard_dev_addr[i] = _keyboard_dev_addr[_keyboard_slot_count - 1];
                _keyboard_instance[i] = _keyboard_instance[_keyboard_slot_count - 1];
                _keyboard_host_state[i] = _keyboard_host_state[_keyboard_slot_count - 1];
                _keyboard_dev_addr[_keyboard_slot_count - 1] = DEV_ADDR_NONE;
            }
            _keyboard_slot_count--;
            return;
        }
    }
    for (uint8_t i = 0; i < _mouse_slot_count; i++) {
        if (_mouse_dev_addr[i] == dev_addr) {
            _mouse_dev_addr[i] = DEV_ADDR_NONE;
            _mouse_instance[i] = 0;
            if (i < _mouse_slot_count - 1) {
                _mouse_dev_addr[i] = _mouse_dev_addr[_mouse_slot_count - 1];
                _mouse_instance[i] = _mouse_instance[_mouse_slot_count - 1];
                _mouse_host_state[i] = _mouse_host_state[_mouse_slot_count - 1];
                _mouse_dev_addr[_mouse_slot_count - 1] = DEV_ADDR_NONE;
            }
            _mouse_slot_count--;
            if (_mouse_slot_count == 0) {
                Gamepad *gamepad = Storage::getInstance().GetGamepad();
                gamepad->auxState.sensors.mouse.enabled = false;
            }
            return;
        }
    }
}

void KeyboardHostListener::report_received(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len){
  if ( _keyboard_slot_count == 0 && _mouse_slot_count == 0 )
    return;

  for (uint8_t i = 0; i < _keyboard_slot_count; i++) {
    if (_keyboard_dev_addr[i] == dev_addr && _keyboard_instance[i] == instance) {
      process_kbd_report(i, (hid_keyboard_report_t const*) report);
      return;
    }
  }
  for (uint8_t i = 0; i < _mouse_slot_count; i++) {
    if (_mouse_dev_addr[i] == dev_addr && _mouse_instance[i] == instance) {
      process_mouse_report(i, report, len);
      return;
    }
  }
}

uint8_t KeyboardHostListener::getKeycodeFromModifier(uint8_t modifier) {
	switch (modifier) {
	  case KEYBOARD_MODIFIER_LEFTCTRL   : return HID_KEY_CONTROL_LEFT ;
	  case KEYBOARD_MODIFIER_LEFTSHIFT  : return HID_KEY_SHIFT_LEFT   ;
	  case KEYBOARD_MODIFIER_LEFTALT    : return HID_KEY_ALT_LEFT     ;
	  case KEYBOARD_MODIFIER_LEFTGUI    : return HID_KEY_GUI_LEFT     ;
	  case KEYBOARD_MODIFIER_RIGHTCTRL  : return HID_KEY_CONTROL_RIGHT;
	  case KEYBOARD_MODIFIER_RIGHTSHIFT : return HID_KEY_SHIFT_RIGHT  ;
	  case KEYBOARD_MODIFIER_RIGHTALT   : return HID_KEY_ALT_RIGHT    ;
	  case KEYBOARD_MODIFIER_RIGHTGUI   : return HID_KEY_GUI_RIGHT    ;
	}

	return 0;
}

void KeyboardHostListener::preprocess_report()
{
  for (uint8_t s = 0; s < MAX_KEYBOARD_MOUSE_SLOTS; s++) {
    _keyboard_host_state[s].dpad = 0;
    _keyboard_host_state[s].buttons = 0;
    _keyboard_host_state[s].lx = joystickMid;
    _keyboard_host_state[s].ly = joystickMid;
    _keyboard_host_state[s].rx = joystickMid;
    _keyboard_host_state[s].ry = joystickMid;
    _keyboard_host_state[s].lt = 0;
    _keyboard_host_state[s].rt = 0;
    _mouse_host_state[s].dpad = 0;
    _mouse_host_state[s].buttons = 0;
    _mouse_host_state[s].lx = joystickMid;
    _mouse_host_state[s].ly = joystickMid;
    _mouse_host_state[s].rx = joystickMid;
    _mouse_host_state[s].ry = joystickMid;
    _mouse_host_state[s].lt = 0;
    _mouse_host_state[s].rt = 0;
  }
}

// convert hid keycode to ascii and print via usb device CDC (ignore non-printable)
void KeyboardHostListener::process_kbd_report(uint8_t slot, hid_keyboard_report_t const *report)
{
  _keyboard_host_state[slot].dpad = 0;
  _keyboard_host_state[slot].buttons = 0;
  _keyboard_host_state[slot].lx = joystickMid;
  _keyboard_host_state[slot].ly = joystickMid;
  _keyboard_host_state[slot].rx = joystickMid;
  _keyboard_host_state[slot].ry = joystickMid;
  _keyboard_host_state[slot].lt = 0;
  _keyboard_host_state[slot].rt = 0;

  // make this 13 instead of 7 to include modifier bitfields from hid_keyboard_modifier_bm_t
  for(uint8_t i=0; i<13; i++)
  {
    uint8_t keycode = 0;
    if (i < 6) {
        // process keycodes normally
        keycode = report->keycode[i];
    } else {
        // keycode modifiers are bitfields, so the old getKeycodeFromModifier switch approach doesn't work
        // keycode = getKeycodeFromModifier(report->modifier);
        // new approach masks the modifier bit to determine which keys are pressed
        keycode = getKeycodeFromModifier(report->modifier & (1 << (i - 6)));
    }
    if ( keycode )
    {
      _keyboard_host_state[slot].dpad |=
            ((keycode == _keyboard_host_mapDpadUp.key)    ? _keyboard_host_mapDpadUp.buttonMask : _keyboard_host_state[slot].dpad)
          | ((keycode == _keyboard_host_mapDpadDown.key)  ? _keyboard_host_mapDpadDown.buttonMask : _keyboard_host_state[slot].dpad)
          | ((keycode == _keyboard_host_mapDpadLeft.key)  ? _keyboard_host_mapDpadLeft.buttonMask  : _keyboard_host_state[slot].dpad)
          | ((keycode == _keyboard_host_mapDpadRight.key) ? _keyboard_host_mapDpadRight.buttonMask : _keyboard_host_state[slot].dpad)
        ;

        _keyboard_host_state[slot].buttons |=
            ((keycode == _keyboard_host_mapButtonB1.key)  ? _keyboard_host_mapButtonB1.buttonMask  : _keyboard_host_state[slot].buttons)
          | ((keycode == _keyboard_host_mapButtonB2.key)  ? _keyboard_host_mapButtonB2.buttonMask  : _keyboard_host_state[slot].buttons)
          | ((keycode == _keyboard_host_mapButtonB3.key)  ? _keyboard_host_mapButtonB3.buttonMask  : _keyboard_host_state[slot].buttons)
          | ((keycode == _keyboard_host_mapButtonB4.key)  ? _keyboard_host_mapButtonB4.buttonMask  : _keyboard_host_state[slot].buttons)
          | ((keycode == _keyboard_host_mapButtonL1.key)  ? _keyboard_host_mapButtonL1.buttonMask  : _keyboard_host_state[slot].buttons)
          | ((keycode == _keyboard_host_mapButtonR1.key)  ? _keyboard_host_mapButtonR1.buttonMask  : _keyboard_host_state[slot].buttons)
          | ((keycode == _keyboard_host_mapButtonL2.key)  ? _keyboard_host_mapButtonL2.buttonMask  : _keyboard_host_state[slot].buttons)
          | ((keycode == _keyboard_host_mapButtonR2.key)  ? _keyboard_host_mapButtonR2.buttonMask  : _keyboard_host_state[slot].buttons)
          | ((keycode == _keyboard_host_mapButtonS1.key)  ? _keyboard_host_mapButtonS1.buttonMask  : _keyboard_host_state[slot].buttons)
          | ((keycode == _keyboard_host_mapButtonS2.key)  ? _keyboard_host_mapButtonS2.buttonMask  : _keyboard_host_state[slot].buttons)
          | ((keycode == _keyboard_host_mapButtonL3.key)  ? _keyboard_host_mapButtonL3.buttonMask  : _keyboard_host_state[slot].buttons)
          | ((keycode == _keyboard_host_mapButtonR3.key)  ? _keyboard_host_mapButtonR3.buttonMask  : _keyboard_host_state[slot].buttons)
          | ((keycode == _keyboard_host_mapButtonA1.key)  ? _keyboard_host_mapButtonA1.buttonMask  : _keyboard_host_state[slot].buttons)
          | ((keycode == _keyboard_host_mapButtonA2.key)  ? _keyboard_host_mapButtonA2.buttonMask  : _keyboard_host_state[slot].buttons)
          | ((keycode == _keyboard_host_mapButtonA3.key)  ? _keyboard_host_mapButtonA3.buttonMask  : _keyboard_host_state[slot].buttons)
          | ((keycode == _keyboard_host_mapButtonA4.key)  ? _keyboard_host_mapButtonA4.buttonMask  : _keyboard_host_state[slot].buttons)
        ;

      // Left/right stick from keyboard (e.g. WASD for left stick)
      if (_keyboard_host_mapLeftStickUp.isAssigned() && keycode == _keyboard_host_mapLeftStickUp.key)
        _keyboard_host_state[slot].ly = GAMEPAD_JOYSTICK_MIN;
      else if (_keyboard_host_mapLeftStickDown.isAssigned() && keycode == _keyboard_host_mapLeftStickDown.key)
        _keyboard_host_state[slot].ly = GAMEPAD_JOYSTICK_MAX;
      if (_keyboard_host_mapLeftStickLeft.isAssigned() && keycode == _keyboard_host_mapLeftStickLeft.key)
        _keyboard_host_state[slot].lx = GAMEPAD_JOYSTICK_MIN;
      else if (_keyboard_host_mapLeftStickRight.isAssigned() && keycode == _keyboard_host_mapLeftStickRight.key)
        _keyboard_host_state[slot].lx = GAMEPAD_JOYSTICK_MAX;
      if (_keyboard_host_mapRightStickUp.isAssigned() && keycode == _keyboard_host_mapRightStickUp.key)
        _keyboard_host_state[slot].ry = GAMEPAD_JOYSTICK_MIN;
      else if (_keyboard_host_mapRightStickDown.isAssigned() && keycode == _keyboard_host_mapRightStickDown.key)
        _keyboard_host_state[slot].ry = GAMEPAD_JOYSTICK_MAX;
      if (_keyboard_host_mapRightStickLeft.isAssigned() && keycode == _keyboard_host_mapRightStickLeft.key)
        _keyboard_host_state[slot].rx = GAMEPAD_JOYSTICK_MIN;
      else if (_keyboard_host_mapRightStickRight.isAssigned() && keycode == _keyboard_host_mapRightStickRight.key)
        _keyboard_host_state[slot].rx = GAMEPAD_JOYSTICK_MAX;
    }
  }
}

uint16_t KeyboardHostListener::scaleMouseToJoystick(int8_t mouseVal) {
  int32_t result = joystickMid + (int32_t)mouseVal * mouseSensitivityScale * MOUSE_SCALE_FACTOR;
  return std::clamp(result, GAMEPAD_JOYSTICK_MIN_I32, GAMEPAD_JOYSTICK_MAX_I32);
}

int32_t KeyboardHostListener::scaleMouseDeltaToJoystick(int8_t mouseVal) {
  return static_cast<int32_t>(mouseVal) * mouseSensitivityScale * MOUSE_SCALE_FACTOR;
}

void KeyboardHostListener::process_mouse_report(uint8_t slot, uint8_t const * report, uint16_t len)
{
  // Debug: expose raw report for axis-order detection (GET /api/getMouseReportDebug)
  mouse_report_debug_store(report, len);

  // HID report may include report_id as first byte (composite device). Boot mouse = 4 bytes (no ID).
  // Xiaomi silent mouse (Linux mi_silent_mouse_rdesc_fixed): Report ID 3, then buttons, X, Y, wheel
  // in the descriptor; some units send bytes as buttons,X,wheel,Y — use Y_AFTER_WHEEL in that case.
  if (len < 3) return;
  uint8_t const * data = (len >= 5) ? (report + 1) : report;
  uint8_t data_len = (len >= 5) ? (len - 1) : len;
  uint8_t buttons = data[0];
  int8_t x;
  int8_t y;
  int8_t wheel;
  uint8_t layout = Storage::getInstance().getAddonOptions().keyboardHostOptions.mouseReportLayout;
  if (layout > MOUSE_LAYOUT_WHEEL_BEFORE_AXES)
    layout = MOUSE_LAYOUT_STANDARD;
  // Optional: for Xiaomi silent mouse (Report ID 3, 5 bytes) hardware often sends buttons,X,wheel,Y;
  // if so, set "Y after wheel" in config. Descriptor (mi_silent_mouse_rdesc_fixed) lists X,Y,wheel order.
  if (data_len >= 4) {
    if (layout == MOUSE_LAYOUT_WHEEL_BEFORE_AXES) {
      wheel = (int8_t)data[1];
      x = (int8_t)data[2];
      y = (int8_t)data[3];
    } else if (layout == MOUSE_LAYOUT_Y_AFTER_WHEEL) {
      x = (int8_t)data[1];
      wheel = (int8_t)data[2];
      y = (int8_t)data[3];
    } else {
      x = (int8_t)data[1];
      y = (int8_t)data[2];
      wheel = (int8_t)data[3];
    }
  } else {
    x = (int8_t)data[1];
    y = (int8_t)data[2];
    wheel = 0;
  }

  _mouse_host_state[slot].buttons = 0;
  _mouse_host_state[slot].buttons |=
      (buttons & MOUSE_BUTTON_LEFT   ?   mouseLeftMapping : 0)
    | (buttons & MOUSE_BUTTON_MIDDLE ? mouseMiddleMapping : 0)
    | (buttons & MOUSE_BUTTON_RIGHT  ?  mouseRightMapping : 0);

  mouseX = x;
  mouseY = y;
  mouseZ = wheel;
  mouseActive = true;

  if (mouseMovementMode == MOUSE_MOVEMENT_NONE) {
    return;
  }

  mouseResetNextTimer = getMillis() + mouseResetMS;

  // Accumulate in int32 for smoother response (no lost sub-step precision)
  int32_t dx = scaleMouseDeltaToJoystick(x);
  int32_t dy = scaleMouseDeltaToJoystick(y);
  if (mouseMovementMode == MOUSE_MOVEMENT_LEFT_ANALOG) {
    _mouse_accumulator_lx[slot] = std::clamp(_mouse_accumulator_lx[slot] + dx, GAMEPAD_JOYSTICK_MIN_I32, GAMEPAD_JOYSTICK_MAX_I32);
    _mouse_accumulator_ly[slot] = std::clamp(_mouse_accumulator_ly[slot] + dy, GAMEPAD_JOYSTICK_MIN_I32, GAMEPAD_JOYSTICK_MAX_I32);
    _mouse_host_state[slot].lx = static_cast<uint16_t>(_mouse_accumulator_lx[slot]);
    _mouse_host_state[slot].ly = static_cast<uint16_t>(_mouse_accumulator_ly[slot]);
  } else if (mouseMovementMode == MOUSE_MOVEMENT_RIGHT_ANALOG) {
    _mouse_accumulator_rx[slot] = std::clamp(_mouse_accumulator_rx[slot] + dx, GAMEPAD_JOYSTICK_MIN_I32, GAMEPAD_JOYSTICK_MAX_I32);
    _mouse_accumulator_ry[slot] = std::clamp(_mouse_accumulator_ry[slot] + dy, GAMEPAD_JOYSTICK_MIN_I32, GAMEPAD_JOYSTICK_MAX_I32);
    _mouse_host_state[slot].rx = static_cast<uint16_t>(_mouse_accumulator_rx[slot]);
    _mouse_host_state[slot].ry = static_cast<uint16_t>(_mouse_accumulator_ry[slot]);
  }
}
