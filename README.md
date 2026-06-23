# zuf2

`zuf2` is a standalone UF2 USB MSC bootloader for Zephyr/NCS targets. It is not
MCUboot and does not enable MCUboot sysbuild images.

The current board bring-up target is the nRF54LM20 DK:

- DK: PCA10184
- Primary target used here: `nrf54lm20dk/nrf54lm20a/cpuapp`
- UF2 family ID: `0x016f65e4`

## Layout

The nRF54LM20 overlays in `boards/` use CPUAPP RRAM like this:

- `0x00000000..0x0003ffff`: `boot_partition`, `zuf2`
- `0x00040000..0x001f3fff`: `slot0_partition`, chain-loaded app
- `0x001f4000..0x001fcfff`: `storage_partition`

Applications that boot through `zuf2` must be built with the same partition
layout and `CONFIG_USE_DT_CODE_PARTITION=y`.

## Build

From `/opt/ncs/sdks/ncs-main`:

```sh
source activate-nrf.sh
west build --no-sysbuild -p always -b nrf54lm20dk/nrf54lm20a/cpuapp -d zuf2/build zuf2
```

Build the sample chain-loaded app:

```sh
source activate-nrf.sh
west build --no-sysbuild -p always -b nrf54lm20dk/nrf54lm20a/cpuapp -d zuf2/samples/hello/build zuf2/samples/hello
```

Convert the sample app HEX to UF2:

```sh
uv run zuf2/scripts/zuf2conv.py \
  zuf2/samples/hello/build/zephyr/zephyr.hex \
  -f 0x016f65e4 \
  --base 0x40000 \
  --max-size 0x1b4000 \
  -o zuf2/samples/hello/build/zephyr/zuf2_hello.uf2
```

## Flash And Use

Flash the bootloader normally:

```sh
source activate-nrf.sh
west flash -d zuf2/build --dev-id 1051851773
```

When no valid app is present, or when `sw0` is held during reset, the board
enumerates as a USB MSC drive named `ZUF2BOOT`. Copy a UF2 file to the drive.
After all UF2 blocks are received, `zuf2` resets and boots the application.

## Verified On PCA10184

On debugger serial `1051851773`, the bootloader enumerated as USB MSC
`1209:5a02` with volume label `ZUF2BOOT`. Copying the generated
`zuf2_hello.uf2` programmed the sample app at `0x00040000`; after reset, serial
logs showed `zuf2_hello: zuf2 chain-loaded hello app`.

## Porting

For another board, provide a devicetree overlay with:

- `boot_partition` as `/chosen/zephyr,code-partition` for the bootloader build
- `slot0_partition` for the application
- the board USB device controller exposed as `zephyr_udc0`
- optional `sw0` alias for forced bootloader entry

Change `CONFIG_ZUF2_FAMILY_ID`, `CONFIG_ZUF2_MODEL`, and `CONFIG_ZUF2_BOARD_ID`
for a real board/product.
