#include "velocity_layers.h"

uint8_t Velocity_SelectLayer(uint8_t vel)
{
    return (vel < 64) ? 0 : 1;
}
