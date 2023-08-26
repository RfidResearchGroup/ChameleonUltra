# CLI

The CLI (**C**ommand **L**ine **I**nterface) is the official way to control your Chameleon.

It requires at least **Python 3.9** version.

## Installing

There are multiple ways to install the CLI, depending on your OS.

### Windows

Windows users have the choice of 4 options:

#### ProxSpace

Using ProxSpace to build the CLI is the easiest and most comfortable way to get started.

1. Download ProxSpace from the [official GitHub](https://github.com/Gator96100/ProxSpace/releases/latest)

2. [Download 7zip](https://www.7-zip.org/) to extract the archive

3. Install 7zip by double clicking the Installer and clicking `Install`

4. Right-click on the downloaded archive and select `7zip -> Unpack to "ProxSpace"`

5. Open a terminal in the proxspace folder. If you are on a new Windows install, you should be able to just right-click and select `Open in Terminal`. If that option is not visible and the ProxSpace folder is still in your downloads folder, press `win+r` and type `powershell` followed by enter. In Powershell now type `cd ~/Downloads/ProxSpace`

6. Run the command `.\runme64.bat`. After successful completion, you should be dropped to the `pm3 ~ $` shell.

7. Clone the Repository by typing `git clone https://github.com/RfidResearchGroup/ChameleonUltra.git`

8. Now go into the newly created folder with `cd ChameleonUltra/software/src`

9. Prepare for package installation with `pacman-key --init; pacman-key --populate; pacman -S msys2-keyring --noconfirm`

10. Proceed by installing Ninja with `pacman -S ninja --noconfirm`

11. Build the required config by running `cmake .`

12. And the binaries with `cmake --build .`

13. Copy the binaries by running `cp -r ~/ChameleonUltra/software/bin/* ~/ChameleonUltra/software/script/`

14. Go into the script folder with `cd ~/ChameleonUltra/software/script/`

15. Install python requirements with `pip install -r requirements.txt`

16. Finally run the CLI with `python chameleon_cli_main.py`

To use after installing, just do the following:

1. Run `runme64.bat`

2. Go into the script folder with `cd ~/ChameleonUltra/software/script/`

3. Run the CLI with `python chameleon_cli_main.py`

#### WSL2

Coming Soon

#### WSL1

Coming Soon

#### Build Natively

Building natively is a bit more advanced and not recommended for beginners

1. Download and install [Visual Studio Community](https://visualstudio.microsoft.com/de/downloads/)

2. On the workload selection screen, choose the `Desktop development with C++` workload. Click `Download and Install`

3. Download and install [git](https://git-scm.com/download). When asked, add to your path

4. Download and install [cmake](https://cmake.org/download/). Again, when asked, add to your path

5. Download and install [python](https://www.python.org/downloads/). When asked, add to your path (small checkbox in the bottom left)

6. Choose a suitable location and open a terminal. Clone the repository with `git clone https://github.com/RfidResearchGroup/ChameleonUltra.git`

7. Change into the binaries folder with `cd ChameleonUltra/software/src`

8. Build the required config by running `cmake .`

9. And the binaries with `cmake --build .`

10. Copy the binaries by running `cp -r ../bin/Debug/* ../script/`

11. Go into the script folder with `cd ../script/`

12. Create a python virtual environment with `python -m venv venv`

13. Activate it by running `.\venv\Scripts\Activate.ps1`

14. Install python requirements with `pip install -r requirements.txt`

15. Finally run the CLI with `python chameleon_cli_main.py`

To run again after installing, just do the following:

1. Activate venv by running `.\venv\Scripts\Activate.ps1`

2. Run the CLI with `python chameleon_cli_main.py`

### Linux

*Coming Soon*

### MacOS

*Coming Soon*

## Usage

When in the CLI, plug in your Chameleon and connect with `hw connect`. If autodetection fails, get the Serial Port used by your Chameleon and run `hw connect -p COM11` (Replace `COM11` with your serial port, on Linux it may be `/dev/ttyACM0`)

### Common activities

- Change slot: hw slot change -s [1-8]

*More examples coming soon*

### Available Commands

In `()` is the argument description, `[]` are possible entries for that argument (eg `[1-8]`)

| Command          | Arguments                                                                 | Description                               |
|:----------------:|:-------------------------------------------------------------------------:|:-----------------------------------------:|
| `hw factory_reset` | `--i-know-what-im-doing` (Make sure you really want to wipe your Chameleon) | Returns the Chameleon to factory settings |
|                  |                                                                           |                                           |
|                  |                                                                           |                                           |
