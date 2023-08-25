# CLI

The CLI (**C**ommand **L**ine **I**nterface) is the official way to control your Chameleon.

## Installing

There are multiple ways to install the CLI, depending on your OS.

### Windows

Windows users have the choice of 4 options:

#### Proxspace

Using Proxspace to build the CLI is the easiest and most comfortable way to get started.

1. Download proxspace from the [official github](https://github.com/Gator96100/ProxSpace/releases/latest)

2. [Download 7zip](https://www.7-zip.org/) to extract the archive

3. Install 7zip by double clicking the Installer and clicking `Install`

4. Rightclick on the downloaded archive and select `7zip -> Unpack to "ProxSpace"`

5. Open a terminal in the proxspace folder. If your on a new windows install you should be able to just rightclick and select `Open in Terminal`. If that option is not visible and the ProxSpace folder is still in your downloads folder, press `win+r` and type `powershell` followed by enter. In Powershell now type `cd ~/Downloads/ProxSpace`

6. Run the command `.\runme64.bat`. After succesfull completion you should be dropped to the `pm3 ~ $` shell.

7. Clone the Repository by typing `git clone https://github.com/RfidResearchGroup/ChameleonUltra.git`

8. Now go into the newly created folder with `cd ChameleonUltra/software/src`

9. Prepare for package installation with `pacman-key --init; pacman-key --populate`

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

#### Build Nativly

Building Nativly is a bit more advanced and not recommended for beginners

1. Download and install [Visual Studio Community](https://visualstudio.microsoft.com/de/downloads/)

2. On the workload selection screen choose the `Desktop development with C++` workload. Click `Download and Install`

3. Download and install [git](https://git-scm.com/download), when asked, add to your path

4. Download and install [cmake](https://cmake.org/download/), again, when asked, add to your path

5. Download and install [python](https://www.python.org/downloads/), when asked, add to your path (small checkbox in the bottom left)

6. Choose a suitable location and open a terminal. Clone the repository with `git clone https://github.com/RfidResearchGroup/ChameleonUltra.git`

7. Change into the binarys folder with `cd ChameleonUltra/software/src`

8. Build the required config by running `cmake .`

9. And the binaries with `cmake --build .`

10. Copy the binaries by running `cp -r ../bin/Debug/* ../script/`

11. Go into the script folder with `cd ../script/`

12. Create a python virtual enviroment with `python -m venv venv`

13. Activate it by running `.\venv\Scripts\Activate.ps1`

14. Install python requirements with `pip install -r requirements.txt`

15. Finally run the CLI with `python chameleon_cli_main.py`

To run again after installing, just do the following:

1. Activate venv by running `.\venv\Scripts\Activate.ps1`

2. Run the CLI with `python chameleon_cli_main.py`

### Linux

Coming Soon

### MacOS

Coming Soon

## Usage

When in the CLI, plug in your chameleon and connect with `hw connect`. If this fails, get the Serial Port used by your Chameleon and run `hw connect -p COM11` (Replace `COM11` with your serial port, on linux it may be `/dev/ttyacm0`)

### Common activites:

- Change slot: hw slot change -s [1-8]

- More examples coming soon

### Available Commands:

In `()` is the argument description, `[]` are possible entries for that argument (eg `[1-8]`)

| Command ID | Command          | Arguments                                                                 | Description                               |
|:----------:|:----------------:|:-------------------------------------------------------------------------:|:-----------------------------------------:|
| 1020       | hw factory_reset | --i-know-what-im-doing (Make sure you really want to wipe your chameleon) | Returns the Chameleon To factory settings |
|            |                  |                                                                           |                                           |
|            |                  |                                                                           |                                           |
