# Allied Vision MIPI CSI-2 driver for Toradex Apalis iMX8

Driver for Allied Vision Alvium cameras with MIPI CSI-2 interface.   
![Alvium camera](https://cdn.alliedvision.com/fileadmin/content/images/cameras/Alvium/various/alvium-cameras-models.png)

Supported board: Toradex Apalis iMX8   
Supported OS: Embedded Linux, see https://www.toradex.com/operating-systems/embedded-linux

Linux kernel version: 4.14.126

Fork information:   
Repository: https://git.toradex.com/cgit/linux-toradex.git   
Branch: toradex_4.14-2.0.x-imx
Commit: f01f6885f1255ca634f361c97301b1d9f3aae170

# Overview
The following instructions describe how to install Torizon and the Allied Vision CSI-2 driver to your Toradex iMX8 Apalis board.  
You can either download the pre-compiled binaries from https://www.alliedvision.com/en/products/software/embedded-software-and-drivers or compile them yourself.

## Prerequisites for cross-compiling
### Host PC
* The scripts require Git on the host PC.
* We have tested the cross-compilation with Ubuntu 18.04 LTS and Debian 10.
* Before cross-compiling the kernel, install all required packages.    

Required packages for Ubuntu or Debian:   
```sudo apt-get install libncurses-dev flex bison openssl libssl-dev dkms libelf-dev libudev-dev libpci-dev libiberty-dev autoconf gcc-aarch64-linux-gnu bc device-tree-compiler```

# Cross-compiling the driver
If you want to use the pre-compiled binaries provided on our website, you can omit these steps.  

1. On the Linux host PC, clone the git repository   
   ```$ git clone https://github.com/alliedvision/linux_imx8_apalis.git```

2. Enter the directory "avt_build" in the directory   
   ```$ cd linux_imx8_apalis/avt_build```
   
3. Run the build script to cross-compile all relevant binaries (kernel image, modules, device tree blob)   
   ```$ ./build.sh```
   
4. The deploy subdirectory now contains "AlliedVision_Apalis_iMX8_Torizon_<git-rev>.tar.gz".   
   Copy this file to the Apalis iMX8 board. Now you can proceed with installing the Alvium CSI-2 driver.

# Installing the Alvium CSI-2 driver
To install the binaries on the target Apalis iMX8 board:  
Use the self-compiled binaries or download the pre-compiled binaries from our website https://www.alliedvision.com/en/products/software/embedded-software-and-drivers.   

1. Copy the kernel binaries to the iMX8 Apalis board (for example, via scp).

2. On the Apalis iMX8 board, extract the binaries:   
   ```$ tar -zxvf <AlliedVision_Apalis_iMX8_Torizon_XXX.tar.gz>```
   
3. Enter the extracted directory:   
   ```$ cd <AlliedVision_Apalis_iMX8_Torizon_XXX>```
   
4. To install everything on the board, run the installation script as superuser:   
   ```$ sudo ./install.sh```
   
5. Reboot the board. Now the Alvium CSI-2 driver is installed.

# Setting up a Debian container on Torizon
## Downloading and starting the Toradex docker container

To download and start the Toradex docker container for Debian, run:
```
$ docker run -e ACCEPT_FSL_EULA=1 -d --rm --name=alliedvision-container \
			 --net=host --cap-add CAP_SYS_TTY_CONFIG \
             -v /dev:/dev -v /tmp:/tmp -v /run/udev/:/run/udev/ \
             --device-cgroup-rule='c 4:* rmw'  --device-cgroup-rule='c 13:* rmw' \
		     --device-cgroup-rule='c 199:* rmw' --device-cgroup-rule='c 226:* rmw' \
			 --device=/dev/video0 \
              torizon/arm64v8-debian-weston-vivante:buster --developer \
			  weston-launch --tty=/dev/tty7 --user=torizon 
```

The paramater "--device=/dev/video0" makes sure that the video device can be accessed from within the container.
If no camera is connected, remove the parameter because docker will complain that no such file exists (/dev/video0).   
Details: https://developer.toradex.com/knowledge-base/debian-container-for-torizon#apalis-imx8colibri-imx8x

## Configuring user rights
By default, the user torizon in the container has no root rights. 
To assign root rights:    

1. In another session (serial or ssh), run the following command to login as root to the running container:   
```$ docker exec -u root -t -i alliedvision-container /bin/bash```

2. Add the user torizon to the group "sudo" and "video":   
```# usermod -a -G sudo torizon```   
```# usermod -a -G video torizon```   

3. Set the password of the user torizon:   
```# passwd torizon```

4. Leave the bash running inside the container:   
```# exit```

5. To commmit the changes to the image, get the id of the running container:   
```$ docker ps -a```

6. Save the container as new image or overwrite the existing one:   
```$ docker commit <container_id> new_image_name:tag_name```

7. Reboot the system.   
8. Run the command above (```$ docker run...```) to start the docker container (adjust the image name if you created a new one above).   
In the command, replace the image name "torizon/arm64v8-debian-weston-vivante:buster" with the name of the new image.   

The user torizon now has sudo rights and can install applications with ```sudo apt install ...```.


 ## Additional information
 :open_book:
 https://github.com/alliedvision/documentation/blob/master/Toradex_Apalis.md
