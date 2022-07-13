#
# Auto-Generated file. Do not edit!
#

# Add inputs and outputs from these tool invocations to the build variables
C_SRCS += \
../UCOSII/CORE/os_core.c \
../UCOSII/CORE/os_flag.c \
../UCOSII/CORE/os_mbox.c \
../UCOSII/CORE/os_mem.c \
../UCOSII/CORE/os_mutex.c \
../UCOSII/CORE/os_q.c \
../UCOSII/CORE/os_sem.c \
../UCOSII/CORE/os_task.c \
../UCOSII/CORE/os_time.c \
../UCOSII/CORE/os_tmr.c \
../UCOSII/CORE/ucos_ii.c

OBJS += \
./UCOSII/CORE/os_core.o \
./UCOSII/CORE/os_flag.o \
./UCOSII/CORE/os_mbox.o \
./UCOSII/CORE/os_mem.o \
./UCOSII/CORE/os_mutex.o \
./UCOSII/CORE/os_q.o \
./UCOSII/CORE/os_sem.o \
./UCOSII/CORE/os_task.o \
./UCOSII/CORE/os_time.o \
./UCOSII/CORE/os_tmr.o \
./UCOSII/CORE/ucos_ii.o

C_DEPS += \
./UCOSII/CORE/os_core.d \
./UCOSII/CORE/os_flag.d \
./UCOSII/CORE/os_mbox.d \
./UCOSII/CORE/os_mem.d \
./UCOSII/CORE/os_mutex.d \
./UCOSII/CORE/os_q.d \
./UCOSII/CORE/os_sem.d \
./UCOSII/CORE/os_task.d \
./UCOSII/CORE/os_time.d \
./UCOSII/CORE/os_tmr.d \
./UCOSII/CORE/ucos_ii.d

# Each subdirectory must supply rules for building sources it contributes
UCOSII/CORE/%.o: ../UCOSII/CORE/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: SDE Lite C Compiler'
	C:/LoongIDE/mips-2011.03/bin/mips-sde-elf-gcc.exe -mips32 -G0 -EL -msoft-float -DLS1B -DOS_RTTHREAD  -O0 -g -Wall -c -fmessage-length=0 -pipe  -I"../" -I"../include" -I"../RTT4/include" -I"../RTT4/port/include" -I"../RTT4/port/mips" -I"../RTT4/components/finsh" -I"../RTT4/components/dfs/include" -I"../RTT4/components/drivers/include" -I"../RTT4/components/libc/time" -I"../RTT4/bsp-ls1x" -I"../ls1x-drv/include" -I"../lwIP-1.4.1/include" -I"../lwIP-1.4.1/include/ipv4" -I"../lwIP-1.4.1/port/include" -I"C:/LS-Workspace/project5/src" -I"C:/LS-Workspace/project5/UCOSII/PORT" -I"C:/LS-Workspace/project5/UCOSII/CORE" -I"C:/LS-Workspace/project5/UCOSII/CONFIG" -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

