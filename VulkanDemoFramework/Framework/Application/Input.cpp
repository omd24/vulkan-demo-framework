#include "Input.hpp"

#include "Externals/imgui/imgui.h"

#include <assert.h>
#include <math.h>

// TODO: Support for win32 backend
#define INPUT_BACKEND_SDL
#include <SDL.h>

namespace Framework
{
struct InputBackend
{
  void init(Gamepad* p_Gamepads, uint32_t p_NumGamepads);
  void shutdown();

  void getMouseState(InputVector2& p_Position, uint8_t* p_Buttons, uint32_t p_NumButtons);

  void onEvent(
      void* p_Event,
      uint8_t* p_Keys,
      uint32_t p_NumKeys,
      Gamepad* p_Gamepads,
      uint32_t p_NumGamepads,
      bool& p_HasFocus);

}; // struct InputBackendSDL

static bool initGamepad(int32_t p_Index, Gamepad& p_Gamepad)
{
  SDL_GameController* pad = SDL_GameControllerOpen(p_Index);

  // Set memory to 0
  memset(&p_Gamepad, 0, sizeof(Gamepad));

  if (pad)
  {
    OutputDebugStringA("Opened Joystick 0\n");
    {
      char msg[256];
      sprintf(msg, "Name: %s\n", SDL_GameControllerNameForIndex(p_Index));
      OutputDebugStringA(msg);
    }

    SDL_Joystick* joy = SDL_GameControllerGetJoystick(pad);

    p_Gamepad.m_Index = p_Index;
    p_Gamepad.m_Name = SDL_JoystickNameForIndex(p_Index);
    p_Gamepad.m_Handle = pad;
    p_Gamepad.m_Id = SDL_JoystickInstanceID(joy);

    return true;
  }
  else
  {
    {
      char msg[256];
      sprintf(msg, "Couldn't open Joystick %u\n", p_Index);
      OutputDebugStringA(msg);
    }

    p_Gamepad.m_Index = UINT32_MAX;
    return false;
  }
}

static void terminateGamepad(Gamepad& p_Gamepad)
{

  SDL_JoystickClose((SDL_Joystick*)p_Gamepad.m_Handle);
  p_Gamepad.m_Index = UINT32_MAX;
  p_Gamepad.m_Name = 0;
  p_Gamepad.m_Handle = 0;
  p_Gamepad.m_Id = UINT32_MAX;
}

// InputBackendSDL //
void InputBackend::init(Gamepad* p_Gamepads, uint32_t p_NumGamepads)
{

  if (SDL_WasInit(SDL_INIT_GAMECONTROLLER) != 1)
    SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);

  SDL_GameControllerEventState(SDL_ENABLE);

  for (uint32_t i = 0; i < p_NumGamepads; i++)
  {
    p_Gamepads[i].m_Index = UINT32_MAX;
    p_Gamepads[i].m_Id = UINT32_MAX;
  }

  const int numJoystics = SDL_NumJoysticks();
  if (numJoystics > 0)
  {
    OutputDebugStringA("Detected joysticks!");

    for (int i = 0; i < numJoystics; i++)
    {
      if (SDL_IsGameController(i))
      {
        initGamepad(i, p_Gamepads[i]);
      }
    }
  }
}

void InputBackend::shutdown() { SDL_GameControllerEventState(SDL_DISABLE); }

static uint32_t toSdlMouseButton(MouseButtons button)
{
  switch (button)
  {
  case MOUSE_BUTTONS_LEFT:
    return SDL_BUTTON_LEFT;
  case MOUSE_BUTTONS_MIDDLE:
    return SDL_BUTTON_MIDDLE;
  case MOUSE_BUTTONS_RIGHT:
    return SDL_BUTTON_RIGHT;
  }

  return UINT32_MAX;
}

