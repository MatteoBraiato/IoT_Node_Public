# AGENTS.md

## Project Overview

This project is an embedded IoT node based on ESP-IDF.

Its purpose is to operate as a remote outdoor environmental sensing station:
- read environmental sensor data
- collect system and battery-related information
- transmit data through an external LoRa module to a gateway
- maximize battery life through aggressive low-power design
- remain diagnosable and robust in remote deployments

This is a battery-powered node intended for remote installation, so the software must prioritize:
1. **low power consumption**
2. **robustness and fault detection**
3. **predictable behavior**
4. **maintainability and modularity**

This system must be **fail-safe as much as possible**, and specifically **not fail-silent**:
- faults should not be hidden
- abnormal conditions should be detectable
- errors should be propagated, logged, or encoded in a way that allows diagnosis
- silent degradation is discouraged unless explicitly intended and documented

---

## Project Structure

### `cfg/`
Project configuration files.

Use this folder for:
- configuration headers
- build-time settings
- feature configuration
- project-wide constants when configuration-oriented

---

### `lib/`
Handmade custom libraries.

Use this folder for:
- reusable helper modules
- custom utilities shared across the project
- logic that is not specific to one hardware abstraction layer

Avoid placing application-specific orchestration logic here.

---

### `main/`
Contains the main project entry point.

Use this folder for:
- startup entry
- bootstrapping
- initialization sequencing if required by the framework

Keep it minimal. Application logic should not accumulate here.

---

### `managed_components/`
Third-party ESP-IDF managed components.

Rules:
- treat this folder as external/vendor code
- do not modify files here unless explicitly requested
- prefer adaptation through wrappers or integration code in the project instead of patching vendor code directly

---

### `src/`
Main application source files.

Important files include:
- `app.c` → main application logic
- `e5lora.c` → LoRa communication handling for the external LoRa chip

This layer contains the application behavior and orchestration.

Rules:
- `app.c` may call BSP-level modules directly
- application code should remain readable and explicit
- avoid mixing low-level hardware details directly into application logic
- application behavior must remain easy to trace during debugging and failure analysis

---

### `system/`
Hardware abstraction and system support layer.

This is a key architectural area of the project.

#### `system/bsp/`
Board Support Package / high-level hardware wrappers.

This layer includes wrappers for:
- MCU peripherals (ADC, UART, digital outputs, etc.)
- external chips and devices (for example BME280, MAX17048, etc.)

Rules:
- this layer **can be called directly by `app`**
- this layer should expose safe, simple, application-oriented interfaces
- it should hide lower-level details from the application
- it should handle parameter validation and meaningful error propagation whenever reasonable

#### `system/bdi/`
Low-level drivers / lower abstraction layer.

This layer is generally one level below BSP.

Rules:
- this layer is **never called directly by `app`**
- it is only used by `bsp`
- it should contain lower-level, hardware-specific, or peripheral-specific logic
- it should stay narrow in scope and avoid application policy decisions

#### `system/hw_cfg.h`
Hardware configuration definitions.

This file contains hardware-specific configuration such as:
- pin mapping
- board-level hardware settings
- configuration constants tied to the actual hardware design

Treat this file as a central hardware definition point.

---

## Architectural Intent

The intended architectural direction is:

- **Application layer (`src/`)**
  - defines behavior
  - orchestrates sensing, transmission, sleep/wake flow, diagnostics, retries, fault handling

- **BSP layer (`system/bsp/`)**
  - provides application-facing hardware abstractions
  - wraps peripherals and external devices
  - converts low-level details into usable APIs

- **BDI layer (`system/bdi/`)**
  - contains low-level hardware access
  - handles raw peripheral/chip interaction
  - is not application-aware

The agent should preserve this layering.
Do not bypass layers unless explicitly justified.

---

## Functional Priorities

When making decisions, prioritize in this order:

1. **Correctness**
2. **Power efficiency**
3. **Fault observability / diagnosability**
4. **Robustness in remote deployment**
5. **Clarity and maintainability**
6. **Code elegance**

A more elegant solution is not better if it:
- increases power consumption
- hides faults
- makes field debugging harder
- weakens determinism

---

