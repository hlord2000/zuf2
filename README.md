# zuf2

`zuf2` is a standalone UF2 USB MSC bootloader for Zephyr/NCS targets.

## Layout

The nRF54LM20 overlays in `boards/` use CPUAPP RRAM like this:

- `0x00000000..0x0003ffff`: `boot_partition`, `zuf2`
- `0x00040000..0x001f3fff`: `slot0_partition`, chain-loaded app
- `0x001f4000..0x001fcfff`: `storage_partition`

Applications that boot through `zuf2` must be built with the same partition
layout and `CONFIG_USE_DT_CODE_PARTITION=y`.

## Build

From a Zephyr/NCS workspace with this repository as an application:

```sh
west build --no-sysbuild -p always \
  -b <board-target> \
  -d build/zuf2 \
  path/to/zuf2
```

For the included nRF54LM20 DK overlay:

```sh
west build --no-sysbuild -p always \
  -b nrf54lm20dk/nrf54lm20a/cpuapp \
  -d build/zuf2 \
  path/to/zuf2
```

Build the sample chain-loaded app the same way:

```sh
west build --no-sysbuild -p always \
  -b nrf54lm20dk/nrf54lm20a/cpuapp \
  -d build/zuf2_hello \
  path/to/zuf2/samples/hello
```

Convert the sample app HEX to UF2:

```sh
uv run path/to/zuf2/scripts/zuf2conv.py \
  build/zuf2_hello/zephyr/zephyr.hex \
  -f 0x016f65e4 \
  --base 0x40000 \
  --max-size 0x1b4000 \
  -o build/zuf2_hello/zephyr/zuf2_hello.uf2
```

## Flash And Use

Flash the bootloader normally:

```sh
west flash -d build/zuf2
```

The board enumerates as a USB MSC drive named `ZUF2BOOT` when no valid app is
present, when `sw0` is held during reset on boards that define that alias, or
when reset is tapped twice within the double-tap window. Copy a UF2 file to the
drive. After all UF2 blocks are received, `zuf2` resets and boots the
application.

By default, `CONFIG_ZUF2_DOUBLE_TAP_RESET=y` and the double-tap window is
`CONFIG_ZUF2_DOUBLE_TAP_DELAY_MS=500`. On a board with a reset button, tap reset
twice quickly to stay in UF2 mode. Nordic overlays can enable `gpregret1` for
the retained double-tap marker; boards without a retained-memory device fall
back to no-init RAM. If only one reset is seen, `zuf2` waits for the window to
expire and then boots the application.

The second reset must happen after the bootloader has started, so use a quick
double-click gesture with a short beat between presses rather than holding reset
or pressing both taps as fast as possible.

To program an app, copy the generated UF2 file to the mounted `ZUF2BOOT` drive.

## Porting

For another board, provide a devicetree overlay with:

- `boot_partition` as `/chosen/zephyr,code-partition` for the bootloader build
- `slot0_partition` for the application
- the board USB device controller exposed as `zephyr_udc0`
- optional `sw0` alias for forced bootloader entry

Change `CONFIG_ZUF2_FAMILY_ID`, `CONFIG_ZUF2_MODEL`, and `CONFIG_ZUF2_BOARD_ID`
for a real board/product.
