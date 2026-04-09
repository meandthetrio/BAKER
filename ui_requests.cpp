#include "ui_requests.h"

#include "app_state.h"

static_assert((UiReqQueue::kCapacity & (UiReqQueue::kCapacity - 1)) == 0,
              "kUiReqQueueSize must be power-of-two");

bool UiReq_Push(AppState& app, const UiReq& r)
{
    if(r.type == UiReqType::LoadWavIndex)
    {
        app.sd.load_pending = true;
        app.sd.load_pending_index = r.a;
        app.worker.ui_req_push++;
        return true;
    }

    UiReqQueue& q = app.worker.ui_req_q;
    const uint32_t head = q.head;
    const uint32_t tail = q.tail;
    if((head - tail) >= UiReqQueue::kCapacity)
    {
        q.overflows++;
        app.worker.ui_req_ovf = q.overflows;
        return false;
    }

    q.buffer[head & (UiReqQueue::kCapacity - 1)] = r;
    q.head = head + 1;
    app.worker.ui_req_push++;
    return true;
}

bool UiReq_Pop(AppState& app, UiReq& out)
{
    UiReqQueue& q = app.worker.ui_req_q;
    const uint32_t tail = q.tail;
    if(tail == q.head)
        return false;

    out = q.buffer[tail & (UiReqQueue::kCapacity - 1)];
    q.tail = tail + 1;
    app.worker.ui_req_pop++;
    return true;
}

uint32_t UiReq_Dropped(const AppState& app)
{
    return app.worker.ui_req_q.overflows;
}

uint32_t UiReq_Fill(const AppState& app)
{
    const UiReqQueue& q = app.worker.ui_req_q;
    uint32_t fill = q.head - q.tail;
    if(fill > UiReqQueue::kCapacity)
        fill = UiReqQueue::kCapacity;
    return fill;
}