void InputBackend::getMouseState(
    InputVector2& p_Position, uint8_t* p_Buttons, uint32_t p_NumButtons)
{
  int mouseX, mouseY;
  uint32_t mouseButtons = SDL_GetMouseState(&mouseX, &mouseY);

  for (uint32_t i = 0; i < p_NumButtons; ++i)
  {
    uint32_t sdlButton = toSdlMouseButton((MouseButtons)i);
    p_Buttons[i] = mouseButtons & SDL_BUTTON(sdlButton);
  }

  p_Position.x = (float)mouseX;
  p_Position.y = (float)mouseY;
}

void InputBackend::onEvent(
    void* p_Event,
    uint8_t* p_Keys,
    uint32_t p_NumKeys,
    Gamepad* p_Gamepads,
    uint32_t p_NumGamepads,
    bool& p_HasFocus)
{
  SDL_Event& event = *(SDL_Event*)p_Event;
  switch (event.type)
  {

  case SDL_KEYDOWN:
  case SDL_KEYUP: {
    int key = event.key.keysym.scancode;
    if (key >= 0 && key < (int)p_NumKeys)
      p_Keys[key] = (event.type == SDL_KEYDOWN);

    break;
  }

  case SDL_CONTROLLERDEVICEADDED: {
    OutputDebugStringA("Gamepad Added\n");
    int32_t index = event.cdevice.which;

    initGamepad(index, p_Gamepads[index]);

    break;
  }

  case SDL_CONTROLLERDEVICEREMOVED: {
    OutputDebugStringA("Gamepad Removed\n");
    int32_t instanceId = event.jdevice.which;
    // Search for the correct gamepad
    for (size_t i = 0; i < kMaxGamepads; i++)
    {
      if (p_Gamepads[i].m_Id == instanceId)
      {
        terminateGamepad(p_Gamepads[i]);
        break;
      }
    }
    break;
  }

  case SDL_CONTROLLERAXISMOTION: {
#if defined(INPUT_DEBUG_OUTPUT)
    {
      char msg[256];
      sprintf(msg, "Axis %u - %f\n", event.jaxis.axis, event.jaxis.value / 32768.0f);
      OutputDebugStringA(msg);
    }
#endif // INPUT_DEBUG_OUTPUT

    for (size_t i = 0; i < kMaxGamepads; i++)
    {
      if (p_Gamepads[i].m_Id == event.caxis.which)
      {
        p_Gamepads[i].m_Axis[event.caxis.axis] = event.caxis.value / 32768.0f;
        break;
      }
    }
    break;
  }

  case SDL_CONTROLLERBUTTONDOWN:
  case SDL_CONTROLLERBUTTONUP: {
#if defined(INPUT_DEBUG_OUTPUT)
    OutputDebugStringA("Button\n");
#endif // INPUT_DEBUG_OUTPUT

    for (size_t i = 0; i < kMaxGamepads; i++)
    {
      if (p_Gamepads[i].m_Id == event.cbutton.which)
      {
        p_Gamepads[i].m_Buttons[event.cbutton.button] = event.cbutton.state == SDL_PRESSED ? 1 : 0;
        break;
      }
    }
    break;
  }

  case SDL_WINDOWEVENT: {
    switch (event.window.event)
    {
    case SDL_WINDOWEVENT_FOCUS_GAINED: {
      p_HasFocus = true;
      break;
    }

    case SDL_WINDOWEVENT_FOCUS_LOST: {
      p_HasFocus = false;
      break;
    }
    }
    break;
  }
  }
}

Device deviceFromPart(DevicePart part)
{
  switch (part)
  {
  case DEVICE_PART_MOUSE: {
    return DEVICE_MOUSE;
  }

  case DEVICE_PART_GAMEPAD_AXIS:
  case DEVICE_PART_GAMEPAD_BUTTONS:
    // case InputBinding::GAMEPAD_HAT:
    {
      return DEVICE_GAMEPAD;
    }

  case DEVICE_PART_KEYBOARD:
  default: {
    return DEVICE_KEYBOARD;
  }
  }
}

const char** gamepadAxisNames()
{
  static const char* names[] = {
      "left_x",
      "left_y",
      "right_x",
      "right_y",
      "trigger_left",
      "trigger_right",
      "gamepad_axis_count"};
  return names;
}

