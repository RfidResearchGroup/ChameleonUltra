# How to use the Firmware

In this file you can look up how to [install requirements](#Prerequisites-for-compiling), [edit](#Editing-the-code), [compile](#Compiling-the-code) and [debug](#Debugging-the-code) the code!

## Prerequisites for compiling

### install a cross-compiler

So far, the following compilers have been reported to work fine.  
Download one of them and decompress it.  
Remember the path where you installed it.

- [gcc-arm-none-eabi-10.3-2021.10](https://developer.arm.com/downloads/-/gnu-rm)
- [arm-gnu-toolchain-12.2.rel1-XXX-arm-none-eabi](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads), e.g. [arm-gnu-toolchain-12.2.rel1-x86_64-arm-none-eabi.tar.xz](https://developer.arm.com/-/media/Files/downloads/gnu/12.2.rel1/binrel/arm-gnu-toolchain-12.2.rel1-x86_64-arm-none-eabi.tar.xz) for a x86_64 Linux host

Always use the official versions from ARM, *DO NOT* install `gcc-arm-none-eabi` from Debian/Ubuntu.  
For some unknown reasons, same gcc version from Debian creates a bootloader too large to fit in the allocated flash space.  
Moreover it does not contain the `gdb` debugger.

### install make

* **Debian/Ubuntu alike**
  * Open a terminal.
  * Run the following command to install Make: `sudo apt-get install build-essential`
* **Windows using Chocolatey:**
  * Open a PowerShell terminal with administrator privileges.
  * If not yet installed, run the following command to install Chocolatey:
  ``` Set-ExecutionPolicy Bypass -Scope Process -Force; [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072; iex ((New-Object System.Net.WebClient).DownloadString('https://chocolatey.org/install.ps1')) ```
  * In the same PowerShell terminal, run the following command to install Make using Chocolatey: `choco install make`
* **macOS:**
  * Open a terminal.
  * If not yet installed, install Homebrew package manager by running the following command: `/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"`
  * Once Homebrew is installed, run the following command to install Make: `brew install make`

### install nRF tools

- Install nRF Util tool [nrfutil](https://www.nordicsemi.com/Products/Development-tools/nrf-util)
    - Move it to a known path like `C:\nrfutil\` or `/usr/local/bin/`
    - Add this path to the `PATH` Environment Variable if not yet there.
- Install nRF Util packages:
    - `nrfutil install completion device nrf5sdk-tools trace`
- Install [nRF Command Line Tools](https://www.nordicsemi.com/Products/Development-tools/nrf-command-line-tools/download) to get `nrfjprog`, `mergehex` etc.

### install programmer tools

Depending on the hardware programmer you want to use, additional tools are needed.

- If you are using a J-Link:
    - Install [Segger J-Link Software](https://www.segger.com/downloads/jlink)
    - alternatively, you can use openocd as described below
    - Note: a JLink OB (or a STLink reflashed as a JLink OB) will not work on a nRF.

- If you are using a ST-Link V2:
    - Install [openocd](https://openocd.org/pages/getting-openocd.html)
    - If under Windows, install [ST-Link drivers](https://www.st.com/en/development-tools/stsw-link009.html), extract the zip and run `dpinst_amd64.exe`


### configure the project

  - Edit `Makefile.defs`:
    - Change `GNU_INSTALL_ROOT` (path of previously installed Compiler `bin` folder)
    - Change `GNU_VERSION` (Version of the installed Compiler) (FIXME: is it really used?)
    - Change the other paths to match your system if needed
    - Don't forget to remove the `#` in front of the changed lines
  - Alternatively, if you are committing often code, it may be easier to leave `Makefile.defs` intact and to invoke `make` with the desired variables from a script, e.g. `make GNU_INSTALL_ROOT=../../../arm-gnu-toolchain-12.2.rel1-x86_64-arm-none-eabi/bin/`

## Editing the code

You can use [Visual Studio Code](https://code.visualstudio.com/download) to edit this project! Simply download and
install it!

- Install the [C++ Extension](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cpptools) in VS-Code.
- Install
  the [C++ Extension Pack](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cpptools-extension-pack) in
  VS-Code.
- Create a new IntelliSense Configuration:
    - press F1 in VS-Code and enter `C/C++: Edit Configurations (UI)`
    - Add a new Configuration and name it
    - Specify your Compiler path (path of previously installed Compiler `bin` folder)
    - Change IntelliSense mode to `gcc-arm (legacy)`
    - Add include path `${workspaceFolder}/**`

## Compiling the code

- Install prerequisites (for instructions have a look at [Prerequisites for compiling](#Prerequisites-for-compiling))
- Run `build.sh` or try to execute its steps manually if your platform is not yet properly supported. Feedback is always welcome.

The script produces several images in `objects`.
* `fullimage.hex` to be used with a programmer over the SWD pins
* `dfu-app.zip` and `dfu-full.zip` to be used with DFU mode

## Uploading the code in DFU mode

If the bootloader and the SoftDevice are already properly installed on the Chameleon, you can reflash it directly over DFU.

To set the device in DFU mode:
* you can use the Python client and issue the command `hw dfu`
* you can use the script `resource/tools/enter_dfu.py` that does exactly the same but may be easier to call from your scripts
* you can unplug the device, wait for it to sleep, then press the button B and plug it. If the application is bogus, this is the only way.

The LEDs 4 & 5 should blink green when in DFU mode.

To flash only the application (safer):

`nrfutil device program --firmware objects/dfu-app.zip --traits nordicDfu`

To flash everything (be sure to also have a JLink or ST-Link V2 programmer if something goes wrong):

`nrfutil device program --firmware objects/dfu-full.zip --traits nordicDfu`

Under Linux you can use the scripts `flash-dfu-app.sh` and `flash-dfu-full.sh`, they will put the device in DFU mode and flash it.

## Uploading the code with a programmer

Connect pins GND, SWC (swclk) and SWD (swdio) to your programmer.

With a JLink and `nrfjprog`
```
# application only:
nrfjprog -f nrf52 --program objects/application.hex --sectorerase --verify --reset
# full:
nrfjprog -f nrf52 --program objects/fullimage.hex --sectorerase --verify --reset
```

With a JLink and `openocd`
```
# application only:
openocd -f interface/jlink.cfg -f target/nrf52.cfg -c "program objects/application.hex verify reset ; shutdown"
# full:
openocd -f interface/jlink.cfg -f target/nrf52.cfg -c "program objects/fullimage.hex verify reset ; shutdown"
```

With a ST-Link V2 and `openocd`

```
# application only:
openocd -f interface/stlink.cfg -f target/nrf52.cfg -c "program objects/application.hex verify reset ; shutdown"
# full:
openocd -f interface/stlink.cfg -f target/nrf52.cfg -c "program objects/fullimage.hex verify reset ; shutdown"
```

## Uploading the code over BLE

If you are adventurous it is possible to flash the device over BLE (DFU mode).

To put the device in DFU mode
* you can use the Python client and issue the command `hw dfu` **TODO:** this will be possible only when the client will be able to work over BLE...
* you can use the script `resource/tools/enter_dfu_over_ble.py`

Once in DFU mode, the device will announce itself over BLE as `CU-xxxx` where xxxx are the last 2 bytes of the Device Serial Number.

Then use the official [nRF Device Firmware Update](https://www.nordicsemi.com/Products/Development-tools/nRF-Device-Firmware-Update) mobile application to flash one of the DFU images.

## Debugging the code from VSCode

- Install [Cortex-Debug](https://marketplace.visualstudio.com/items?itemName=marus25.cortex-debug) VS-Code Extension
- Open `app_main.c`
- Open the extension with `CTRL-SHIFT-D`
- Klick on `create a launch.json file`
- Select `Cortex-Debug`
- Add this in the configuration bracket:

```
{
    "cwd": "${workspaceFolder}",
    "executable": "${workspaceRoot}/firmware/objects/bootloader.out",
    "name": "Debug with JLink",
    "request": "launch",
    "type": "cortex-debug",
    "runToEntryPoint": "main",
    "showDevDebugOutput": "none",
    "servertype": "jlink",
    "device": "nrf52",
    "interface": "swd",
    "svdFile": "${workspaceRoot}/firmware/nrf52_sdk/modules/nrfx/mdk/nrf52.svd",
}, 
{
    "cwd": "${workspaceFolder}",
    "executable": "${workspaceRoot}/firmware/objects/bootloader.out",
    "name": "Debug with STLink",
    "request": "launch",
    "type": "cortex-debug",
    "runToEntryPoint": "main",
    "showDevDebugOutput": "none",
    "servertype": "openocd",
    "device": "nrf52",
    "svdFile": "${workspaceRoot}/firmware/nrf52_sdk/modules/nrfx/mdk/nrf52.svd",
    "gdbPath": "C:/Program Files (x86)/GNU Arm Embedded Toolchain/10 2021.10/bin/arm-none-eabi-gdb.exe",
    "configFiles": [
        "interface/stlink.cfg",
        "target/nrf52.cfg"
    ]
}
```

- If you are jlink probe, create `settings.json` in {projectRoot}/.vscode directory.

```
{
    "cortex-debug.armToolchainPath": "C:\\UserProgram\\arm_gcc\\none\\bin",
    "cortex-debug.JLinkGDBServerPath": "C:\\Program Files\\SEGGER\\JLink\\JLinkGDBServerCL.exe",
}
```

- To change `executable` target in `launch.json` to `application` or `bootloader`
- In the debug menu you can select `Debug with JLink` or `Debug with STLink`

## Debugging the code with gdb and openocd

See first if you can execute `arm-none-eabi-gdb` from the installed tools.
* gcc-arm-none-eabi-10.3-2021.10 gdb requires `libncurses5`
* arm-gnu-toolchain-12.2.rel1 gdb requires Python 3.8

In case Python 3.8 is not available anymore on your distro, to install a local copy you can do
```
wget https://www.python.org/ftp/python/3.8.17/Python-3.8.17.tgz
tar zxvf Python-3.8.17.tgz
cd Python-3.8.17
./configure --prefix=$HOME/opt/python-3.8.17 --enable-shared
make
rm -rf ~/opt/python-3.8.17
make install
```

Connect openocd to the device with a JLink or a ST-Link V2
```
openocd -f interface/jlink.cfg -f target/nrf52.cfg
```
```
openocd -f interface/stlink.cfg -f target/nrf52.cfg
```
Then run gdb as follows

```
PYTHONHOME=~/opt/python-3.8.17/ arm-gnu-toolchain-12.2.rel1-x86_64-arm-none-eabi/bin/arm-none-eabi-gdb
```
and tell gdb to connect to openocd
```
target extended-remote localhost:3333
```

## BlackMagicProbe with RTT support, out of a ST-Link V2
You can reflash a ST-Link V2 to use it as a BlackMagicProbe, to get support for RTT and see NRF_LOG messages.
Some clones have only 64kb, this is too short.
Even 128kb is too small when enabling RTT, but we can comment parts of the BMP source code.

```
git clone --recursive git@github.com:blackmagic-debug/stlink-tool.git
( cd stlink-tool && make )
```
Then put the `stlink-tool` binary in your path.

Get [BMP full sources](https://github.com/blackmagic-debug/blackmagic/releases)

Comment out all probes except Nordic nrf51 in `src/target/cortexm.c` big switch for probes. It should remain
```c
    switch (t->designer_code) {
    case JEP106_MANUFACTURER_NORDIC:
        PROBE(nrf51_probe);
        break;
    }
```
```
make -j PROBE_HOST=stlink ST_BOOTLOADER=1 ENABLE_RTT=1
```
Then flash the ST_Link V2
```
stlink-tool src/blackmagic.bin
```
See [src/platforms/stlink/README.md](https://github.com/blackmagic-debug/blackmagic/blob/main/src/platforms/stlink/README.md) for more details.
Unplug/plug.  
Every time you plug the ST-Link, you have to run `stlink-tool` to enable BMP.  
Under linux, it is convenient to install [udev rules](https://github.com/blackmagic-debug/blackmagic/blob/main/driver/README.md#99-blackmagic-plugdevrules) to get aliases `/dev/ttyBmpGdb` and `/dev/ttyBmpTarg`.

Note that using a native ST-Link V2 with BlackMagicProbe "hosted" will not allow to see NRF_LOG messages.

## Debugging the code with gdb and BMP with RTT to monitor NRF_LOG

Assuming you have a BlackMagicProbe with RTT support made out of a ST-Link V2.

RTT usage: https://black-magic.org/usage/rtt.html

```
stlink-tool
sleep 1
screen /dev/ttyBmpTarg
```
In another terminal
```
$ arm-none-eabi-gdb
(gdb) target extended-remote /dev/ttyBmpGdb
(gdb) monitor swdp_scan
 1      Nordic nRF52 M4
 2      Nordic nRF52 Access Port.
(gdb) attach 1
(gdb) monitor rtt
```

We are now able to use gdb and see the NRF_LOG messages on the other terminal.

## Using JLink with RTT to monitor NRF_LOG

cf https://embeddedexplorer.com/nrf52-nrf-log-tutorial/

```
JLinkExe -if SWD -device nrf52 -speed 4000 -autoconnect 1
```
in a second terminal:
```
JLinkRTTClient
```
