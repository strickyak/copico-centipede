---
name: Reflash Centipede
description: Re-flash the Centipede board with new Firmware.
---

When asked to "Re-flash the Centipede board with new Firmware", execute the following command:

```bash
cd v1/firmware && ./reflash.expect build/centipede.uf2
```

Note: This restarts the board in BOOT_SEL mode, and re-installs the firmware on the board; then the board restarts itself. It takes up to 20 seconds to run. Allow it sufficient time to complete.
