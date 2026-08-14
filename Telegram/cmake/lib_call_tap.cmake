# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

# Tiny isolation layer: the only place raw WebRTC headers are used to tap call
# audio. Exposes a webrtc-free interface (calls/recording/call_tap.h) to the
# rest of the app, so no other target needs the private external_webrtc.

add_library(lib_call_tap STATIC)
init_target(lib_call_tap)

add_library(tdesktop::lib_call_tap ALIAS lib_call_tap)

nice_target_sources(lib_call_tap ${src_loc}/calls/recording
PRIVATE
    call_tap.cpp
    call_tap.h
)

target_include_directories(lib_call_tap
PUBLIC
    ${src_loc}
)

target_link_libraries(lib_call_tap
PUBLIC
    desktop-app::lib_webrtc
PRIVATE
    desktop-app::external_webrtc
)

target_compile_definitions(lib_call_tap
PRIVATE
    WEBRTC_APP_TDESKTOP
    RTC_ENABLE_H265
    RTC_ENABLE_VP9
)
