#include "ui_worker.h"
#include "ui_worker_internal.h"

#include "app_state_shared.h"
#include "app_state_ui.h"
#include "app_state_worker.h"
#include "sd_browser_state.h"

#include "fatfs.h"
#include "ff.h"
#include "per/sdmmc.h"

using namespace daisy;

SdWorkerState s_sd;

bool EnsureSdMountedInternal(SdBrowserState& sd)
{
    if(!s_sd.inited)
    {
        SdmmcHandler::Config sd_cfg;
        sd_cfg.Defaults();
        // BusWidth::BITS_4 is unreliable on this carrier board's SD connector
        // (D0-D3 crosstalk) at Speed::FAST — confirmed on both the original
        // Daisy Seed and a Seed V3, even with 47pF snubber caps on CMD/D0-D3.
        // BusWidth::BITS_1 removes D0-D3 from the active bus (only CLK/CMD/D0
        // toggle), which lets every SD operation run reliably at the full
        // default 50MHz on a narrower bus. Revisit once the SD connector is
        // moved closer to the Seed on a future PCB revision.
        sd_cfg.width = SdmmcHandler::BusWidth::BITS_1;
        s_sd.sdmmc.Init(sd_cfg);
        FatFSInterface::Config fsi_cfg;
        fsi_cfg.media = FatFSInterface::Config::MEDIA_SD;
        s_sd.fsi.Init(fsi_cfg);
        s_sd.inited = true;
        sd.sd_inited = true;
    }

    FRESULT res = f_mount(&s_sd.fsi.GetSDFileSystem(), s_sd.fsi.GetSDPath(), 1);
    if(res != FR_OK)
    {
        sd.sd_ok = false;
        SdBrowser_SetStatus(sd, "SD ERR");
        return false;
    }
    sd.sd_ok = true;
    return true;
}

bool EnsureSdMounted(AppUiState& ui)
{
    return EnsureSdMountedInternal(ui.sd);
}

void CancelLoad(AppUiState& ui, AppWorkerState& worker, AppSharedState& shared)
{
    SdBrowserState& sd = ui.sd;
    if(s_sd.file_open)
    {
        f_close(&s_sd.file);
        s_sd.file_open = false;
    }
    s_sd.state = LoaderState::Idle;
    sd.load_in_progress = false;
    sd.load_progress = 0;
    SdWavLoad_SetBusy(shared, sd, false);
    ClearProjectRestoreState(worker);
}
