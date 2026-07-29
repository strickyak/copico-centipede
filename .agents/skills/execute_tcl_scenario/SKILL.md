---
name: Execute Tcl Scenario
description: Execute some Tcl code on the Centipede firmware without a Coco connected to it.
---

When asked to "execute some Tcl code on the Centipede firmware without a Coco connected to it", follow these steps:

1. Write your Tcl code as a fragment of an "expect script" (see `v1/firmware/tests/*.tcl` for examples).
2. Save this code in a file, for example `v1/firmware/tests/f.tcl`.
3. Execute the scenario using the following command:

```bash
cd v1/firmware && ./scenario tests/f.tcl
```

Note: It may take up to 20 seconds to run. Allow it sufficient time to complete.