## Low Power Design Principles

This node is battery-powered, so low power is a core design constraint.

When proposing or implementing changes, always consider:
- wake time minimization
- avoiding unnecessary peripheral enable time
- minimizing UART / sensor / radio active time
- minimizing retries that waste energy
- avoiding polling when a simpler approach exists
- ensuring proper shutdown / sleep preparation
- avoiding unnecessary logging or background activity in production paths
- avoiding architectural choices that keep hardware powered longer than needed

Any change that could significantly impact current consumption must be explicitly called out.

---

## Fault Handling Philosophy

This system is deployed remotely, so hidden failures are unacceptable.

The code should prefer:
- explicit return values
- status codes
- structured error propagation
- detection of invalid states
- defensive checks on inputs and hardware responses
- clear distinction between success, transient failure, and permanent failure
- making faults diagnosable through logs, error flags, counters, state variables, or encoded uplink data when appropriate

Avoid:
- swallowing errors silently
- returning success when the operation is uncertain
- hiding communication failures
- masking sensor faults without traceability
- catch-all fallback behavior that prevents diagnosis

When a fault cannot be recovered locally, the software should fail in a controlled and observable way.

---

## Coding Expectations

The agent must work in a **meticulous, careful, and precise** way.

General expectations:
- prefer small, targeted changes
- preserve the existing architecture unless there is a strong reason not to
- avoid unnecessary refactors
- avoid introducing abstractions that are not justified by actual project needs
- keep embedded constraints in mind at all times
- prefer explicit and readable code over overly clever code
- document assumptions clearly
- do not invent behavior that is not grounded in the codebase structure or the request

When adding logic:
- think about error paths, not only happy paths
- think about power impact
- think about startup/shutdown sequencing
- think about what happens in remote unattended operation
- think about whether the failure would be detectable in the field

---

## Rules for Modifying the Project

### For non-trivial changes
Before making any non-trivial modification, the agent must first provide:

1. **a short proposal of the intended change**
2. **the reason for the change**
3. **the list of impacted modules/files**
4. **key risks or trade-offs**
5. **explicit confirmation request before proceeding**, if the impact is significant

Examples of changes that should be treated as non-trivial:
- interface changes across layers
- changes affecting power behavior
- changes affecting boot, sleep, wake, or transmission flow
- changes affecting error handling architecture
- changes affecting public APIs of modules
- refactors touching multiple files
- changes in hardware abstraction responsibilities
- protocol/data format changes
- retry policy changes
- initialization/deinitialization sequence changes

---

### For significant-impact changes
If the requested change has relevant architectural, functional, or power impact, the agent must:
- stop before applying the change
- explain why the impact is relevant
- ask for confirmation

Do **not** proceed directly in these cases.

---

### For trivial changes
For clearly local and low-risk changes, the agent may proceed directly, but should still briefly state:
- what is being changed
- which file/module is affected
- why the change is safe/local

---

## Layering Constraints

The agent should preserve the following constraints unless explicitly instructed otherwise:

- `app` may call `bsp`
- `app` must **not** call `bdi` directly
- `bsp` may call `bdi`
- `bdi` must not depend on application logic
- hardware-specific details should remain out of `app` when reasonably possible
- third-party managed components should not be edited casually

If a requested change appears to violate these rules, the agent should point it out before implementing it.

---

## Preferred Change Style

The preferred style is:
- minimal diff
- explicit logic
- readable control flow
- clear error handling
- no hidden side effects
- no unnecessary churn in unrelated files

When possible, preserve:
- existing naming conventions
- file organization
- current interfaces, unless change is justified

---

## What the Agent Should Check Before Proposing Changes

Before proposing or implementing a solution, the agent should think through:
- Which layer should own this logic?
- Does this change increase power consumption?
- Does this change make failures more visible or less visible?
- Is the behavior deterministic?
- Does this introduce coupling between unrelated modules?
- Is this appropriate for remote unattended operation?
- Can this fail in a silent way?
- Does this modify public interfaces or expected behavior?
- What files are impacted directly and indirectly?

---

## Output Style Expected from the Agent

For non-trivial tasks, use this format before editing:

### Proposed change
Brief description of the intended modification.

### Why
Why this change is needed.

### Impacted modules
- file/module 1
- file/module 2
- file/module 3

### Risks / trade-offs
- risk/trade-off 1
- risk/trade-off 2

### Confirmation needed
State clearly whether confirmation is required before proceeding.

For trivial tasks, a shorter version is acceptable.

---

## Special Notes for This Project

- This is an embedded, resource-constrained, battery-powered IoT system.
- Low power is a first-class requirement, not an optimization afterthought.
- Remote diagnosability matters a lot.
- Robust behavior is preferred over optimistic behavior.
- Silent failure is strongly discouraged.
- Architecture should remain clean: `app` → `bsp` → `bdi`.

When in doubt, choose the solution that is:
- safer
- more diagnosable
- lower power
- more explicit
- less architecturally invasive

Do not make large speculative changes without approval.

---

## Coding Rules

The agent must follow strict embedded-oriented coding guidelines.

### MISRA C Compliance

- All code should follow **MISRA C principles** as much as reasonably possible.
- Avoid undefined behavior, implicit conversions, and unsafe constructs.
- Prefer explicit types and clear control flow.
- Avoid complex or ambiguous expressions.
- Avoid hidden side effects.

If a rule is intentionally violated, it must be:
- clearly justified
- documented in code comments

---

### Single Return Policy

Each function must have **only one return statement**, placed at the end of the function.