const char** gamepadButtonNames()
{
  static const char* names[] = {
      "a",
      "b",
      "x",
      "y",
      "back",
      "guide",
      "start",
      "left_stick",
      "right_stick",
      "left_shoulder",
      "right_shoulder",
      "dpad_up",
      "dpad_down",
      "dpad_left",
      "dpad_right",
      "gamepad_button_count",
  };
  return names;
}

const char** mouseButtonNames()
{
  static const char* names[] = {
      "left",
      "right",
      "middle",
      "mouse_button_count",
  };
  return names;
}

/// InputService
static InputBackend g_InputBackend;
static InputService g_InputService;

InputService* InputService::instance() { return &g_InputService; }

void InputService::init(Allocator* p_Allocator)
{
  OutputDebugStringA("InputService init\n");

  m_StringBuffer.init(1000, p_Allocator);
  m_ActionMaps.init(p_Allocator, 16);
  m_Actions.init(p_Allocator, 64);
  m_Bindings.init(p_Allocator, 256);

  // Init gamepads handles
  for (size_t i = 0; i < kMaxGamepads; i++)
  {
    m_Gamepads[i].m_Handle = nullptr;
  }
  memset(m_Keys, 0, KEY_COUNT);
  memset(m_PreviousKeys, 0, KEY_COUNT);
  memset(m_MouseButton, 0, MOUSE_BUTTONS_COUNT);
  memset(m_PreviousMouseButton, 0, MOUSE_BUTTONS_COUNT);

  g_InputBackend.init(m_Gamepads, kMaxGamepads);
}

void InputService::shutdown()
{
  g_InputBackend.shutdown();
  m_ActionMaps.shutdown();
  m_Actions.shutdown();
  m_Bindings.shutdown();

  m_StringBuffer.shutdown();

  OutputDebugStringA("InputService shutdown\n");
}

static constexpr float kMouseDragMinDistance = 4.f;

bool InputService::isKeyDown(Keys key) { return m_Keys[key] && m_HasFocus; }

bool InputService::isKeyJustPressed(Keys key, bool repeat)
{
  return m_Keys[key] && !m_PreviousKeys[key] && m_HasFocus;
}

bool InputService::isKeyJustReleased(Keys key)
{
  return !m_Keys[key] && m_PreviousKeys[key] && m_HasFocus;
}

bool InputService::isMouseDown(MouseButtons button) { return m_MouseButton[button]; }

bool InputService::isMouseClicked(MouseButtons button)
{
  return m_MouseButton[button] && !m_PreviousMouseButton[button];
}

bool InputService::isMouseReleased(MouseButtons button) { return !m_MouseButton[button]; }

bool InputService::isMouseDragging(MouseButtons button)
{
  if (!m_MouseButton[button])
    return false;

  return m_MouseDragDistance[button] > kMouseDragMinDistance;
}

void InputService::onEvent(void* p_Event)
{
  g_InputBackend.onEvent(p_Event, m_Keys, KEY_COUNT, m_Gamepads, kMaxGamepads, m_HasFocus);
}

bool InputService::isTriggered(InputHandle action) const
{
  assert(action < m_Actions.m_Size);
  return m_Actions[action].triggered();
}

float InputService::isReadValue1d(InputHandle action) const
{
  assert(action < m_Actions.m_Size);
  return m_Actions[action].readValue1d();
}

InputVector2 InputService::isReadValue2d(InputHandle action) const
{
  assert(action < m_Actions.m_Size);
  return m_Actions[action].readValue2d();
}

InputHandle InputService::createActionMap(const InputActionMapCreation& creation)
{
  InputActionMap newActionMap;
  newActionMap.active = creation.active;
  newActionMap.name = creation.name;

  m_ActionMaps.push(newActionMap);

  return m_ActionMaps.m_Size - 1;
}

InputHandle InputService::createAction(const InputActionCreation& creation)
{
  InputAction action;
  action.actionMap = creation.actionMap;
  action.name = creation.name;

  m_Actions.push(action);

  return m_Actions.m_Size - 1;
}

