#pragma once

#include <cstdint>

#include "ui_requests.h"

// Main-loop worker request lifecycle and job bookkeeping.
struct AppWorkerState
{
    UiReqQueue ui_req_q{};
    uint32_t ui_req_ovf = 0;
    uint32_t ui_req_push = 0;
    uint32_t ui_req_pop = 0;
    bool     ui_req_busy = false;
    UiReqType ui_req_active = UiReqType::None;
    uint8_t  ui_req_progress = 0;
    int8_t   ui_req_result = 0;
    uint16_t ui_req_arg0 = 0;
    uint32_t ui_req_done_count = 0;
    uint32_t ui_req_work_units_done = 0;
    uint32_t ui_req_work_units_total = 0;
};
