cmake_minimum_required(VERSION 3.22)

# 设置 C 标准
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

# 构建类型
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# 选项: 使用 Bootloader (应用偏移到 0x2000)
option(BOOTLOADER "Build with bootloader partition (offset 0x2000)" OFF)

# 选项: 生成虚拟传感器数据（测试 UART 吞吐量）
option(TEST_DUMMY_DATA "Generate dummy sensor data instead of reading MPU6050" OFF)

# 项目名称
set(CMAKE_PROJECT_NAME stm32mpu6050)
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# 启用 C 和 ASM 语言
enable_language(C ASM)

# 生成编译命令数据库（方便 clangd 等工具）
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# =============================================
# 1. FreeRTOS 内核源码
# =============================================
set(FREERTOS_DIR "Middlewares/FreeRTOS/kernel")
set(FREERTOS_PORT_DIR "${FREERTOS_DIR}/portable/GCC/ARM_CM3")

file(GLOB FREERTOS_SRC
    "${FREERTOS_DIR}/tasks.c"
    "${FREERTOS_DIR}/queue.c"
    "${FREERTOS_DIR}/list.c"
    "${FREERTOS_DIR}/timers.c"
    "${FREERTOS_DIR}/event_groups.c"
    "${FREERTOS_PORT_DIR}/port.c"
)
set(FREERTOS_HEAP_SRC "${FREERTOS_DIR}/portable/MemMang/heap_4.c")

list(APPEND FREERTOS_SRC ${FREERTOS_HEAP_SRC})

# =============================================
# 2. 用户自定义源文件
# =============================================
set(APP_SRC
    Core/Src/bsp_mpu6050.c
    Core/Src/data_protocol.c
    Core/Src/flash_config.c
    Core/Src/system_config.c
    Core/Src/cli_shell.c
    Core/Src/task_watchdog.c
    Core/Src/motor_control.c
    Core/Src/control_task.c
    Core/Src/sensor_fusion.c
    Core/Src/tasks/app_tasks.c
    Core/Src/tasks/freertos_vector.c
)

add_executable(${CMAKE_PROJECT_NAME}
    ${APP_SRC}
    ${FREERTOS_SRC}
)

# =============================================
# 3. 包含 CubeMX 生成的子目录（驱动库）
# =============================================
add_subdirectory(cmake/stm32cubemx)

# =============================================
# 4. 头文件搜索路径
# =============================================
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    Core/Inc
    Core/Inc/tasks
    Drivers/STM32F1xx_HAL_Driver/Inc
    Drivers/STM32F1xx_HAL_Driver/Inc/Legacy
    Drivers/CMSIS/Device/ST/STM32F1xx/Include
    Drivers/CMSIS/Include
    ${FREERTOS_DIR}/include
    ${FREERTOS_PORT_DIR}
)

# =============================================
# 5. 预定义宏
# =============================================
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    USE_HAL_DRIVER
    STM32F103xB
)

if(TEST_DUMMY_DATA)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE TEST_DUMMY_DATA=1)
    message(STATUS "TEST_DUMMY_DATA enabled: generating dummy sensor data")
endif()

# =============================================
# 6. 链接库（CubeMX 生成的接口库 + 数学库）
# =============================================
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
    m          # 数学库，提供 sqrtf、atan2f 等函数
)

# 7. 条件: Bootloader 模式 & 链接脚本
# =============================================
if(BOOTLOADER)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE APP_START_OFFSET=0x2000)
    target_link_options(${CMAKE_PROJECT_NAME} PRIVATE
        -Wl,-T${CMAKE_SOURCE_DIR}/STM32F103XX_FLASH_APP.ld
    )
    message(STATUS "Building with bootloader partition (APP at 0x08002000)")
else()
    target_link_options(${CMAKE_PROJECT_NAME} PRIVATE
        -Wl,-T${CMAKE_SOURCE_DIR}/STM32F103XX_FLASH.ld
    )
endif()

# =============================================
# 8. 生成 .bin / .hex 输出
# =============================================
set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES
    ADDITIONAL_CLEAN_FILES
        "${CMAKE_PROJECT_NAME}.hex;${CMAKE_PROJECT_NAME}.bin"
)
add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex ${CMAKE_PROJECT_NAME}.elf
            ${CMAKE_PROJECT_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary ${CMAKE_PROJECT_NAME}.elf
            ${CMAKE_PROJECT_NAME}.bin
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Generating .hex and .bin output files"
)

# =============================================
# 9. 移除自动添加的 libob.a 依赖（避免警告）
# =============================================
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# 生成 map 文件并加入 clean 列表
set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES
    ADDITIONAL_CLEAN_FILES ${CMAKE_PROJECT_NAME}.map
)