InputHandle InputService::findActionMap(const char* name) const
{
  // TODO: move to hash map ?
  for (uint32_t i = 0; i < m_ActionMaps.m_Size; i++)
  {
    if (strcmp(name, m_ActionMaps[i].name) == 0)
    {
      return i;
    }
  }
  return InputHandle(UINT32_MAX);
}

InputHandle InputService::findAction(const char* name) const
{
  // TODO: move to hash map ?
  for (uint32_t i = 0; i < m_Actions.m_Size; i++)
  {
    if (strcmp(name, m_Actions[i].name) == 0)
    {
      return i;
    }
  }
  return InputHandle(UINT32_MAX);
}

void InputService::addButton(
    InputHandle action, DevicePart devicePart, uint16_t button, bool repeat)
{
  const InputAction& bindingAction = m_Actions[action];

  InputBinding binding;
  binding.set(BINDING_TYPE_BUTTON, deviceFromPart(devicePart), devicePart, button, 0, 0, repeat)
      .setHandles(bindingAction.actionMap, action);

  m_Bindings.push(binding);
}

void InputService::addAxis1d(
    InputHandle action, DevicePart devicePart, uint16_t axis, float minDeadzone, float maxDeadzone)
{
  const InputAction& bindingAction = m_Actions[action];

  InputBinding binding;
  binding.set(BINDING_TYPE_AXIS_1D, deviceFromPart(devicePart), devicePart, axis, 0, 0, 0)
      .setDeadzones(minDeadzone, maxDeadzone)
      .setHandles(bindingAction.actionMap, action);

  m_Bindings.push(binding);
}

void InputService::addAxis2d(
    InputHandle action,
    DevicePart devicePart,
    uint16_t axisX,
    uint16_t axisY,
    float minDeadzone,
    float maxDeadzone)
{
  const InputAction& bindingAction = m_Actions[action];

  InputBinding binding, bindingX, bindingY;
  binding.set(BINDING_TYPE_AXIS_2D, deviceFromPart(devicePart), devicePart, UINT16_MAX, 1, 0, 0)
      .setDeadzones(minDeadzone, maxDeadzone)
      .setHandles(bindingAction.actionMap, action);
  bindingX.set(BINDING_TYPE_AXIS_2D, deviceFromPart(devicePart), devicePart, axisX, 0, 1, 0)
      .setDeadzones(minDeadzone, maxDeadzone)
      .setHandles(bindingAction.actionMap, action);
  bindingY.set(BINDING_TYPE_AXIS_2D, deviceFromPart(devicePart), devicePart, axisY, 0, 1, 0)
      .setDeadzones(minDeadzone, maxDeadzone)
      .setHandles(bindingAction.actionMap, action);

  m_Bindings.push(binding);
  m_Bindings.push(bindingX);
  m_Bindings.push(bindingY);
}

void InputService::addVector1d(
    InputHandle action,
    DevicePart devicePartPos,
    uint16_t buttonPos,
    DevicePart devicePartNeg,
    uint16_t buttonNeg,
    bool repeat)
{
  const InputAction& bindingAction = m_Actions[action];

  InputBinding binding, bindingPositive, bindingNegative;
  binding
      .set(
          BINDING_TYPE_VECTOR_1D,
          deviceFromPart(devicePartPos),
          devicePartPos,
          UINT16_MAX,
          1,
          0,
          repeat)
      .setHandles(bindingAction.actionMap, action);
  bindingPositive
      .set(
          BINDING_TYPE_VECTOR_1D,
          deviceFromPart(devicePartPos),
          devicePartPos,
          buttonPos,
          0,
          1,
          repeat)
      .setHandles(bindingAction.actionMap, action);
  bindingNegative
      .set(
          BINDING_TYPE_VECTOR_1D,
          deviceFromPart(devicePartNeg),
          devicePartNeg,
          buttonNeg,
          0,
          1,
          repeat)
      .setHandles(bindingAction.actionMap, action);

  m_Bindings.push(binding);
  m_Bindings.push(bindingPositive);
  m_Bindings.push(bindingNegative);
}

