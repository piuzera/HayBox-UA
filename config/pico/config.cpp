#include "comms/backend_init.hpp"
#include "config_defaults.hpp"
#include "core/CommunicationBackend.hpp"
#include "core/KeyboardMode.hpp"
#include "core/Persistence.hpp"
#include "core/mode_selection.hpp"
#include "core/pinout.hpp"
#include "core/state.hpp"
#include "input/DebouncedGpioButtonInput.hpp"
#include "input/NunchukInput.hpp"
#include "reboot.hpp"
#include "stdlib.hpp"

#include <config.pb.h>

Config config = default_config;

GpioButtonMapping button_mappings[] = {
    // LEFT HAND
    { BTN_LF4, 0  },  // L Shield
    { BTN_LF3, 1  },  // Left
    { BTN_RF4, 2  },  // Up
    { BTN_LF2, 3  },  // Down
    { BTN_LF1, 4  },  // Right

    // CENTER
    { BTN_MB1, 5  },  // Start

    // LEFT THUMB
    { BTN_LT1, 6  },  // MX
    { BTN_LT2, 7  },  // MY

    // RIGHT THUMB
    { BTN_RT2, 8  },  // C-Down
    { BTN_RT3, 9  },  // C-Left
    { BTN_RF1, 10 },  // B
    { BTN_RT4, 11 },  // C-Up
    { BTN_RT5, 12 },  // C-Right
    { BTN_RT1, 13 },  // A

    // RIGHT HAND
    { BTN_RF5, 14 },  // R Shield
    { BTN_RF2, 15 },  // X
    { BTN_RF3, 16 },  // Z
    { BTN_MB4, 17 },  // D-Pad Toggle
    { BTN_RF6, 18 },  // Y
    { BTN_RF8, 19 },  // Mid Shield
    { BTN_RF7, 20 },  // Light Shield
};


const size_t button_count = sizeof(button_mappings) / sizeof(GpioButtonMapping);

DebouncedGpioButtonInput<button_count> gpio_input(button_mappings);

const Pinout pinout = {
    .joybus_data = 28,
    .nes_data = -1,
    .nes_clock = -1,
    .nes_latch = -1,
    .mux = -1,
    .nunchuk_detect = -1,
    .nunchuk_sda = -1,
    .nunchuk_scl = -1,
};

CommunicationBackend **backends = nullptr;
size_t backend_count;
KeyboardMode *current_kb_mode = nullptr;

void setup() {
    static InputState inputs;

    // Create GPIO input source and use it to read button states for checking button holds.
    gpio_input.UpdateInputs(inputs);

    // Check bootsel button hold as early as possible for safety.
    if (inputs.rt2) {
        reboot_bootloader();
    }

    // Turn on LED to indicate firmware booted.
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    gpio_put(PICO_DEFAULT_LED_PIN, 1);

    // Attempt to load config, or write default config to flash if failed to load config.
    if (!persistence.LoadConfig(config)) {
        persistence.SaveConfig(config);
    }

    // Create array of input sources to be used.
    static InputSource *input_sources[] = {};
    size_t input_source_count = sizeof(input_sources) / sizeof(InputSource *);

    backend_count =
        initialize_backends(backends, inputs, input_sources, input_source_count, config, pinout);

    setup_mode_activation_bindings(config.game_mode_configs, config.game_mode_configs_count);
}

void loop() {
    select_mode(backends, backend_count, config);

    for (size_t i = 0; i < backend_count; i++) {
        backends[i]->SendReport();
    }

    if (current_kb_mode != nullptr) {
        current_kb_mode->SendReport(backends[0]->GetInputs());
    }
}

/* Button inputs are read from the second core */

void setup1() {
    while (backends == nullptr) {
        tight_loop_contents();
    }
}


void loop1() {
    if (backends != nullptr) {
        auto &inputs = backends[0]->GetInputs();

        // Read all physical buttons (including GPIO17)
        gpio_input.UpdateInputs(inputs);

        // Persistent toggle state
        static bool last_pressed = false;
        static bool dpad_enabled = false;

        // GPIO17 is mapped to BTN_MB4
        const bool pressed = inputs.mb4;

        // Toggle only on a new button press
        if (pressed && !last_pressed) {
            dpad_enabled = !dpad_enabled;
        }

        last_pressed = pressed;

        // Activate Melee's existing D-pad layer
        inputs.nunchuk_c = dpad_enabled;
    }
}

