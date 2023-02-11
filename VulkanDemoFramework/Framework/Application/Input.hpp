#pragma once

#include "Foundation/Array.hpp"
#include "Foundation/String.hpp"

#include "Application/Keys.hpp"

namespace Framework
{
struct Allocator;

static const uint32_t kMaxGamepads = 4;

enum GamepadAxis
{
  GAMEPAD_AXIS_LEFTX = 0,
  GAMEPAD_AXIS_LEFTY,
  GAMEPAD_AXIS_RIGHTX,
  GAMEPAD_AXIS_RIGHTY,
  GAMEPAD_AXIS_TRIGGERLEFT,
  GAMEPAD_AXIS_TRIGGERRIGHT,
  GAMEPAD_AXIS_COUNT
}; // enum GamepadAxis

enum GamepadButtons
{
  GAMEPAD_BUTTON_A = 0,
  GAMEPAD_BUTTON_B,
  GAMEPAD_BUTTON_X,
  GAMEPAD_BUTTON_Y,
  GAMEPAD_BUTTON_BACK,
  GAMEPAD_BUTTON_GUIDE,
  GAMEPAD_BUTTON_START,
  GAMEPAD_BUTTON_LEFTSTICK,
  GAMEPAD_BUTTON_RIGHTSTICK,
  GAMEPAD_BUTTON_LEFTSHOULDER,
  GAMEPAD_BUTTON_RIGHTSHOULDER,
  GAMEPAD_BUTTON_DPAD_UP,
  GAMEPAD_BUTTON_DPAD_DOWN,
  GAMEPAD_BUTTON_DPAD_LEFT,
  GAMEPAD_BUTTON_DPAD_RIGHT,
  GAMEPAD_BUTTON_COUNT
}; // enum GamepadButtons

enum MouseButtons
{
  MOUSE_BUTTONS_NONE = -1,
  MOUSE_BUTTONS_LEFT = 0,
  MOUSE_BUTTONS_RIGHT,
  MOUSE_BUTTONS_MIDDLE,
  MOUSE_BUTTONS_COUNT

}; // enum MouseButtons

enum Device : uint8_t
{
  DEVICE_KEYBOARD,
  DEVICE_MOUSE,
  DEVICE_GAMEPAD
}; // enum Device

enum DevicePart : uint8_t
{
  DEVICE_PART_KEYBOARD,
  DEVICE_PART_MOUSE,
  DEVICE_PART_GAMEPAD_AXIS,
  DEVICE_PART_GAMEPAD_BUTTONS
}; // enum DevicePart

enum BindingType : uint8_t
{
  BINDING_TYPE_BUTTON,
  BINDING_TYPE_AXIS_1D,
  BINDING_TYPE_AXIS_2D,
  BINDING_TYPE_VECTOR_1D,
  BINDING_TYPE_VECTOR_2D,
  BINDING_TYPE_BUTTON_ONE_MOD,
  BINDING_TYPE_BUTTON_TWO_MOD
}; // enum Type

/// Utility methods
Device deviceFromPart(DevicePart p_Part);

const char** gamepadAxisNames();
const char** gamepadButtonNames();
const char** mouseButtonNames();

struct InputVector2
{
  float x;
  float y;
}; // struct InputVector2

//
//
struct Gamepad
{
  float m_Axis[GAMEPAD_AXIS_COUNT];
  uint8_t m_Buttons[GAMEPAD_BUTTON_COUNT];
  uint8_t m_PreviousButtons[GAMEPAD_BUTTON_COUNT];

  void* m_Handle;
  const char* m_Name;

  uint32_t m_Index;
  int m_Id;

  bool isAttached() const { return m_Id >= 0; }
  bool isButtonDown(GamepadButtons p_Button) { return m_Buttons[p_Button]; }
  bool isButtonJustPressed(GamepadButtons p_Button)
  {
    return (m_Buttons[p_Button] && !m_PreviousButtons[p_Button]);
  }
}; // struct Gamepad

//
//
typedef uint32_t InputHandle;

struct InputBinding
{
  // Enums are uint8_t
  BindingType m_Type;
  Device m_Device;
  DevicePart m_DevicePart;
  uint8_t m_ActionMapIndex;

  uint16_t m_ActionIndex;
  uint16_t m_Button; // Stores the buttons either from GAMEPAD_BUTTONS_*, KEY_*, MOUSE_BUTTON_*.

  float m_Value = 0.0f;

  uint8_t m_IsComposite;
  uint8_t m_IsPartOfComposite;
  uint8_t m_Repeat;

