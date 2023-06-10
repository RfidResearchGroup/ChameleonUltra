# How to use the Firmware
In this file you can look up how to [edit](#Editing-the-code), [compile](#Compiling-the-code) and [debug](#Debugging-the-code) the code!

## Editing the code
We are using [Visual Studio Code](https://code.visualstudio.com/download) to edit this project! Simply download and install it!
 - Install the [ARM-GCC](https://mynewt.apache.org/latest/get_started/native_install/cross_tools.html) version [10.3.1 Compiler Tested](https://developer.arm.com/downloads/-/gnu-rm) and remember the path where you installed it.
 - Install the [C++ Extension](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cpptools) in VS-Code.
 - Install the [C++ Extension Pack](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cpptools-extension-pack) in VS-Code.
 - Create a new IntelliSense Configuration:
	 - press F1 in VS-Code and enter `C/C++: Edit Configurations (UI)`
	 - Add a new Configuration and name it
	 - Specify your Compiler path (path of previously installed Compiler `bin` folder)
	 - Change IntelliSense mode to `gcc-arm (legacy)`
	 - Add include path `${workspaceFolder}/**`
## Compiling the code
 - Install the compiler (for instructions have a look at [Editing the code](#Editing-the-code))
 - Edit Makefile.defs:
	- Change `GNU_INSTALL_ROOT`(path of previously installed Compiler `bin` folder)
	- Change `GNU_VERSION` (Version of the installed Compiler)
	- Change the other paths to match your system
	- Don't forget to remove the `#` in front of the changed lines
- Install make
	- **Ubuntu:**  
		- Open a terminal.
		- Run the following command to install Make: `sudo apt-get install build-essential`
	- **Windows using Chocolatey:**  
	  - Install Chocolatey: 
		  - Open a PowerShell terminal with administrator privileges. 
		   - Run the following command to install Chocolatey: 
		``` Set-ExecutionPolicy Bypass -Scope Process -Force; [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072; iex ((New-Object System.Net.WebClient).DownloadString('https://chocolatey.org/install.ps1')) ``` 
	  - Install Make:
	    - In the same PowerShell terminal, run the following command to install Make using Chocolatey: `choco install make`
	- **macOS:** 
	  - Open a terminal.
	  - Install Homebrew package manager by running the following command: `/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"`
	  - Once Homebrew is installed, run the following command to install Make: `brew install make`
- go into folder and run `make`
  - if make fails try to add a folder named `objects` in the `firmware` folder
- do this for `application` and `bootloader` folder

## Merging the code
- Install [nRF Util](https://www.nordicsemi.com/Products/Development-tools/nrf-util)
  - Move it to a known path like `C:/nrfutil/`
  - Add this path to `PATH` Environment Variable
- Install [nRF Command Line Tools](https://www.nordicsemi.com/Products/Development-tools/nRF-Command-Line-Tools)
- Install nRF Util packages:
  - `nrfutil install completion device nrf5sdk-tools trace`
- go into `objects` folder
- generate settings by `nrfutil settings generate --family NRF52840 --application application.hex --application-version 1 --bootloader-version 1 --bl-settings-version 2 settings.hex` (only has to be done once)
- merge bootloader and settings by `mergehex --merge bootloader.hex settings.hex --output bootloader_settings.hex`
- copy the `s140_nrf52_7.2.0_softdevice.hex` file from `nrf52_sdk/components/softdevice/s140/hex/` to `objects`
- rename `s140_nrf52_7.2.0_softdevice.hex` to `softdevice.hex`
- merge bootloader_settings and app by `mergehex --merge bootloader_settings.hex application.hex softdevice.hex --output project.hex`

## Uploading the code
- If you are using J-Link:
  - Install [Segger J-Link](https://www.segger.com/downloads/jlink)
  - [Merge](#Merging-the-code) the code
  - Upload softdevice with `make flash_softdevice`
  - Upload bootloader in bootloader folder with `make flash`
  - Upload application in application folder with `make flash`
- If you are using ST-Link:
  - Install [openocd](https://mynewt.apache.org/latest/get_started/native_install/cross_tools.html)
  - Install [ST-Link drivers](https://www.st.com/en/development-tools/stsw-link009.html)
  - Extract downloaded zip
  - run `dpinst_amd64.exe`
  - Upload full image with `make flash_stlink` in application or bootloader folder


## Debugging the code
- Install [Cortex-Debug](https://marketplace.visualstudio.com/items?itemName=marus25.cortex-debug) VS-Code Extension
- MORE INFO ADDED LATER