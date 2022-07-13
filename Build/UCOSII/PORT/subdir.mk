#
# Auto-Generated file. Do not edit!
#

# Add inputs and outputs from these tool invocations to the build variables
C_SRCS += \
../UCOSII/PORT/os_cpu_c.c \
../UCOSII/PORT/os_dbg.c \
../UCOSII/PORT/os_dbg_r.c

OBJS += \
./UCOSII/PORT/os_cpu_c.o \
./UCOSII/PORT/os_dbg.o \
./UCOSII/PORT/os_dbg_r.o

C_DEPS += \
./UCOSII/PORT/os_cpu_c.d \
./UCOSII/PORT/os_dbg.d \
./UCOSII/PORT/os_dbg_r.d

# Each subdirectory must supply rules for building sources it contributes
UCOSII/PORT/%.o: ../UCOSII/PORT/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: SDE Lite C Compiler'
	C:/LoongIDE/mips-2011.03/bin/mips-sde-elf-gcc.exe -mips32 -G0 -EL -msoft-float -DLS1B -DOS_RTTHREAD  -O0 -g -Wall -c -fmessage-length=0 -pipe  -I"../" -I"../include" -I"../RTT4/include" -I"../RTT4/port/include" -I"../RTT4/port/mips" -I"../RTT4/components/finsh" -I"../RTT4/components/dfs/include" -I"../RTT4/components/drivers/include" -I"../RTT4/components/libc/time" -I"../RTT4/bsp-ls1x" -I"../ls1x-drv/include" -I"../lwIP-1.4.1/include" -I"../lwIP-1.4.1/include/ipv4" -I"../lwIP-1.4.1/port/include" -I"C:/LS-Workspace/project5/src" -I"C:/LS-Workspace/project5/UCOSII/PORT" -I"C:/LS-Workspace/project5/UCOSII/CORE" -I"C:/LS-Workspace/project5/UCOSII/CONFIG" -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

