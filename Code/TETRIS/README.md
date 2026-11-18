# Two-player CAN Tetris

Two players take turns controlling the pieces on a shared Tetris board (10 × 20). Player 1 uses the
joystick on a Teensy 3.6, and player 2 tilts a Raspberry Pi 3 with an accelerometer. When a piece
lands, the turn passes to the other player. The fall speed increases with every cleared line, and
the game is over when a new piece does not fit.

## Architecture

The nodes communicate over CAN at 250 kbit/s.

- **Teensy 3.6** (SK Pang Dual CAN board) reads the joystick every 10 ms and sends the raw button state
  to the Pi. It draws the board and the active piece that the Pi sends back on the OLED display. The
  Teensy has no game logic.
- **Raspberry Pi 3** (PiCAN GPS + Gyro + Accelerometer) owns all game logic and the game clock.
  - It runs edge detection on the button state, so each press gives exactly one event.
  - It reads the MPU-6050 accelerometer over I2C and turns tilting into input using hysteresis.
  - It sends the game state back over CAN and shows the game in the terminal over SSH.

Sensor data therefore travels from the Teensy over CAN to the Pi, is processed there, and returns to
the Teensy as processed data. With all logic on one node there is only one version of the game state,
so nothing has to be synchronised.

The Pi code is split into modules:

- `TetrisGame` is pure game logic with no IO.
- `protocol.h` packs the CAN frames.
- `CanBus` wraps SocketCAN.
- `Imu` reads the MPU-6050.
- `render()` draws the game in the terminal.
- `main.cpp` holds the main loop. The loop never blocks: it waits with `poll()`, and every timed task
  uses `std::chrono::steady_clock`.

## CAN protocol

| ID | From → to | Len | Content | Sent |
|---|---|---|---|---|
| `0x100` | Teensy → Pi | 1 | Buttons (1 = pressed): bit0 left, bit1 right, bit2 up, bit3 down, bit4 click | on change + every 100 ms |
| `0x110` | Pi → all | 7 | ax, ay, az (int16, milli-g, big-endian). Byte 6 holds the tilt flags: bit0 left, bit1 right, bit2 away, bit3 towards | every 100 ms |
| `0x200`–`0x203` | Pi → Teensy | 8 | Byte 0: sequence number. Bytes 1–7: five rows of 10 bits (part k = rows 5k…5k+4) | on change + every 500 ms |
| `0x210` | Pi → Teensy | 8 | Active piece as a 4×4 mask (2 bytes), x, y, flags (turn, game over, last player), fall interval / 10 ms, score (uint16) | on change + every 500 ms |

- **Priority.** The lowest ID wins arbitration, so the joystick frame has the lowest ID and never waits
  for the Pi's frames.
- **Reliability.** The Teensy only switches to a new board once all four parts carry the same sequence
  number. Frames that arrive out of order or go missing therefore never produce a half-updated board.
- **Periodic resend.** Everything is resent every 500 ms, so a Teensy that has missed something is back
  in sync within half a second.
- **Bus load.** About 7.5 kbit/s in the worst case, roughly 3 % of 250 kbit/s.

## Controls

| | Player 1 (joystick) | Player 2 (tilt) |
|---|---|---|
| Move one cell | left / right | tilt left / right (once per tilt) |
| Rotate | up or click | tilt away from you |
| Soft drop | hold down | keep the board tilted towards you |

After game over, a rotate starts a new round.

The accelerometer measures gravity, so a tilt angle θ gives x = sin θ. At 0.35 g (≈ 20°) the board
counts as tilted, and below 0.15 g (≈ 9°) it is neutral again. The two thresholds give hysteresis.
Low-pass filtering happens inside the MPU-6050 (DLPF ≈ 5 Hz).

## Build and run

**Pi.** Put the files in `pi/` into the Buildroot project's `mas245/can_pingpong/` and build with
`make can_pingpong-rebuild`. I2C must be enabled (`dtparam=i2c_arm=on` in `config.txt`, and
`modprobe i2c-bcm2835 i2c-dev`). Then start:

```bash
ip link set can0 up type can bitrate 250000
```
```bash
can_pingpong
```

`can_pingpong --calibrate` prints the accelerometer values continuously, so the thresholds and signs in
`imu.h` can be tuned.

**Teensy.** Upload `teensy_tetris/teensy_tetris.ino` with Teensyduino. The sketch needs `FlexCAN_T4` and
`Adafruit_SSD1306`.

## Testing

- **Game logic and CAN packing** are tested without hardware:
  ```bash
  g++ -std=c++17 test_tetris.cpp -o test_tetris && ./test_tetris
  ```
- **The Pi** can be tested without the Teensy by setting `loopback on` on `can0`. Sending
  `cansend can0 100#01` followed by `100#00` simulates a press on left.
- **The Teensy** shows a test pattern until it receives a board. A known board can be sent with
  `cansend`:
  ```bash
  cansend can0 200#0101084000021000
  cansend can0 201#0120000108400002
  cansend can0 202#0101084000021000
  cansend can0 203#0120000108400002
  cansend can0 210#4E000308005004D2
  ```
  This should show two diagonal lines and a T piece.