void InputService::addVector2d(
    InputHandle action,
    DevicePart devicePartUp,
    uint16_t buttonUp,
    DevicePart devicePartDown,
    uint16_t buttonDown,
    DevicePart devicePartLeft,
    uint16_t buttonLeft,
    DevicePart devicePartRight,
    uint16_t buttonRight,
    bool repeat)
{
  const InputAction& bindingAction = m_Actions[action];

  InputBinding binding, bindingUp, bindingDown, bindingLeft, bindingRight;

  binding
      .set(
          BINDING_TYPE_VECTOR_2D,
          deviceFromPart(devicePartUp),
          devicePartUp,
          UINT16_MAX,
          1,
          0,
          repeat)
      .setHandles(bindingAction.actionMap, action);
  bindingUp
      .set(
          BINDING_TYPE_VECTOR_2D,
          deviceFromPart(devicePartUp),
          devicePartUp,
          buttonUp,
          0,
          1,
          repeat)
      .setHandles(bindingAction.actionMap, action);
  bindingDown
      .set(
          BINDING_TYPE_VECTOR_2D,
          deviceFromPart(devicePartDown),
          devicePartDown,
          buttonDown,
          0,
          1,
          repeat)
      .setHandles(bindingAction.actionMap, action);
  bindingLeft
      .set(
          BINDING_TYPE_VECTOR_2D,
          deviceFromPart(devicePartLeft),
          devicePartLeft,
          buttonLeft,
          0,
          1,
          repeat)
      .setHandles(bindingAction.actionMap, action);
  bindingRight
      .set(
          BINDING_TYPE_VECTOR_2D,
          deviceFromPart(devicePartRight),
          devicePartRight,
          buttonRight,
          0,
          1,
          repeat)
      .setHandles(bindingAction.actionMap, action);

  m_Bindings.push(binding);
  m_Bindings.push(bindingUp);
  m_Bindings.push(bindingDown);
  m_Bindings.push(bindingLeft);
  m_Bindings.push(bindingRight);
}

void InputService::newFrame()
{
  // Cache previous frame keys.
  // Resetting previous frame breaks key pressing - there can be more frames between key presses
  // even if continuously pressed.
  for (uint32_t i = 0; i < KEY_COUNT; ++i)
  {
    m_PreviousKeys[i] = m_Keys[i];
    // keys[ i ] = 0;
  }

  for (uint32_t i = 0; i < MOUSE_BUTTONS_COUNT; ++i)
  {
    m_PreviousMouseButton[i] = m_MouseButton[i];
  }

  for (uint32_t i = 0; i < kMaxGamepads; ++i)
  {
    if (m_Gamepads[i].isAttached())
    {
      for (uint32_t k = 0; k < GAMEPAD_BUTTON_COUNT; k++)
      {
        m_Gamepads[i].m_PreviousButtons[k] = m_Gamepads[i].m_Buttons[k];
      }
    }
  }
}