Rules:
- Use an intermediate result variable:
  
  ```c
  res_t res = RES_OK;

---

## Clarification and Interaction Policy

The agent must **never make critical assumptions** when information is missing, ambiguous, or uncertain.

### When to ask questions

The agent must explicitly ask for clarification when:

- requirements are incomplete or unclear
- multiple implementation strategies are possible with different trade-offs
- hardware behavior is not fully defined
- interfaces or expected behavior are ambiguous
- a change could impact architecture, power consumption, or reliability
- assumptions would be required to proceed

In these cases, the agent must:
1. stop before implementing
2. explain what is unclear
3. propose possible interpretations (if helpful)
4. ask for explicit confirmation or additional information

Avoid:
- guessing intended behavior
- inventing requirements
- proceeding with hidden assumptions

---

## Knowledge Capture and AGENTS.md Evolution

This project is expected to evolve over time, and the agent must contribute to maintaining a consistent and updated development standard.

### When new rules or preferences emerge

If the user provides:
- new coding rules
- architectural decisions
- design constraints
- workflow requirements
- project conventions

the agent must:

1. recognize that this is **project-level knowledge**
2. propose an update to `AGENTS.md`
3. show the exact section to be added or modified
4. ask for confirmation before applying the change

---

### After confirmation

Once confirmed, the agent must:
- update `AGENTS.md`
- keep the structure clean and consistent
- avoid duplications
- integrate the new rule in the most appropriate section

---

### Continuous improvement

The agent should proactively suggest improvements to `AGENTS.md` when:

- recurring patterns emerge
- inconsistencies are detected
- new best practices are introduced during development
- repeated clarifications from the user indicate missing rules

---

### Important constraint

The agent must NOT:
- modify `AGENTS.md` silently
- introduce rules that were not explicitly discussed
- change existing rules without justification and confirmation

---

## Communication Style Expectations

When interacting with the user, the agent must:

- be precise and technical
- avoid vague or generic explanations
- clearly state assumptions when they exist
- clearly distinguish between:
  - facts
  - assumptions
  - proposals

When asking questions:
- keep them focused and actionable
- avoid unnecessary verbosity
- group related questions together when possible

---

## Default Behavior in Case of Uncertainty

If uncertainty exists and no clarification is available:

- prefer the safest implementation
- prefer explicit error handling
- prefer lower power impact
- prefer more diagnosable behavior
- document the uncertainty in code comments if implementation proceeds

However, if the impact is non-trivial:
→ the agent must stop and ask instead of proceeding

---

## Documentation Folder (`doc/`)

The `doc/` folder contains all the hardware documentation of the system.

This includes:
- datasheets of all system components
- schematic of the development board (ESP32-S3 DevKitC)
- datasheets of external devices used in the system, such as:
  - BME280 (environmental sensor)
  - Wio E5 mini (LoRa module)
  - MAX17048 (battery fuel gauge)

---

### Usage Guidelines

The agent should treat the `doc/` folder as the **primary source of truth for hardware-related behavior**.

When working on:
- drivers (BSP / BDI)
- hardware configuration
- communication interfaces (UART, I2C, ADC, etc.)
- timing constraints
- electrical limitations
- initialization sequences

the agent should:

1. refer to the relevant datasheet when available
2. avoid making assumptions about hardware behavior
3. align implementation with documented specifications
4. explicitly mention when a datasheet-based assumption is being made

---

### When documentation is missing or unclear

If the required information is not available or is ambiguous:
- the agent must ask the user for clarification
- the agent must NOT guess hardware behavior

---

### Important constraint

- Do not invent register values, timing parameters, or electrical characteristics
- Do not assume default configurations unless explicitly stated in documentation or code
- Prefer conservative and safe configurations when unsure

---

### Suggested Improvements

If the agent detects that:
- a driver is implemented without clear reference to the datasheet
- assumptions are made that could be incorrect
- configuration values are hardcoded without justification

it should:
- highlight the issue
- suggest aligning the implementation with the datasheet
- optionally propose improvements

---

## System Architecture and Hardware Connections

This section describes how the main hardware components are connected in the system.

The agent must use this information to:
- understand system behavior
- avoid incorrect assumptions
- guide implementation decisions
- reason about power, initialization, and fault handling

---

### Power Architecture

#### Components

- Solar panel: 5W, 6V
- Solar charger: BQ24074 (Adafruit module, datasheet available in `doc/`)
- Battery: Li-ion 3.7V, 3700mAh
- DC-DC boost converter: 5V output
- Main MCU: ESP32-S3 (DevKitC)
- Other peripherals powered from 5V rail

#### Power Chain
Solar Panel (6V) --> BQ24074 Solar Charger --> Battery (3.7V Li-ion) --> DC-DC Boost Converter (5V) --> ESP32-S3 + ohter components


---

### Power Design Implications

The agent must consider:

- The system is **battery-powered with solar charging**
- Energy is limited → low power is critical
- The DC-DC boost introduces efficiency losses
- The ESP32 and peripherals are powered from the **boosted 5V rail**
- The battery is monitored via MAX17048

---

### Power-Related Guidelines

When implementing or modifying code:

- minimize active time of peripherals (LoRa, sensors, UART)
- avoid unnecessary retries that consume energy
- ensure proper shutdown/deinitialization before sleep
- avoid keeping peripherals powered longer than needed
- consider that voltage may vary depending on battery state and charging

If a change may impact power consumption, the agent must explicitly highlight it.

---

### Communication Interfaces

#### UART

- Interface: ESP32-S3 ↔ Wio E5 Mini (LoRa module)
- Usage: AT command communication for LoRa transmission

Guidelines:
- communication must be robust (timeouts, retries, validation)
- failures must be detectable and not silent
- avoid blocking indefinitely
- minimize UART active time for power efficiency

---

#### I2C

- Interface: ESP32-S3 ↔ sensors
- Connected devices:
  - BME280 (temperature, humidity, pressure)
  - MAX17048 (battery fuel gauge)

Guidelines:
- handle communication errors explicitly
- validate sensor data
- detect invalid or out-of-range values
- avoid unnecessary reads
- consider sensor initialization and power-up timing

---

### Architectural Constraints

- All hardware access must go through:
  - `bsp/` for high-level usage
  - `bdi/` for low-level drivers

- The application (`app`) must not:
  - access hardware directly
  - bypass abstraction layers

---

### Important Notes

- The LoRa module is external and controlled via UART → not directly integrated into ESP32 radio stack
- The system relies on correct sequencing:
  - sensor read
  - data processing
  - LoRa transmission
  - shutdown / deep sleep

- Any change affecting this flow must be treated as **non-trivial**

---

### When Information Is Uncertain

If the agent needs:
- electrical details
- timing constraints
- configuration values

it should:
- check the `doc/` folder
- ask the user if unclear
- avoid guessing hardware behavior

---