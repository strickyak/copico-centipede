## 3 Jul 2026

Discovered that Centipede 32Z "Z44" cannot run the OPIL demo if clocked
at the default 150 MHz.  Works very reliably at 250 MHz.

```
    #define DBUS_HOLD_CYCLES 0

 66 #elif CENTIPEDE_REV == 3226  // 32z
 67
 68 #define G_CTS 8
 69 #define G_SCS 9
 70
 71 #define G_LED 25
 72 #define G_SND 26
 73 #define G_CART 27
 74 #define G_SLENB 28
 75 #define G_HALT 29
 76 #define G_NMI 30
 77 #define G_RESET 31
```

Legacy centipede firmware uses FIFO_WRITE to push to Fifo and a 4-byte
packet starting C_RAM2_WRITE==195:

```
711 #if FIFO_WRITE
712           PUSH(FIFO_WRITE | (abus << 8) | dbus);
713 #endif

543         case FIFO_WRITE >> 24:  // write cycle      
544           putchar_raw(C_RAM2_WRITE);
545           putchar_raw(x >> 16);
546           putchar_raw(x >> 8);
547           putchar_raw(x);
548           break;
```

Using our 1024-slot Fifo:

```
100 #include "cross-core.h"
101 #include "flash-label.h"
102
103 CrossCoreFIFO<uint, 1024> ccfifo;
104
105 FORCE_INLINE uint ccfifo_pop_blocking() {
106   uint z = 0;
107   while (1) {
108     bool ok = ccfifo.pop(z);
109     if (ok) return z;
110   }
111 }
112
113 // #define PUSH force_inline_multicore_fifo_push_blocking
114 // #define POP  multicore_fifo_pop_blocking
115 #define PUSH ccfifo.push
116 #define POP ccfifo_pop_blocking
```

tether thinks char 31 means Reboot Pico (in RunSelect).


