# SimpleVisor

The original [SimpleVisor](https://github.com/ionescu007/SimpleVisor) by Alex Ionescu is a simple, Intel x64 Windows-specific hypervisor with two specific goals: using the least amount of assembly code (10 lines), and having the smallest amount of VMX-related code to support dynamic hyperjacking and unhyperjacking (that is, virtualizing the host state from within the host).

This fork provides some additional advanced functionality including:

* Windows 7 compatability
* VMX VPID support
* VMX EPT support

## Introduction

Complete details about SimpleVisor can be found at the [original project page](https://ionescu007.github.io/SimpleVisor/).

SimpleVisor can be built with any recent copy of Visual Studio 2015, and while older compilers have not been tested and are not supported, it's likely that they can build the project as well. It's important, however, to keep the various compiler and linker settings as you see them, however.

SimpleVisor has currently been tested on the following platforms successfully:

* Windows 8.1 on a Haswell Processor (Custom Desktop)
* Windows 10 Redstone 1 on a Sandy Bridge Processor (Samsung 930 Laptop)
* Windows 10 Threshold 2 on a Skylake Processor (Surface Pro 4 Tablet)
* Windows 10 Threshold 2 on a Skylake Processor (Dell Inspiron 11-3153 w/ SGX)

This fork has additionally been tested on the following platform:

* Windows 10 1511 on a Haswell Processor (Lenovo T520 Laptop)

At this time, it has not been tested on any Virtual Machine, but barring any bugs in the implementations of either Bochs or VMWare, there's no reason why SimpleVisor could not run in those environments as well. However, if your machine is already running under a hypervisor such as Hyper-V or Xen, SimpleVisor will not load.

Keep in mind that x86 versions of Windows are expressly not supported, nor are processors earlier than the Nehalem microarchitecture.

## Installation

Because x64 Windows requires all drivers to be signed, you must testsign the SimpleVisor binary. The Visual Studio project file can be setup to do so by using the "Driver Signing" options and enabling "Test Sign" with your own certificate. From the UI, you can also generate your own.

Secondly, you must enable Test Signing Mode on your machine. To do so, first boot into UEFI to turn off "Secure Boot", otherwise Test Signing mode cannot be enabled. Alternatively, if you possess a valid KMCS certificate, you may "Production Sign" the driver to avoid this requirement.

To setup Test Signing Mode, you can use the following command:

```bcdedit /set testsigning on```

After a reboot, you can then setup the required Service Control Manager entries for SimpleVisor in the registry with the following command:

```sc create simplevisor type=kernel binPath="<PATH_TO_SIMPLEVISOR.SYS>"```

You can then launch SimpleVisor with

```net start simplevisor```

And stop it with

```net stop simplevisor```

You must have administrative rights for usage of any of these commands.

## Caveats

SimpleVisor is designed to minimize code size and complexity -- this does come at a cost of robustness. For example, even though many VMX operations performed by SimpleVisor "should" never fail, there are always unknown reasons, such as memory corruption, CPU errata, invalid host OS state, and potential bugs, which can cause certain operations to fail. For truly robust, commercial-grade software, these possibilities must be taken into account, and error handling, exception handling, and checks must be added to support them. Additionally, the vast array of BIOSes out there, and different CPU and chipset iterations, can each have specific incompatibilities or workarounds that must be checked for. ***SimpleVisor does not do any such error checking, validation, and exception handling. It is not robust software designed for production use, but rather a reference code base***.

## License

```
Copyright 2016 Alex Ionescu. All rights reserved.
Copyright 2016 Joe T. Sylve. All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided
that the following conditions are met: 
1. Redistributions of source code must retain the above copyright notice, this list of conditions and
   the following disclaimer. 
2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions
   and the following disclaimer in the documentation and/or other materials provided with the 
   distribution. 

THIS SOFTWARE IS PROVIDED BY ALEX IONESCU AND JOE T. SYLVE ``AS IS'' AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL ALEX IONESCU
OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

The views and conclusions contained in the software and documentation are those of the authors and
should not be interpreted as representing official policies, either expressed or implied, of 
Alex Ionescu or Joe T. Sylve.
```
