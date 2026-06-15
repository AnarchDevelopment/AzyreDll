# Contributing to AegleInternal

Thank you for your interest in contributing to AegleInternal! This document provides guidelines and instructions for contributing to the project.

## Code of Conduct

- **Be Respectful**: Treat all contributors with respect
- **Be Constructive**: Provide helpful feedback and criticism
- **Be Inclusive**: Welcome diverse perspectives and backgrounds
- **Be Professional**: Maintain professionalism in all interactions

## Getting Started

### Prerequisites

- Git (for cloning and managing changes)
- CMake 3.15 or later
- C++ compiler (MSVC 2015+ or MinGW-w64)
- Windows SDK (for DirectX 11 headers)

### Setting Up Development Environment

```bash
# Clone the repository
git clone https://github.com/iVyz3r/aegledll.git
cd AegleInternal

# Create a feature branch
git checkout -b feature/your-feature-name

# Set up build environment
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build .
```

## Ways to Contribute

### 1. Bug Reports

Provide detailed bug reports:

**Include:**
- Operating system and version
- DirectX version and GPU model
- Steps to reproduce the issue
- Expected vs actual behavior
- Relevant screenshots or logs
- Error messages or stack traces

**Example:**
```
Title: Motion blur crashes on Intel HD Graphics

OS: Windows 10 x64
GPU: Intel HD Graphics 630
DirectX: 9.1 (fallback to 11.0)

Steps to reproduce:
1. Enable motion blur
2. Set blur mode to "Real Motion Blur"
3. Set intensity to maximum

Expected: Smooth blur effect
Actual: DirectX device lost error and crash

Screenshot: [attach screenshot]
```

### 2. Feature Requests

Suggest new features:

- **Description**: Clear description of the feature
- **Use Case**: Why this feature is needed
- **Implementation Ideas**: Suggested approach (optional)
- **Alternatives**: Alternative solutions considered

**Example:**
```
Title: Add customizable hotkeys

Use Case: Different users prefer different hotkeys for menu toggle.
Currently hardcoded to INSERT key.

Implementation: Store hotkey preferences in ImGui config, add 
settings panel for customization.
```

### 3. Code Contributions

### Code Style Guidelines

#### Naming Conventions

```cpp
// Constants: g_variableName for globals
float g_menuAnim = 0.0f;

// Functions: PascalCase
void UpdateKeyboardInput();
ImVec4 LerpColors(ImVec4 a, ImVec4 b, float t);

// Local variables: camelCase
int currentTab = 0;
ImVec2 mousePos = ImGui::GetIO().MousePos;

// Classes/Structs: PascalCase
struct HudElement {
    ImVec2 pos;
};
```

#### Code Formatting

```cpp
// Spaces around operators
if (a > b && c < d) {
    x = (a + b) * c;
}

// 4-space indentation
if (condition) {
    DoSomething();
    if (nested) {
        DoNestedThing();
    }
}

// Braces on same line
void Function() {
    // Implementation
}

// Comments above code
// Update animation timing
float elapsed = GetTickCount64() - g_animStartTime;
```

#### Comment Style

```cpp
// Short comment for simple statements
float value = 1.0f;

// Longer multi-line comment for complex logic
// Check if hitbox should be scaled based on animation state
// Animation frames 0-10 use base hitbox
// Frames 10-20 use 1.5x scaling for jump animation
if (animFrame < 10) {
    scale = 1.0f;
} else {
    scale = 1.5f;
}

// Avoid: Obvious comments that add no value
// Increment counter (AVOID THIS)
counter++;
```

#### Function Documentation

```cpp
// GetAnimationEasing - Samples easing curve based on time
// Parameters:
//   time: Elapsed time in milliseconds
//   duration: Total animation duration in milliseconds
// Returns: Interpolation factor (0.0 to 1.0)
float GetAnimationEasing(ULONGLONG time, ULONGLONG duration) {
    float t = (float)time / (float)duration;
    return fminf(1.0f, t);  // Clamp to 1.0
}
```

### Pull Request Process

1. **Fork the repository** and create a feature branch
   ```bash
   git checkout -b feature/descriptive-name
   ```

2. **Make your changes** following the style guide

3. **Test thoroughly**
   ```bash
   mkdir build && cd build
   cmake .. -DCMAKE_BUILD_TYPE=Release
   cmake --build .
   ```

