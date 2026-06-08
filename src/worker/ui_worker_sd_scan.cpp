#include "ui_worker_internal.h"
#include "app_state_shared.h"
#include "sd_browser_state.h"
#include "storage_limits.h"

#include "fatfs.h"
#include "ff.h"

#include <cstdio>

using namespace daisy;

void CancelScan(SdBrowserState& sd)
{
    if(s_sd.dir_open)
    {
        f_closedir(&s_sd.dir);
        s_sd.dir_open = false;
    }
    s_sd.state = LoaderState::Idle;
    sd.scan_in_progress = false;
    sd.scan_done = true;
}

bool StartScan(SdBrowserState& sd, AppSharedState& shared, bool include_bk)
{
    SdBrowser_ClearList(sd);
    sd.scan_in_progress = true;
    sd.scan_done = false;
    SdBrowser_SetStatus(sd, "SCANNING");
    sd.load_progress = 0;
    sd.load_in_progress = false;
    SdWavLoad_SetBusy(shared, sd, false);
    s_sd.state = LoaderState::Scan;
    s_sd.scan_include_bk = include_bk;

    if(!EnsureSdMountedInternal(sd))
    {
        sd.scan_in_progress = false;
        s_sd.state = LoaderState::Idle;
        return false;
    }

    const char* base = s_sd.fsi.GetSDPath();
    std::snprintf(s_sd.scan_path, sizeof(s_sd.scan_path), "%s", base);
    if(f_opendir(&s_sd.dir, s_sd.scan_path) != FR_OK)
    {
        SdBrowser_SetStatus(sd, "DIR ERR");
        sd.scan_in_progress = false;
        s_sd.state = LoaderState::Idle;
        return false;
    }

    s_sd.dir_open = true;
    return true;
}

bool ScanStep(SdBrowserState& sd)
{
    if(!s_sd.dir_open)
        return true;

    static constexpr uint8_t kEntriesPerTick = 4;
    FILINFO fno;
    for(uint8_t i = 0; i < kEntriesPerTick; ++i)
    {
        const FRESULT res = f_readdir(&s_sd.dir, &fno);
        if(res != FR_OK)
        {
            f_closedir(&s_sd.dir);
            s_sd.dir_open = false;
            s_sd.state = LoaderState::Idle;
            sd.scan_in_progress = false;
            SdBrowser_SetStatus(sd, "SD ERR");
            return true;
        }
        if(fno.fname[0] == 0)
        {
            f_closedir(&s_sd.dir);
            s_sd.dir_open = false;
            s_sd.state = LoaderState::Idle;
            sd.scan_in_progress = false;
            sd.scan_done = true;
            if(sd.wav_count == 0)
                SdBrowser_SetStatus(sd, "NO WAV");
            else
                SdBrowser_SetStatus(sd, "");
            SdBrowser_RebuildMenu(sd);
            return true;
        }
        if(fno.fattrib & AM_DIR)
            continue;
        const bool is_wav = IsWavName(fno.fname);
        const bool is_bk  = s_sd.scan_include_bk && IsBkName(fno.fname);
        if(!is_wav && !is_bk)
            continue;
        if(sd.wav_count >= kSdMaxFiles)
        {
            f_closedir(&s_sd.dir);
            s_sd.dir_open = false;
            s_sd.state = LoaderState::Idle;
            sd.scan_in_progress = false;
            sd.scan_done = true;
            SdBrowser_SetStatus(sd, "LIST FULL");
            SdBrowser_RebuildMenu(sd);
            return true;
        }

        char path[kSdPathMax];
        if(!MakePath(path, sizeof(path), s_sd.scan_path, fno.fname))
        {
            SdBrowser_SetStatus(sd, "PATH TOO LONG");
            sd.scan_in_progress = false;
            sd.scan_done = true;
            f_closedir(&s_sd.dir);
            s_sd.dir_open = false;
            s_sd.state = LoaderState::Idle;
            SdBrowser_RebuildMenu(sd);
            return true;
        }
        if(!SdBrowser_AddWavFile(sd, fno.fname, path))
        {
            SdBrowser_SetStatus(sd, "LIST ERR");
            sd.scan_in_progress = false;
            sd.scan_done = true;
            f_closedir(&s_sd.dir);
            s_sd.dir_open = false;
            s_sd.state = LoaderState::Idle;
            SdBrowser_RebuildMenu(sd);
            return true;
        }
    }

    return false;
}
