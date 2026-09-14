# Physical adapter contract tests

Runs the sibling Composer adapter suite on ESP32/ESP-IDF 6.0.2 with real RMT
channels on GPIO4 and GPIO5. Leave those pins unconnected during this test.
It does not use GPIO16/17 or access NVS. A separate image keeps driver tests
independent of the scene suite's mocked storage and static fixtures.

Build here with `idf.py -D IDF_TARGET=esp32 build`. Bench flashing is arranged
with Rodrigo; after running the test restore the root application using its
own custom partition table. Do not erase flash/NVS. Success is the recurring
`ADAPTER_TEST ... RESULT=ALL_PASS` line, not a successful compilation.

Executed 2026-09-14 on WROOM/COM6: **172 checks, zero failures**, GPIO4/5
disconnected. The root application was restored afterward. Capture:
`build-wroom-rmt-safe/bench-outputs-20260914/adapter-tests.log` in the root.
