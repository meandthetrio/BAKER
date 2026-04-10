#pragma once

#include <cstdint>

#include "daisy_pod.h"
#include "ui_input.h"

struct AppUiState;

struct ControlsState
{
    daisy::DaisyPod* hw = nullptr;
    daisy::Encoder   ext_enc;
    daisy::Switch    rshift_btn;
    daisy::Switch    lshift_btn;
};

void Controls_Init(ControlsState& cs);
void Controls_Tick(ControlsState& cs, AppUiState& ui, uint32_t now_ms);