  float m_MinDeadzone = 0.10f;
  float m_MaxDeadzone = 0.95f;

  InputBinding&
  set(BindingType p_Type,
      Device p_Device,
      DevicePart p_DevicePart,
      uint16_t p_Button,
      uint8_t p_IsComposite,
      uint8_t p_IsPartOfComposite,
      uint8_t p_Repeat);
  InputBinding& setDeadzones(float p_Min, float p_Max);
  InputBinding& setHandles(InputHandle p_ActionMap, InputHandle p_Action);

}; // struct InputBinding

struct InputAction
{
  bool triggered() const;
  float readValue1d() const;
  InputVector2 readValue2d() const;

  InputVector2 value;
  InputHandle actionMap;
  const char* name;

}; // struct InputAction

struct InputActionMap
{
  const char* name;
  bool active;
}; // struct InputActionMap

struct InputActionMapCreation
{
  const char* name;
  bool active;
}; // struct InputActionMapCreation

struct InputActionCreation
{

  const char* name;
  InputHandle actionMap;

}; // struct InputActionCreation

//
//
struct InputBindingCreation
{

  InputHandle action;

}; // struct InputBindingCreation

struct InputService : public Framework::Service
{
  FRAMEWORK_DECLARE_SERVICE(InputService);

  void init(Allocator* p_Allocator);
  void shutdown();

  bool isKeyDown(Keys p_Key);
  bool isKeyJustPressed(Keys p_Key, bool p_Repeat = false);
  bool isKeyJustReleased(Keys p_Key);

  bool isMouseDown(MouseButtons p_Button);
  bool isMouseClicked(MouseButtons p_Button);
  bool isMouseReleased(MouseButtons p_Button);
  bool isMouseDragging(MouseButtons p_Button);

  void update(float p_Delta);

  void debugUi();

  void newFrame(); // Called before message handling
  void onEvent(void* p_InputEvent);

  bool isTriggered(InputHandle p_Action) const;
  float isReadValue1d(InputHandle p_Action) const;
  InputVector2 isReadValue2d(InputHandle p_Action) const;

  // Create methods used to create the actual input
  InputHandle createActionMap(const InputActionMapCreation& p_Creation);
  InputHandle createAction(const InputActionCreation& p_Creation);

  // Find methods using name
  InputHandle findActionMap(const char* name) const;
  InputHandle findAction(const char* name) const;

  void
  addButton(InputHandle p_Action, DevicePart p_Device, uint16_t p_Button, bool p_Repeat = false);
  void addAxis1d(
      InputHandle p_Action,
      DevicePart p_Device,
      uint16_t p_Axis,
      float p_MinDeadzone,
      float p_MaxDeadzone);
  void addAxis2d(
      InputHandle p_Action,
      DevicePart p_Device,
      uint16_t p_AxisX,
      uint16_t p_AxisY,
      float p_MinDeadzone,
      float p_MaxDeadzone);
  void addVector1d(
      InputHandle p_Action,
      DevicePart p_DevicePos,
      uint16_t p_ButtonPos,
      DevicePart p_DeviceNeg,
      uint16_t p_ButtonNeg,
      bool p_Repeat = true);
  void addVector2d(
      InputHandle p_Action,
      DevicePart p_DeviceUp,
      uint16_t p_ButtonUp,
      DevicePart p_DeviceDown,
      uint16_t p_ButtonDown,
      DevicePart p_DeviceLeft,
      uint16_t p_ButtonLeft,
      DevicePart p_DeviceRight,
      uint16_t p_ButtonRight,
      bool p_Repeat = true);

  Framework::StringBuffer m_StringBuffer;

  Array<InputActionMap> m_ActionMaps;
  Array<InputAction> m_Actions;
  Array<InputBinding> m_Bindings;

  Gamepad m_Gamepads[kMaxGamepads];

  uint8_t m_Keys[KEY_COUNT];
  uint8_t m_PreviousKeys[KEY_COUNT];

  InputVector2 m_MousePosition;
  InputVector2 m_PreviousMousePosition;
  InputVector2 m_MouseClickedPosition[MOUSE_BUTTONS_COUNT];
  uint8_t m_MouseButton[MOUSE_BUTTONS_COUNT];
  uint8_t m_PreviousMouseButton[MOUSE_BUTTONS_COUNT];
  float m_MouseDragDistance[MOUSE_BUTTONS_COUNT];

  bool m_HasFocus;

  static constexpr const char* ms_Name = "Framework input service";

}; // struct InputService

} // namespace Framework
