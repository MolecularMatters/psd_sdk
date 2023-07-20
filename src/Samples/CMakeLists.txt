# CMake build for psd_sdk samples
# Copyright 2023, heavenstone <heavenstone1308@gmail.com>
# See LICENSE.txt for licensing details (2-clause BSD License: https://opensource.org/licenses/BSD-2-Clause)

cmake_minimum_required(VERSION 3.2)

project(PsdSamples)

set(psdsamples_source
  PsdSamples.cpp
  PsdTgaExporter.h
  PsdTgaExporter.cpp
)

add_executable(${PROJECT_NAME} ${psdsamples_source})

target_link_libraries(${PROJECT_NAME} Psd)
