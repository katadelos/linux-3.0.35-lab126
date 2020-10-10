# Linux for Amazon Kindle
This repository is a re-upload of the modified Linux source tree used to build kernels for Amazon Kindle e-readers. The original upload of this tree can be found (along with other Kindle source code) [within this archive](https://kindledownloads.s3.amazonaws.com/Kindle_src_5.10.1.1_3358210034.tar.gz).

## Compatibility
This kernel release is compatible with the following devices:
|Device Name                       |MR Name |
|----------------------------------|--------|
|Kindle Oasis                      |KOA     |
|Kindle Paperwhite (6th generation)|PW2     |
|Kindle Paperwhite (7th generation)|PW3     |
|Kindle Voyage (7th generation)    |KV      |
|Kindle (7th generation)           |KT2     |

## Configuration
In order to compile the kernel,  you will need the relevant config file for your device:
|Device            |Config                |
|------------------|----------------------|
|KOA               |imx60_duet_defconfig  |
|PW2, PW3, KV, KT2 |imx60_wario_defconfig |

Run the following command to load the config file:
```
make ARCH=arm CROSS_COMPILE=<path to gcc linaro>/bin/arm-linux-gnueabi- <configure>
```
