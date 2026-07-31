cmake_minimum_required(VERSION 3.26)
project(carcos_challenge_mod CXX)

# The Dusklight version to build against: a release tag (e.g. "v1.5.0") or a commit SHA.
if (NOT DUSKLIGHT_VERSION)
    set(DUSKLIGHT_VERSION "8caab1a6ba4aad5513bc9fe402a76dce53964e9c")
endif ()
# Fetches the Dusklight sources into ./dusklight and provides DUSKLIGHT_DIR.
# Use -DDUSKLIGHT_DIR= to point at an existing Dusklight checkout.
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/FetchDusklight.cmake")
add_subdirectory("${DUSKLIGHT_DIR}/sdk" dusklight-sdk EXCLUDE_FROM_ALL)

add_mod(carcos_challenge_mod
        FEATURES game          # remove for service/asset-only mods; add webgpu for GfxService
        SOURCES src/mod.cpp
        MOD_JSON mod.json
)