void InputService::update(float delta)
{

  // Update Mouse
  m_PreviousMousePosition = m_MousePosition;
  // Update current mouse state
  g_InputBackend.getMouseState(m_MousePosition, m_MouseButton, MOUSE_BUTTONS_COUNT);

  for (uint32_t i = 0; i < MOUSE_BUTTONS_COUNT; ++i)
  {
    // Just clicked. Save position
    if (isMouseClicked((MouseButtons)i))
    {
      m_MouseClickedPosition[i] = m_MousePosition;
    }
    else if (isMouseDown((MouseButtons)i))
    {
      float deltaX = m_MousePosition.x - m_MouseClickedPosition[i].x;
      float deltaY = m_MousePosition.y - m_MouseClickedPosition[i].y;
      m_MouseDragDistance[i] = sqrtf((deltaX * deltaX) + (deltaY * deltaY));
    }
  }

  // NEW UPDATE

  // Update all actions maps
  // Update all actions
  // Scan each action of the map
  for (uint32_t j = 0; j < m_Actions.m_Size; j++)
  {
    InputAction& inputAction = m_Actions[j];
    inputAction.value = {0, 0};
  }

  // Read all input values for each binding
  // First get all the button or composite parts. Composite input will be calculated after.
  for (uint32_t k = 0; k < m_Bindings.m_Size; k++)
  {
    InputBinding& inputBinding = m_Bindings[k];
    // Skip composite bindings. Their value will be calculated after.
    if (inputBinding.m_IsComposite)
      continue;

    inputBinding.m_Value = false;

    switch (inputBinding.m_Device)
    {
    case DEVICE_KEYBOARD: {
      bool key_value = inputBinding.m_Repeat ? isKeyDown((Keys)inputBinding.m_Button)
                                             : isKeyJustPressed((Keys)inputBinding.m_Button, false);
      inputBinding.m_Value = key_value ? 1.0f : 0.0f;
      break;
    }

    case DEVICE_GAMEPAD: {
      Gamepad& gamepad = m_Gamepads[0];
      if (gamepad.m_Handle == nullptr)
      {
        break;
      }

      const float minDeadzone = inputBinding.m_MinDeadzone;
      const float maxDeadzone = inputBinding.m_MaxDeadzone;

      switch (inputBinding.m_DevicePart)
      {
      case DEVICE_PART_GAMEPAD_AXIS: {
        inputBinding.m_Value = gamepad.m_Axis[inputBinding.m_Button];
        inputBinding.m_Value =
            fabs(inputBinding.m_Value) < minDeadzone ? 0.0f : inputBinding.m_Value;
        inputBinding.m_Value = fabs(inputBinding.m_Value) > maxDeadzone
                                   ? (inputBinding.m_Value < 0 ? -1.0f : 1.0f)
                                   : inputBinding.m_Value;

        break;
      }
      case DEVICE_PART_GAMEPAD_BUTTONS: {
        // input_binding.value = gamepad.buttons[ input_binding.button ];
        inputBinding.m_Value =
            inputBinding.m_Repeat
                ? gamepad.isButtonDown((GamepadButtons)inputBinding.m_Button)
                : gamepad.isButtonJustPressed((GamepadButtons)inputBinding.m_Button);
        break;
      }
        /*case InputBinding::GAMEPAD_HAT:
        {
            input_binding.value = gamepad.hats[ input_binding.button ];
            break;
        }*/
      }
    }
    }
  }

  for (uint32_t k = 0; k < m_Bindings.m_Size; k++)
  {
    InputBinding& inputBinding = m_Bindings[k];

    if (inputBinding.m_IsPartOfComposite)
      continue;

    InputAction& inputAction = m_Actions[inputBinding.m_ActionIndex];

    switch (inputBinding.m_Type)
    {
    case BINDING_TYPE_BUTTON: {
      inputAction.value.x = fmaxf(inputAction.value.x, inputBinding.m_Value ? 1.0f : 0.0f);
      break;
    }

    case BINDING_TYPE_AXIS_1D: {
      inputAction.value.x =
          inputBinding.m_Value != 0.f ? inputBinding.m_Value : inputAction.value.x;
      break;
    }

    case BINDING_TYPE_AXIS_2D: {
      // Retrieve following 2 bindings
      InputBinding& input_bindingX = m_Bindings[++k];
      InputBinding& input_bindingY = m_Bindings[++k];

      inputAction.value.x =
          input_bindingX.m_Value != 0.0f ? input_bindingX.m_Value : inputAction.value.x;
      inputAction.value.y =
          input_bindingY.m_Value != 0.0f ? input_bindingY.m_Value : inputAction.value.y;

      break;
    }

    case BINDING_TYPE_VECTOR_1D: {
      // Retrieve following 2 bindings
      InputBinding& inputBindingPos = m_Bindings[++k];
      InputBinding& inputBindingNeg = m_Bindings[++k];

      inputAction.value.x = inputBindingPos.m_Value   ? inputBindingPos.m_Value
                            : inputBindingNeg.m_Value ? -inputBindingNeg.m_Value
                                                      : inputAction.value.x;
      break;
    }

    case BINDING_TYPE_VECTOR_2D: {
      // Retrieve following 4 bindings
      InputBinding& inputBindingUp = m_Bindings[++k];
      InputBinding& inputBindingDown = m_Bindings[++k];
      InputBinding& inputBindingLeft = m_Bindings[++k];
      InputBinding& inputBindingRight = m_Bindings[++k];

      inputAction.value.x = inputBindingRight.m_Value  ? 1.0f
                            : inputBindingLeft.m_Value ? -1.0f
                                                       : inputAction.value.x;
      inputAction.value.y = inputBindingUp.m_Value     ? 1.0f
                            : inputBindingDown.m_Value ? -1.0f
                                                       : inputAction.value.y;
      break;
    }
    }
  }
}

