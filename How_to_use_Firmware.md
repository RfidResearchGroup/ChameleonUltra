# How to use the Firmware
In this file you can look up how to [edit](#Editing-the-code), [compile](#Compiling-the-code) and [debug](#Debugging-the-code) the code!

## Editing the code
We are using [Visual Studio Code](https://code.visualstudio.com/download) to edit this project! Simply download and install it!
 - Install the [ARM-GCC](https://mynewt.apache.org/latest/get_started/native_install/cross_tools.html) version [9.3.1 Compiler](https://developer.arm.com/downloads/-/gnu-rm) and remember the path where you installed it.
 - Install the [C++ Extension](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cpptools) in VS-Code.
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

## Debugging the code
- Install [openocd](https://mynewt.apache.org/latest/get_started/native_install/cross_tools.html)
- Install [Cortex-Debug](https://marketplace.visualstudio.com/items?itemName=marus25.cortex-debug) VS-Code Extension
- MORE INFO ADDED LATER