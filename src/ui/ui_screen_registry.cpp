#include "ui_screens_internal.h"

const UiScreen& GetScreen(UiScreenId id)
{
    static const UiScreen start{UiScreenId::Start, nullptr, nullptr, MainMenu_OnEvent, MainMenu_Render, MainMenu_OnEnter};
    static const UiScreen presets{UiScreenId::Presets, nullptr, nullptr, Presets_OnEvent, Presets_Render};
    static const UiScreen record{UiScreenId::Record, Record_OnEnter, Record_OnExit, Record_OnEvent, Record_Render};
    static const UiScreen perform_menu{UiScreenId::PerformMenu, nullptr, nullptr, PerformMenu_OnEvent, PerformMenu_Render, PerformMenu_OnEnter};
    static const UiScreen perform_engine{UiScreenId::PerformEngine,
                                         PerformEngine_OnScreenEnter,
                                         nullptr,
                                         PerformEngine_OnEvent,
                                         PerformEngine_Render,
                                         PerformEngine_OnEnter};
    static const UiScreen perform_wave_edit{UiScreenId::PerformWaveEdit,
                                            PerformWaveEdit_OnScreenEnter,
                                            nullptr,
                                            PerformWaveEdit_OnEvent,
                                            PerformWaveEdit_Render,
                                            PerformWaveEdit_OnEnter};
    static const UiScreen perform_keyzone{UiScreenId::PerformKeyzone, nullptr, nullptr, PerformKeyzone_OnEvent, PerformKeyzone_Render};
    static const UiScreen perform_adsr{UiScreenId::PerformAdsr,
                                       PerformAdsr_OnScreenEnter,
                                       nullptr,
                                       PerformAdsr_OnEvent,
                                       PerformAdsr_Render};
    static const UiScreen perform_emphasis{UiScreenId::PerformEmphasis,
                                           PerformEmphasis_OnScreenEnter,
                                           nullptr,
                                           PerformEmphasis_OnEvent,
                                           PerformEmphasis_Render};
    static const UiScreen perform_process{UiScreenId::PerformProcess, nullptr, nullptr, PerformProcess_OnEvent, PerformProcess_Render};
    static const UiScreen project_status{UiScreenId::ProjectStatus,
                                         nullptr,
                                         nullptr,
                                         ProjectStatus_OnEvent,
                                         ProjectStatus_Render};
    static const UiScreen hud{UiScreenId::Hud, nullptr, nullptr, Hud_OnEvent, Hud_Render};
    static const UiScreen fx{UiScreenId::Fx, nullptr, nullptr, Fx_OnEvent, Fx_Render};
    static const UiScreen mod{UiScreenId::Mod, nullptr, nullptr, Mod_OnEvent, Mod_Render};
    static const UiScreen macro{UiScreenId::Macro, nullptr, nullptr, Macro_OnEvent, Macro_Render};
    static const UiScreen sd{UiScreenId::SdBrowse, SdBrowse_OnEnter, nullptr, SdBrowse_OnEvent, SdBrowse_Render};
    static const UiScreen sd_del_confirm{UiScreenId::SdDeleteConfirm,
                                        nullptr,
                                        nullptr,
                                        nullptr,
                                        SdDeleteConfirm_Render,
                                        SdDeleteConfirm_OnEnter};
    static const UiScreen se{UiScreenId::SampleEdit, nullptr, nullptr, SampleEdit_OnEvent, SampleEdit_Render};
    static const UiScreen shift{UiScreenId::ShiftMenu,
                               ShiftMenu_OnScreenEnter,
                               nullptr,
                               ShiftMenu_OnEvent,
                               ShiftMenu_Render};

    switch(id)
    {
        case UiScreenId::Start:
            return start;
        case UiScreenId::Presets:
            return presets;
        case UiScreenId::Record:
            return record;
        case UiScreenId::PerformMenu:
            return perform_menu;
        case UiScreenId::PerformEngine:
            return perform_engine;
        case UiScreenId::PerformWaveEdit:
            return perform_wave_edit;
        case UiScreenId::PerformKeyzone:
            return perform_keyzone;
        case UiScreenId::PerformAdsr:
            return perform_adsr;
        case UiScreenId::PerformEmphasis:
            return perform_emphasis;
        case UiScreenId::PerformProcess:
            return perform_process;
        case UiScreenId::ProjectStatus:
            return project_status;
        case UiScreenId::Hud:
            return hud;
        case UiScreenId::Fx:
            return fx;
        case UiScreenId::Mod:
            return mod;
        case UiScreenId::Macro:
            return macro;
        case UiScreenId::SdBrowse:
            return sd;
        case UiScreenId::SdDeleteConfirm:
            return sd_del_confirm;
        case UiScreenId::SampleEdit:
            return se;
        case UiScreenId::ShiftMenu:
            return shift;
        default:
            return hud;
    }
}
