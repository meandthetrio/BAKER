#include "ui_screens_internal.h"

const UiScreen& GetScreen(UiScreenId id)
{
    static const UiScreen start{UiScreenId::Start, nullptr, nullptr, MainMenu_OnEvent, MainMenu_Render, MainMenu_OnEnter};
    static const UiScreen presets{UiScreenId::Presets, Presets_OnEnter, nullptr, Presets_OnEvent, Presets_Render};
    static const UiScreen project_action_menu{UiScreenId::ProjectActionMenu,
                                              nullptr,
                                              nullptr,
                                              ProjectActionMenu_OnEvent,
                                              ProjectActionMenu_Render};
    static const UiScreen rename_project{UiScreenId::RenameProject,
                                         RenameProject_OnEnter,
                                         nullptr,
                                         RenameProject_OnEvent,
                                         RenameProject_Render};
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
    static const UiScreen perform_keyzone{UiScreenId::PerformKeyzone, nullptr, nullptr, PerformKeyzone_OnEvent, PerformKeyzone_Render, PerformKeyzone_OnEnter};
    static const UiScreen vel_mod{UiScreenId::VelocityMod, nullptr, nullptr, VelocityMod_OnEvent, VelocityMod_Render};
    static const UiScreen vel_mod2{UiScreenId::VelocityMod2, nullptr, nullptr, VelocityMod2_OnEvent, VelocityMod2_Render};
    static const UiScreen mod_block_a{UiScreenId::ModBlockA, nullptr, nullptr, ModBlockA_OnEvent, ModBlockA_Render};
    static const UiScreen mod_block_b{UiScreenId::ModBlockB, nullptr, nullptr, ModBlockB_OnEvent, ModBlockB_Render};
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
    static const UiScreen perform_express{UiScreenId::PerformExpress,
                                          PerformExpress_OnScreenEnter,
                                          nullptr,
                                          PerformExpress_OnEvent,
                                          PerformExpress_Render};
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
        case UiScreenId::ProjectActionMenu:
            return project_action_menu;
        case UiScreenId::RenameProject:
            return rename_project;
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
        case UiScreenId::VelocityMod:
            return vel_mod;
        case UiScreenId::VelocityMod2:
            return vel_mod2;
        case UiScreenId::ModBlockA:
            return mod_block_a;
        case UiScreenId::ModBlockB:
            return mod_block_b;
        case UiScreenId::PerformAdsr:
            return perform_adsr;
        case UiScreenId::PerformEmphasis:
            return perform_emphasis;
        case UiScreenId::PerformExpress:
            return perform_express;
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
