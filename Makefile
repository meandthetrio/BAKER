# Project Name
TARGET = ADSR_V2
APP_TYPE = BOOT_QSPI
USE_FATFS = 1

C_INCLUDES += -I.
C_INCLUDES += -I./src/ui

# Sources
CPP_SOURCES = main.cpp
CPP_SOURCES += params.cpp
CPP_SOURCES += tilt_eq.cpp
CPP_SOURCES += audio_engine.cpp
CPP_SOURCES += ui_logic.cpp
CPP_SOURCES += ui_render.cpp
CPP_SOURCES += ui_input.cpp
CPP_SOURCES += controls.cpp
CPP_SOURCES += ui_screens.cpp
CPP_SOURCES += src/ui/ui_router.cpp
CPP_SOURCES += src/ui/ui_screen_registry.cpp
CPP_SOURCES += src/ui/ui_screen_status.cpp
CPP_SOURCES += src/ui/ui_screen_browser.cpp
CPP_SOURCES += ui_list_menu.cpp
CPP_SOURCES += ui_value_edit.cpp
CPP_SOURCES += ui_layout.cpp
CPP_SOURCES += ui_overlay.cpp
CPP_SOURCES += ui_requests.cpp
CPP_SOURCES += src/ui/project_actions.cpp
CPP_SOURCES += ui_worker.cpp
CPP_SOURCES += ui_worker_project.cpp
CPP_SOURCES += sd_browser_state.cpp
CPP_SOURCES += sd_sample_pool.cpp
CPP_SOURCES += oled_pager.cpp
CPP_SOURCES += voice_engine.cpp
CPP_SOURCES += keygroups.cpp
CPP_SOURCES += velocity_layers.cpp
CPP_SOURCES += mod_sources.cpp
CPP_SOURCES += mod_matrix.cpp
CPP_SOURCES += plocks.cpp
CPP_SOURCES += macros.cpp
CPP_SOURCES += Effects/Reverb/DattorroReverb.cpp

# Library Locations
LIBDAISY_DIR = ../../libDaisy
DAISYSP_DIR = ../../DaisySP

# Core location, and generic Makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
