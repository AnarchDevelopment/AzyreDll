/**
 * Aegleseeker Client - Modules Database
 * AegleDllMSVC C++ Modules Registry
 */

const aegleModules = [
  // Combat Category
  {
    id: 'hitbox',
    name: 'Hitbox Expander',
    category: 'Combat',
    keybind: 'NONE',
    status: 'Active',
    description: 'Dynamically expands entity bounding boxes in memory for enhanced collision detection and target accuracy.',
    tags: ['Hitbox', 'Combat', 'Entity']
  },
  {
    id: 'reach',
    name: 'Reach Modifier',
    category: 'Combat',
    keybind: 'NONE',
    status: 'Active',
    description: 'Customizes maximum melee attack reach distance via direct hooks into the rendering & interaction matrices.',
    tags: ['Reach', 'Combat', 'Distance']
  },

  // Movement Category
  {
    id: 'autosprint',
    name: 'AutoSprint',
    category: 'Movement',
    keybind: 'KEY_V',
    status: 'Active',
    description: 'Automatically maintains sprint state whenever moving forward without needing to hold down a sprint key.',
    tags: ['AutoSprint', 'Movement', 'Speed']
  },
  {
    id: 'timer',
    name: 'Game Timer',
    category: 'Movement',
    keybind: 'NONE',
    status: 'Active',
    description: 'Modifies the internal game tick loop speed to accelerate or smooth player movement and world physics.',
    tags: ['Timer', 'Tick', 'Speed']
  },

  // Visuals Category
  {
    id: 'clickgui',
    name: 'ImGui ClickGUI',
    category: 'Visuals',
    keybind: 'INSERT',
    status: 'Core',
    description: 'Advanced DirectX 11 overlay interface featuring tabbed navigation, smooth animations, and theme presets.',
    tags: ['GUI', 'Menu', 'DirectX11']
  },
  {
    id: 'watermark',
    name: 'Watermark HUD',
    category: 'Visuals',
    keybind: 'NONE',
    status: 'Active',
    description: 'On-screen watermark displaying the Aegleseeker Client branding, version tag, and real-time performance state.',
    tags: ['HUD', 'Watermark', 'Overlay']
  },
  {
    id: 'fullbright',
    name: 'FullBright',
    category: 'Visuals',
    keybind: 'KEY_B',
    status: 'Active',
    description: 'Boosts scene gamma illumination to eliminate dark shadows in caves and dimensions without shader distortion.',
    tags: ['Gamma', 'Lighting', 'Visuals']
  },
  {
    id: 'keystrokes',
    name: 'Keystrokes HUD',
    category: 'Visuals',
    keybind: 'NONE',
    status: 'Active',
    description: 'Renders a real-time HUD widget reflecting key inputs (WASD, Left Click, Right Click).',
    tags: ['Keystrokes', 'HUD', 'Input']
  },
  {
    id: 'cpscounter',
    name: 'CPS Counter',
    category: 'Visuals',
    keybind: 'NONE',
    status: 'Active',
    description: 'Calculates and displays exact Clicks Per Second for both left and right mouse buttons.',
    tags: ['CPS', 'Stats', 'HUD']
  },
  {
    id: 'fpsoverlay',
    name: 'FPS Overlay',
    category: 'Visuals',
    keybind: 'NONE',
    status: 'Active',
    description: 'Monitors DirectX 11 frame rates with minimal CPU/GPU overhead.',
    tags: ['FPS', 'Performance', 'HUD']
  },
  {
    id: 'pingcounter',
    name: 'Ping Counter',
    category: 'Visuals',
    keybind: 'NONE',
    status: 'Active',
    description: 'Displays current network latency in milliseconds to the Minecraft Bedrock server.',
    tags: ['Ping', 'Network', 'HUD']
  },
  {
    id: 'renderinfo',
    name: 'Render Info',
    category: 'Visuals',
    keybind: 'NONE',
    status: 'Active',
    description: 'HUD panel displaying exact player coordinates (X, Y, Z), biome name, and process memory usage.',
    tags: ['Coords', 'Info', 'Stats']
  },
  {
    id: 'motionblur',
    name: 'Motion Blur',
    category: 'Visuals',
    keybind: 'NONE',
    status: 'Active',
    description: 'Cinematic motion blur effect implemented via Direct3D 11 frame accumulation buffer blending.',
    tags: ['Shaders', 'MotionBlur', 'FX']
  },

  // Misc Category
  {
    id: 'autoclicker',
    name: 'AutoClicker',
    category: 'Misc',
    keybind: 'NONE',
    status: 'Active',
    description: 'Automates mouse clicks at a configurable frequency with random jitter to mimic human clicking patterns.',
    tags: ['Clicker', 'Automation', 'Misc']
  },
  {
    id: 'antiafk',
    name: 'AntiAFK',
    category: 'Misc',
    keybind: 'NONE',
    status: 'Active',
    description: 'Executes subtle micro-movements to prevent idle timeouts on multiplayer servers.',
    tags: ['AntiAFK', 'Misc', 'Automation']
  },
  {
    id: 'unlockfps',
    name: 'Unlock FPS',
    category: 'Misc',
    keybind: 'NONE',
    status: 'Active',
    description: 'Removes v-sync constraints and default graphics engine caps to achieve maximum possible frame rates.',
    tags: ['FPS', 'Unlock', 'Performance']
  },
  {
    id: 'screenshot',
    name: 'Screenshot Helper',
    category: 'Misc',
    keybind: 'KEY_F2',
    status: 'Active',
    description: 'Captures high-resolution screenshots directly from the DirectX 11 backbuffer and saves them locally.',
    tags: ['Screenshot', 'Capture', 'Misc']
  }
];

if (typeof module !== 'undefined' && module.exports) {
  module.exports = aegleModules;
}
