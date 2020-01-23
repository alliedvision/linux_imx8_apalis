# Beta Release

Allied Vision CSI-2 Alvium Camera Driver

This program is free software; you may redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; version 2 of the License.

THE SOFTWARE IS PRELIMINARY AND STILL IN TESTING AND VERIFICATION PHASE AND IS PROVIDED ON AN “AS IS” AND “AS AVAILABLE” BASIS AND IS BELIEVED TO CONTAIN DEFECTS.
A PRIMARY PURPOSE OF THIS EARLY ACCESS IS TO OBTAIN FEEDBACK ON PERFORMANCE AND THE IDENTIFICATION OF DEFECT SOFTWARE, HARDWARE AND DOCUMENTATION.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

---

# Allied Vision MIPI CSI-2 driver for Toradex Apalis iMX8

Driver for Allied Vision Alvium cameras with MIPI CSI-2 interface.   
Supported board: Toradex Apalis iMX8   
Supported OS: Embedded Linux, see https://www.toradex.com/operating-systems/embedded-linux

Linux kernel version: 4.14.126

Fork information:   
Repository: https://git.toradex.com/cgit/linux-toradex.git   
Branch: toradex_4.14-2.0.x-imx
Commit: f01f6885f1255ca634f361c97301b1d9f3aae170

## Build instructions

Build environment: Cross-compiling on x86-64 Ubuntu (for example, version 18.04)

1. Download Linaro aarch64 Cross-Compile Toolchain for x86_64 (to any directory on your host PC):
  https://releases.linaro.org/components/toolchain/binaries/latest-7/aarch64-linux-gnu/

2. Extract the archive:  
    `$ tar -xf <linaro-archive>.tar.xz`

3. Clone the git repository and enter the folder.

4. Create deploy dir for collecting binaries:   
    `$ mkdir deploy`

5. Set environment variables:   
    `$ export ARCH=arm64`   
    `$ export INSTALL_MOD_PATH=deploy`   
    `$ export CROSS_COMPILE=<cross-compile-prefix>`   
     with cross-compile-prefix = <path-to-linaro-arm64-toolchain>/bin/aarch64-linux-gnu-

6. Make kernel config:   
    `$ make defconfig`

7. Build the kernel image:   
    `$ make Image dtbs modules -j$(nproc)`

8. Copy binaries to the deploy folder:   
    `$ make modules_install`   
    `$ cp arch/arm64/boot/Image deploy`   
    `$ cp arch/arm64/boot/dts/freescale/fsl-imx8qm-apalis.dtb deploy`   

The deploy folder now contains all relevant binaries.  

To install the new kernel and driver on Toradex Apalis iMX8:   
1. On Toradex Apalis iMX8, replace the kernel image and device tree blob on the boot partition.   
2. Copy the lib folder to the target's rootfs.
3. Reboot the board.