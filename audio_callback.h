#pragma once

#include "daisy_pod.h"

void AudioCallback(daisy::AudioHandle::InputBuffer  in,
                   daisy::AudioHandle::OutputBuffer out,
                   size_t                    size);
