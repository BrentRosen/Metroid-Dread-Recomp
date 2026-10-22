# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the ScarletNX contributors
# scarletnx's main where it is also the other drivers (SNX_MULTICALL), which src/nx links into it: the driver its name
# says, from the library of the driver's own folder
add_library(snx_multicall OBJECT main.c)
set_target_properties(snx_multicall PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
target_compile_options(snx_multicall PRIVATE -Wall -Wextra -Werror=missing-prototypes)
if(TARGET snx_ctr_program)
    target_compile_definitions(snx_multicall PRIVATE SNX_MULTICALL_3DS)
    target_link_libraries(snx_multicall PUBLIC snx_ctr_program)
endif()
if(TARGET snx_gnuabi_program)
    target_compile_definitions(snx_multicall PRIVATE SNX_MULTICALL_GNUABI)
    target_link_libraries(snx_multicall PUBLIC snx_gnuabi_program)
endif()
