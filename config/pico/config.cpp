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
    { BTN_LF1, 2  }, // GP2: RIGHT
    { BTN_LF2, 3  }, // GP3: DOWN
    { BTN_LF3, 4  }, // GP4: LEFT
    { BTN_LF4, 5  }, // GP5: L
    { BTN_LF5, 1  }, // GP1: START - UNUSED

    { BTN_LT1, 6  }, // GP6: MOD_X
    { BTN_LT2, 7  }, // GP7: MOD_Y

    { BTN_MB1, 0  }, // GP0: START
    { BTN_MB2, 10 }, // GP10: C_UP - UNUSED
    { BTN_MB3, 11 }, // GP11: C_LEFT - UNUSED

    { BTN_RT1, 14 }, // GP14: A
    { BTN_RT2, 15 }, // GP15: C_DOWN
    { BTN_RT3, 13 }, // GP13: C_LEFT
    { BTN_RT4, 12 }, // GP12: C_UP
    { BTN_RT5, 16 }, // GP16: C_RIGHT

    { BTN_RF1, 26 }, // GP26: B
    { BTN_RF2, 21 }, // GP21: X
    { BTN_RF3, 19 }, // GP19: Z
    { BTN_RF4, 17 }, // GP17: UP

    { BTN_RF5, 27 }, // GP27: R
    { BTN_RF6, 22 }, // GP22: Y
    { BTN_RF7, 20 }, // GP20: LS
    { BTN_RF8, 18 }, // GP18: MS
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
        gpio_input.UpdateInputs(backends[0]->GetInputs());
    }
}
