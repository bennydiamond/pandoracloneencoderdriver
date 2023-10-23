# pandoracloneencoderdriver
Linux module to interface arcade controller encoder on Pandora Box clone systems
The goal of this repo is to interface the arcade encoder board of bootleg/clone Pandora Box arcade systems without hardware modifications.


This drivers aims to expose the serial arcade controller encoder device found in bootleg/clone Pandora Box and expose a 2 * 1-player joystick interfaces to the system.
![Screenshot 2023-11-03 195725](https://github.com/bennydiamond/pandoracloneencoderdriver/assets/6563977/902a9278-2160-4183-b4ae-032804a8783a)

This repo contains a 2 part solution:
- A kernel module to interface a tty interface and spawn 2 "js" joystick interfaces
- A userland program used to configure an instance of the kernel module and bind it to a tty interface


## Disclaimer

This software is GPLv3 licensed. Commercial usage of this product is forbidden.

I take no responsibility if you damage your hardware or corrupt your system with this piece of software. 
It is highly unlikely any hardware damage could result from using this. 


## User prerequisite

In order to use this project the user should have fairly good knowledge of Linux, ssh, systemd.
This most likely not a ready-made project. You will have to modify Makefiles, scripts and other files to make it work on your setup.
I made this for me, and I'm sharing it in hopes it will help others in the same situation as me.


## System prerequisites

The host system must have a kernel with module CONFIG_SERIO enabled. Usually this is not an issue for any flavor of the Linux Kernel that is not 20 years old. Unfortunately, default compile option of the 3.14 kernel in CoreELEC/EmuELEC do not enable said module. 

This means any official release of EmuELEC that do not use the Amlogic-**ng** kernel will not support this driver out of the box. This includes EmuELEC 3.9 as well as the non "ng" version of EmuELEC 4.3. 
Kernel recompilation for these is necessary.

It is also highly recommended to disable the debug console usually attach to ttyS0(which is usually mapped onto the debug uart) or at least move it to another tty device. 
On EmuELEC, you usually need to modify the kernel's initargs (using `fw_printenv initargs` and `fw_setenv initargs` to remove any command line option related to console uart allocation) and disable `debug-shell.service` systemd unit.

## Building

In order to build this projet, you will need to build the cross-compiler toolchain and the linux kernel for your target system.

Using EmuELEC's repo on tag `v4.3`, the following command will build both the toolchain as well as the kernel
```
PROJECT=Amlogic ARCH=aarch64 DISTRO=EmuELEC  scripts/build_mt linux
```

After having the kernel successfully built, you must modify the makefile of this repo to point it to the location of the kernel build folder.

Once properly configured, running "make install" will create all the necessary files in the `pandoraclone` folder. You will have to copy this folder at the root of your EmuELEC's `/storage` partition.

You will then need to copy the `pandoraclone.service` file into `/storage/.config/system.d/` and enable the systemd unit with command `systemctl enable pandoraclone.service`

This file will invoke the `run.sh` on system boot which in turn will disable debug-shell.service, insert the custom kernel driver module and run a modified version of the program 'inputattach' to bind the hardware to the driver and expose 2 joystick interfaces.


On system reboot, you should have your arcade controls detected in EmulationStation.


## EmuELEC custom kernel building

Afterward, if you want to compile your own kernel in order to replace the stock one on a vanilla v4.3 install (following instructions assumes a target version of EmuELEC 4.3), in file `EmuELEC/projects/Amlogic/linux/linux.aarch64.conf`, replace line 

```
# CONFIG_SERIO is not set
```

with
```
CONFIG_SERIO=y
```

Re-running the command `PROJECT=Amlogic ARCH=aarch64 DISTRO=EmuELEC  scripts/build_mt linux` will likely fail at some point. This is normal and expected.
You must run the kernel config tool. 
```
PROJECT=Amlogic ARCH=arm64 DISTRO=EmuELEC make oldconfig -C build.EmuELEC-Amlogic.aarch64-4.3/linux-07d26b4ce91cf934d65a64e2da7ab3bc75e59fcc/
```
When prompted, press enter to select the default option for all queries. CONFIG_SERIO and CONFIG_SERIO_RAW should be enabled. The rest of the entries are specific input device hardware drivers. They are not needed here.

Once finished, re-run a third time the kernel build command
`PROJECT=Amlogic ARCH=aarch64 DISTRO=EmuELEC  scripts/build_mt linux`

At the end of the compilation process the kernel image file will be created at location "EmuELEC/build.EmuELEC-Amlogic.aarch64-4.3/linux-07d26b4ce91cf934d65a64e2da7ab3bc75e59fcc/arch/arm64/boot/Image.lzo"

Rename this file to `kernel.img` and copy it to the "EMUELEC" partition on your SD card, replacing the stock kernel.img already on it. The md5 cheksum file is irrelevant. You can delete keep it, it does not matter.


## Backstory

I acquired a Pandora Box Arcade system and upon opening up, I discovered my unit is not "genuine" and thus jailbreak tool like [Pandory tools](https://github.com/TeamPandory/pandorytool) will not work on these. These bootleg devices are so chuck-full of garbage hacks and duplicate games that cleaning them up makes no sense. Also, the quality of emulation is really poor with very little amount of configurability. Hence the desire to start from the ground up and build my own game library using stock EmuELEC.
![IMG_20230918_102009](https://github.com/bennydiamond/pandoracloneencoderdriver/assets/6563977/e3c771a5-04c9-4439-8b65-bc67c3a537dc)

Bootleg/clone Pandora Box 2-players arcade system sold on Amazon/Aliexpress are usually outfitted with a "handmade" logic board solution comprised of an old gutted out lower-end Android TV box with a custom daughterboard to supplement functionality.

On my particular unit, the Android TV Box runs a s905L RevC SOC with 1GB RAM. The supplied MicroSD Card runs a custom installation of EmuELEC 3.9 with a custom Frontend and control input driver software solution.
![IMG_20230926_172227](https://github.com/bennydiamond/pandoracloneencoderdriver/assets/6563977/b143eb21-3cc6-48c7-b609-7560218b05a0)

The daughterboard is in charge of arcade control input and transmit to main system, speaker output (there is a small amplifier circuit), USB passthrough to main system on the bottom USB female port and Device emulation on the top USB port (expose as a dual stick fighter pad to a computer).
![IMG_20230918_102828](https://github.com/bennydiamond/pandoracloneencoderdriver/assets/6563977/420bfffc-e8b5-4357-ad56-bc8c78f29473)

The daughterboard is handsoldered to various test points onto the Android TV box' mainboard.
![IMG_20230926_172601](https://github.com/bennydiamond/pandoracloneencoderdriver/assets/6563977/cfdb3f7e-bab5-4367-88e4-2cf4b23b591f)



The particularity of the arcade control encoder part of the daughterboard is it is interfacing the Android TV box through the debug uart port of the latter; making this rather one of a kind arcade encoder board in 2023.

As mentionned earlier, stock system is programmed with a custom EmuELEC 3.9 distro on a SD card. The modified system runs a custom frontend program that seems to be in charge of handling player input controls. 

After scouring the internet and inspecting the Linux Kernel source code, I could not find any ready-made piece of software to interface this obscure hardware. 

Reverse-engineering of the custom software solution was an option but upon sniffing the serial protocol between the 2 boards, it was determined writing a driver from scratch would be far more easier and faster. So I made my own.