4. **Commit with clear messages**
   ```bash
   git commit -m "Add motion blur intensity slider
   
   - Added new slider control in Visuals tab
   - Range: 0.0 to 10.0
   - Default: 3.0
   - Closes #42"
   ```

5. **Push to your fork**
   ```bash
   git push origin feature/descriptive-name
   ```

6. **Open a Pull Request** with:
   - Clear title and description
   - Reference to related issues
   - Screenshots for UI changes
   - Test results

### Commit Messages

**Format:**
```
<type>: <subject>

<body>

<footer>
```

**Types:**
- `feat`: New feature
- `fix`: Bug fix
- `refactor`: Code restructuring
- `style`: Formatting changes
- `docs`: Documentation updates
- `test`: Test additions

**Examples:**
```
feat: Add CPS counter customization

- Add alignment options (left, center, right)
- Add position offset configuration
- Add text color picker
- Update settings persistence

Closes #123

---

fix: Prevent menu freeze on window resize

- Added proper size validation
- Fixed race condition in resize handler
- Added null pointer checks

Closes #456

---

docs: Update README build instructions

- Add CMake configuration examples
- Clarify Windows SDK requirements
- Add troubleshooting section
```

## Development Workflow

### Understanding the Architecture

**Main Components:**
1. **dllmain.cpp**: Core DLL entry point and hook implementation
2. **ImGui Module**: User interface rendering
3. **MinHook Module**: API hooking infrastructure
4. **Backend Systems**: DirectX 11, input handling, animations

### Key Files to Know

- `dllmain.cpp`: Main hook function, render loop, menu logic
- `ImGui/imgui.cpp`: Core ImGui implementation
- `minhook/hook.c`: Hook installation and management

### Testing Changes

Before submitting a PR:

```cpp
// Test in isolation
void TestNewFeature() {
    // Arrange
    bool enabled = false;
    
    // Act
    ToggleFeature(enabled);
    
    // Assert
    assert(enabled == true);
}
```

### Performance Considerations

All changes should maintain or improve performance:

- Profile hooks for CPU overhead
- Check GPU memory usage
- Minimize allocations in render loop
- Use static alloc where possible

## Documentation

### README Updates

Update README.md for:
- New features
- Additional dependencies
- Build process changes
- API changes

### Code Comments

Comment requirements:
- **Complex algorithms**: Explain the approach
- **Non-obvious logic**: Why this way?
- **Important invariants**: What must be true?
- **Platform specifics**: Windows-specific code

### Avoid Commenting

Don't comment:
- Obvious variable assignments
- Simple control flow
- Standard library usage
- Well-named code

## Issue Labels

- `bug`: Something isn't working
- `enhancement`: New feature or improvement
- `documentation`: Documentation updates
- `help-wanted`: Seeking contributions
- `good-first-issue`: Good for newcomers
- `performance`: Performance improvement
- `security`: Security-related issue

## Review Process

### Code Review Checklist

Reviewers will check:

- [ ] Code follows style guidelines
- [ ] Changes are well-tested
- [ ] Performance impact is acceptable
- [ ] No security issues introduced
- [ ] Compiler warnings addressed
- [ ] Documentation is updated
- [ ] Comments are clear
- [ ] Tests pass

### Feedback

- Reviews aim to catch issues early
- Feedback is about code, not person
- Questions are genuinely curious
- Suggestions are constructive

### Appeal Process

Disagree with review feedback?

1. Comment on the PR with your perspective
2. Reference relevant documentation
3. Provide technical reasoning
4. Maintainers make final decision

## Project Roadmap

### Planned Features

- [ ] Additional motion blur modes
- [ ] Advanced hitbox debugging
- [ ] Profile-based configurations
- [ ] Memory optimization
- [ ] Additional UI customization

### Architecture Improvements

- [ ] Decouple module system
- [ ] Add settings persistence
- [ ] Improve animation framework
- [ ] Better error handling

## Recognition

Contributors are recognized:

- In commit history
- In release notes (for major contributions)
- In CONTRIBUTORS file (if applicable)

## Questions?

- Check existing issues and PRs
- Review the README and documentation
- Ask in a new issue with `question` label
- Join community discussions

## Legal

By contributing, you agree that:

- Your contributions are provided under MIT license
- You have the right to contribute the code
- You understand the project's purpose and limitations

## Thank You!

Your contributions help make AegleInternal better for everyone. Thank you for your time and effort!

---

**Last Updated**: April 3, 2026  
**Contribution Guidelines Version**: 1.0
