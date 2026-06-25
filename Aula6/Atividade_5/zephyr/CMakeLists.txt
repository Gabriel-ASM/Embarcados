# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Evita quebra de caminho com espaco ao montar CONF_FILE
set(CONF_FILE "prj.conf")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(blinky)

target_include_directories(app PRIVATE
    ../lib/pwm
    ../lib/hc_sr04
)

target_sources(app PRIVATE
    ../src/main.c
    ../lib/pwm/pwm_z42.c
    ../lib/hc_sr04/hc_sr04.c
)
