#ifndef CENTIPEDE_FIRMWARE_PIO_SPOONFEED_H_
#define CENTIPEDE_FIRMWARE_PIO_SPOONFEED_H_

struct Spoonfeeder {
    static constexpr uint MAX = 32;
    static constexpr uint POST = 20;
    byte inputs[MAX];
    byte log_rw[MAX];
    byte log_dbus[MAX];
    uint log_abus[MAX];
    uint in;
    uint out;

    void Run(const char* control) {
        in = out = 0;
        memset(log_dbus, 0, sizeof log_dbus);
        memset((char*)log_abus, 0, sizeof log_abus);
        memset((char*)log_rw, 0, sizeof log_rw);

        // Claim ownership of the signals we need.
        for (uint i = 0; i < 8; i++) {
            INPUT(i);
        }
        INPUT(G_RW);
        INPUT(G_E);
        INPUT(G_Q);

        // Get in sync with the already-halting CPU
        this->sync();
        // Unhalt and continue sync
        gpio_set_dir(G_HALT, 0);  // Let HALT float (un-halts)
        this->sync();
        //this->sync();
        //this->sync();

        // Now execute the control program
        while (*control) {
            switch (*control) {
                case 'f': // feed
                    this->feed();
                break;
                case 'v': // view
                    this->view();
                break;
                case '|': // re-halt
                    gpio_set_dir(G_HALT, 1); // output HALT (re-halts)
                break;
                default:
                break;
            }
            ++control;
        }
        for (uint p = 0; p < POST; p++) {
            this->sync();
        }
    }

    // RW is 20 on 32z
    static constexpr uint INVERTED = 0x100;
    static constexpr uint E = INVERTED | G_E; // 21 on 32z
    static constexpr uint Q = INVERTED | G_Q; // 22 on 32z

    void await(uint pin, bool awaited) {
        bool value;
        if (pin & INVERTED) {
            pin &= ~INVERTED;
            awaited = !awaited;
        }
        do {
            value = gpio_get(pin);
        } while (value != awaited);
    }

    void view() {
        await(E, 0); // start phase 1
        await(Q, 1); // start phase 2
        await(E, 1); // start phase 3
        await(Q, 0); // start phase 4

        const uint abus = volatile_sio_hw->gpio_hi_in & 0xFFFF;
        const uint signals = volatile_sio_hw->gpio_in;
        const byte dbus = (byte)signals;
        const bool rw = 0 != (signals & (1u << G_RW));

        log_abus[out] = abus;
        log_dbus[out] = dbus;
        log_rw[out] = rw;
        ++out;
    }

    void feed() {
        await(E, 0); // start phase 1
        await(Q, 1); // start phase 2

        gpio_set_dir_masked(/*mask=*/0xFF, /*direction=*/ 0xFF);  // data bus OUTPUT
        gpio_put_masked(/*mask=*/0xFF, /*put=*/inputs[in]);
        ++in;

        await(E, 1); // start phase 3
        await(Q, 0); // start phase 4
        await(E, 0); // start phase 1
        gpio_put_masked(/*mask=*/0xFF, /*put=*/0xFF);  // reset outputs
        gpio_set_dir_masked(/*mask=*/0xFF, /*direction=*/ 0x00);  // data bus INPUT
    }

    void sync() {
        await(E, 0); // start phase 1
        await(Q, 1); // start phase 2
        await(E, 1); // start phase 3
        await(Q, 0); // start phase 4
    }

    void PrintLog() {
        printf("\nSpoonfeeder: in=%d. out=%d.\n", in, out);
        for (int i = 0; i < MAX; i++) {
            printf("[% 2d.] r=%d a=%04x d=%02x\n", i, log_rw[i], log_abus[i], log_dbus[i]);
        }
    }
};

Spoonfeeder spoonfeeder;
/*
byte Spoonfeeder::inputs[MAX];
byte Spoonfeeder::log_rw[MAX];
byte Spoonfeeder::log_dbus[MAX];
uint Spoonfeeder::log_abus[MAX];
uint Spoonfeeder::in;
uint Spoonfeeder::out;
*/

void JustSpy() {
    spoonfeeder.Run("vvvvvvvvvv|vvvvvvvvvvvvvvv");
    spoonfeeder.PrintLog();
}
void SendSWI() {
    spoonfeeder.inputs[0] = 0x3F; // SWI
    spoonfeeder.inputs[1] = 0x3F; // SWI
    spoonfeeder.inputs[2] = 0x3F; // SWI
    spoonfeeder.inputs[3] = 0x3F; // SWI
    spoonfeeder.Run("ffff|vvvvvvvvvvvvvvvvvvvv");
    spoonfeeder.PrintLog();
}
void DisableInterrupts() {
    spoonfeeder.inputs[0] = 0x1A; // ORCC
    spoonfeeder.inputs[1] = 0x50; // F & I
    spoonfeeder.Run("f|fvvvvvv");
    spoonfeeder.PrintLog();
}

#endif // CENTIPEDE_FIRMWARE_PIO_SPOONFEED_H_