void InputService::debugUi()
{
  if (ImGui::Begin("Input"))
  {
    ImGui::Text("Has focus %u", m_HasFocus ? 1 : 0);

    if (ImGui::TreeNode("Devices"))
    {
      ImGui::Separator();
      if (ImGui::TreeNode("Gamepads"))
      {
        for (uint32_t i = 0; i < kMaxGamepads; ++i)
        {
          const Gamepad& g = m_Gamepads[i];
          ImGui::Text("Name: %s, id %d, index %u", g.m_Name, g.m_Id, g.m_Index);
          // Attached gamepad
          if (g.isAttached())
          {
            ImGui::NewLine();
            ImGui::Columns(GAMEPAD_AXIS_COUNT);
            for (uint32_t gi = 0; gi < GAMEPAD_AXIS_COUNT; gi++)
            {
              ImGui::Text("%s", gamepadAxisNames()[gi]);
              ImGui::NextColumn();
            }
            for (uint32_t gi = 0; gi < GAMEPAD_AXIS_COUNT; gi++)
            {
              ImGui::Text("%f", g.m_Axis[gi]);
              ImGui::NextColumn();
            }
            ImGui::NewLine();
            ImGui::Columns(GAMEPAD_BUTTON_COUNT);
            for (uint32_t gi = 0; gi < GAMEPAD_BUTTON_COUNT; gi++)
            {
              ImGui::Text("%s", gamepadButtonNames()[gi]);
              ImGui::NextColumn();
            }
            ImGui::Columns(GAMEPAD_BUTTON_COUNT);
            for (uint32_t gi = 0; gi < GAMEPAD_BUTTON_COUNT; gi++)
            {
              ImGui::Text("%u", g.m_Buttons[gi]);
              ImGui::NextColumn();
            }

            ImGui::Columns(1);
          }
          ImGui::Separator();
        }
        ImGui::TreePop();
      }

      ImGui::Separator();
      if (ImGui::TreeNode("Mouse"))
      {
        ImGui::Text("Position     %f,%f", m_MousePosition.x, m_MousePosition.y);
        ImGui::Text("Previous pos %f,%f", m_PreviousMousePosition.x, m_PreviousMousePosition.y);

        ImGui::Separator();

        for (uint32_t i = 0; i < MOUSE_BUTTONS_COUNT; i++)
        {
          ImGui::Text("Button %u", i);
          ImGui::SameLine();
          ImGui::Text(
              "Clicked Position     %4.1f,%4.1f",
              m_MouseClickedPosition[i].x,
              m_MouseClickedPosition[i].y);
          ImGui::SameLine();
          ImGui::Text("Button %u, Previous %u", m_MouseButton[i], m_PreviousMouseButton[i]);
          ImGui::SameLine();
          ImGui::Text("Drag %f", m_MouseDragDistance[i]);

          ImGui::Separator();
        }
        ImGui::TreePop();
      }

      ImGui::Separator();
      if (ImGui::TreeNode("Keyboard"))
      {
        for (uint32_t i = 0; i < KEY_LAST; i++) {}
        ImGui::TreePop();
      }
      ImGui::TreePop();
    }

    if (ImGui::TreeNode("Actions"))
    {

      for (uint32_t j = 0; j < m_Actions.m_Size; j++)
      {
        const InputAction& inputAction = m_Actions[j];
        ImGui::Text(
            "Action %s, x %2.3f y %2.3f",
            inputAction.name,
            inputAction.value.x,
            inputAction.value.y);
      }

      ImGui::TreePop();
    }

    if (ImGui::TreeNode("Bindings"))
    {
      for (uint32_t k = 0; k < m_Bindings.m_Size; k++)
      {
        const InputBinding& binding = m_Bindings[k];
        const InputAction& parentAction = m_Actions[binding.m_ActionIndex];

        const char* buttonName = "";
        switch (binding.m_DevicePart)
        {
        case DEVICE_PART_KEYBOARD: {
          buttonName = getKeyNames()[binding.m_Button];
          break;
        }
        case DEVICE_PART_MOUSE: {
          break;
        }
        case DEVICE_PART_GAMEPAD_AXIS: {
          break;
        }
        case DEVICE_PART_GAMEPAD_BUTTONS: {
          break;
        }
        }

        switch (binding.m_Type)
        {
        case BINDING_TYPE_VECTOR_1D: {
          ImGui::Text(
              "Binding action %s, type %s, value %f, composite %u, part of composite %u, button %s",
              parentAction.name,
              "vector 1d",
              binding.m_Value,
              binding.m_IsComposite,
              binding.m_IsPartOfComposite,
              buttonName);
          break;
        }
        case BINDING_TYPE_VECTOR_2D: {
          ImGui::Text(
              "Binding action %s, type %s, value %f, composite %u, part of composite %u",
              parentAction.name,
              "vector 2d",
              binding.m_Value,
              binding.m_IsComposite,
              binding.m_IsPartOfComposite);
          break;
        }
        case BINDING_TYPE_AXIS_1D: {
          ImGui::Text(
              "Binding action %s, type %s, value %f, composite %u, part of composite %u",
              parentAction.name,
              "axis 1d",
              binding.m_Value,
              binding.m_IsComposite,
              binding.m_IsPartOfComposite);
          break;
        }
        case BINDING_TYPE_AXIS_2D: {
          ImGui::Text(
              "Binding action %s, type %s, value %f, composite %u, part of composite %u",
              parentAction.name,
              "axis 2d",
              binding.m_Value,
              binding.m_IsComposite,
              binding.m_IsPartOfComposite);
          break;
        }
        case BINDING_TYPE_BUTTON: {
          ImGui::Text(
              "Binding action %s, type %s, value %f, composite %u, part of composite %u, button %s",
              parentAction.name,
              "button",
              binding.m_Value,
              binding.m_IsComposite,
              binding.m_IsPartOfComposite,
              buttonName);
          break;
        }
        }
      }

      ImGui::TreePop();
    }
  }
  ImGui::End();
}

/// InputAction

bool InputAction::triggered() const { return value.x != 0.0f; }

float InputAction::readValue1d() const { return value.x; }

InputVector2 InputAction::readValue2d() const { return value; }

InputBinding& InputBinding::set(
    BindingType type,
    Device device,
    DevicePart devicePart,
    uint16_t button,
    uint8_t isComposite,
    uint8_t isPartOfComposite,
    uint8_t repeat)
{
  m_Type = type;
  m_Device = device;
  m_DevicePart = devicePart;
  m_Button = button;
  m_IsComposite = isComposite;
  m_IsPartOfComposite = isPartOfComposite;
  m_Repeat = repeat;
  return *this;
}

InputBinding& InputBinding::setDeadzones(float min, float max)
{
  m_MinDeadzone = min;
  m_MaxDeadzone = max;
  return *this;
}

InputBinding& InputBinding::setHandles(InputHandle actionMap, InputHandle action)
{
  // Don't expect this to have more than 256.
  assert(actionMap < 256);
  assert(action < 16636);

  m_ActionMapIndex = (uint8_t)actionMap;
  m_ActionIndex = (uint16_t)action;

  return *this;
}

} // namespace Framework
