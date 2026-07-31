#include <mods/svc/config.h>
#include <mods/svc/ui.h>
#include "d/actor/d_a_alink.h"
#include "mods/hook.hpp"
#include "mods/svc/hook.h"
#include "mods/svc/log.h"

#include <random>
#include <chrono>
using Clock = std::chrono::steady_clock;
static auto lastRun = Clock::now();

DEFINE_MOD();
IMPORT_SERVICE(LogService, svc_log);
IMPORT_SERVICE(ConfigService, svc_config);
IMPORT_SERVICE(UiService, svc_ui);
IMPORT_SERVICE(HookService, svc_hook);

namespace {
ConfigVarHandle g_cvarEnableShuffleMode = 0;
ConfigVarHandle g_cvarShuffleModeTimer = 0;
UiWindowHandle g_settingsWindow = 0;

// Effects
ConfigVarHandle g_cvarFastLink = 0;
ConfigVarHandle g_cvarInvisibleLink = 0;
ConfigVarHandle g_cvarSlipperyFloors = 0;

// Randomized Controls
ConfigVarHandle g_cvarRandomControls = 0;
struct Controls_Mapping {
    u8 BUTTON_X;
    u8 BUTTON_Y;
    u8 BUTTON_Z;
    u8 BUTTON_B;
    u8 BUTTON_A;
    u8 BUTTON_L;
    u8 BUTTON_R;
};
Controls_Mapping controlsMapping = {
    1, 1, 1, 1, 1, 1, 1
};
void randomize_mapping() {
    std::array<u8, 7> mapping = {0, 1, 2, 3, 4, 5, 6};
    static std::random_device rd;
    static std::mt19937 rng(rd());

    std::shuffle(mapping.begin(), mapping.end(), rng);
    controlsMapping.BUTTON_X = mapping[0];
    controlsMapping.BUTTON_Y = mapping[1];
    controlsMapping.BUTTON_Z = mapping[2];
    controlsMapping.BUTTON_B = mapping[3];
    controlsMapping.BUTTON_A = mapping[4];
    controlsMapping.BUTTON_L = mapping[5];
    controlsMapping.BUTTON_R = mapping[6];
}

// Get Config Var Helpers
bool get_bool_option(ConfigVarHandle handle, bool fallback) {
    bool value = fallback;
    if (handle == 0 || svc_config->get_bool(mod_ctx, handle, &value) != MOD_OK) {
        return fallback;
    }
    return value;
}
int get_int_option(ConfigVarHandle handle, int fallback) {
    int64_t value = fallback;
    if (handle == 0 || svc_config->get_int(mod_ctx, handle, &value) != MOD_OK) {
        return fallback;
    }
    return value;
}

void set_bool_option(ConfigVarHandle handle, bool value) {
    svc_config->set_bool(mod_ctx, handle, value);
}

// Hooks
// Slippery Effect
DEFINE_HOOK(&daAlink_c::setIceSlipSpeed, SetIceSlipSpeed);
// Invisibility Effect
DEFINE_HOOK(&daAlink_c::draw, LinkDraw);
// Fast Effect
DEFINE_HOOK(&daAlink_c::procMove, LinkMove);
DEFINE_HOOK(&daAlink_c::procFrontRoll, LinkFrontRoll);
// Random Controls
DEFINE_HOOK(&daAlink_c::itemTriggerCheck, LinkItemTriggerCheck);

// Hook Functions
// Slippery Effect
HookAction on_set_ice_slip_speed_pre(ModContext*, void* args, void* retval, void*) {
    if (!get_bool_option(g_cvarSlipperyFloors, false)) {
        return HOOK_CONTINUE;
    }

    daAlink_c* link = mods::arg<daAlink_c*>(args, 0);
    f32 var_f31, var_f30, var_f29;
    if (link->checkWolf()) {
        var_f31 = 0.1f;
        var_f30 = 1.0f;
        var_f29 = 0.5f;
    } else {
        var_f31 = 0.03f;
        var_f30 = 0.5f;
        var_f29 = 0.75f;
    }

    cLib_addCalc(&link->field_0x35c4.x, 0.0f, var_f31, 100.0f, var_f30);
    cLib_addCalc(&link->field_0x35c4.z, 0.0f, var_f31, 100.0f, var_f30);

    cXyz sp58 = link->field_0x3528 - link->speed;
    f32 var_f28 = sp58.absXZ();
    if (var_f28 > 30.0f) {
        sp58 *= 30.0f / var_f28;
    }

    link->field_0x35c4 += (sp58 * var_f29) * 1.6f;
    link->speed += sp58 * (1.0f - var_f29);

    if (link->mProcVar4.field_0x3010 != 0 && (link->mProcID == link->PROC_LARGE_DAMAGE_UP || link->mProcID == link->PROC_WOLF_LARGE_DAMAGE_UP)) {
        sp58.normalizeZP();
        link->field_0x35c4 += sp58 * 20.0f;
    }

    return HOOK_SKIP_ORIGINAL;
}

// Invisibility Effect
HookAction on_draw_pre(ModContext*, void* args, void* retval, void*) {
    if (!get_bool_option(g_cvarInvisibleLink, false)) {
        return HOOK_CONTINUE;
    }

    return HOOK_SKIP_ORIGINAL;
}

// Fast Effect
void on_procMove_post(ModContext*, void* args, void* retval, void*) {
    if (!get_bool_option(g_cvarFastLink, false)) {
        return;
    }

    daAlink_c* link = mods::arg<daAlink_c*>(args, 0);
    link->mMaxSpeed *= 5.5f;
    link->speedF *= 5.5f;
}

HookAction on_proc_front_roll(ModContext*, void* args, void* retval, void*) {
    if (!get_bool_option(g_cvarFastLink, false)) {
        return HOOK_CONTINUE;
    }

    daAlink_c* link = mods::arg<daAlink_c*>(args, 0);
    link->mNormalSpeed *= 1.2f;
    return HOOK_CONTINUE;
}

// Random Controls
HookAction on_item_trigger_check(ModContext*, void* args, void* retval, void*) {
    if (!get_bool_option(g_cvarRandomControls, false)) {
        return HOOK_CONTINUE;
    }

    daAlink_c* link = mods::arg<daAlink_c*>(args, 0);
    u8 i_btnFlag = mods::arg<u8>(args, 1);
    switch (i_btnFlag) {
    case daAlink_c::daAlink_ITEM_BTN::BTN_X:
        i_btnFlag = (1 << controlsMapping.BUTTON_X);
        break;

    case daAlink_c::daAlink_ITEM_BTN::BTN_Y:
        i_btnFlag = (1 << controlsMapping.BUTTON_Y);
        break;

    case daAlink_c::daAlink_ITEM_BTN::BTN_Z:
        i_btnFlag = (1 << controlsMapping.BUTTON_Z);
        break;

    case daAlink_c::daAlink_ITEM_BTN::BTN_B:
        i_btnFlag = (1 << controlsMapping.BUTTON_B);
        break;

    case daAlink_c::daAlink_ITEM_BTN::BTN_A:
        i_btnFlag = (1 << controlsMapping.BUTTON_A);
        break;

    case daAlink_c::daAlink_ITEM_BTN::BTN_L:
        i_btnFlag = (1 << controlsMapping.BUTTON_L);
        break;

    case daAlink_c::daAlink_ITEM_BTN::BTN_R:
        i_btnFlag = (1 << controlsMapping.BUTTON_R);
        break;
    }

    link->mUseButtonFlags |= i_btnFlag;
    if (retval != nullptr) {
        *static_cast<BOOL*>(retval) = link->mItemTrigger & i_btnFlag;
    }

    return HOOK_SKIP_ORIGINAL;
}

// UI Functions
void on_settings_window_closed(ModContext*, UiWindowHandle, void*) {
    g_settingsWindow = 0;
}

void add_control(UiElementHandle pane, const UiControlDesc& desc) {
    svc_ui->pane_add_control(mod_ctx, pane, &desc, nullptr);
}

void add_toggle(UiElementHandle pane, const char* label, ConfigVarHandle cvar, const char* help) {
    UiControlDesc control = UI_CONTROL_DESC_INIT;
    control.kind = UI_CONTROL_TOGGLE;
    control.label = label;
    control.help_rml = help;
    control.binding = UI_BINDING_CONFIG_VAR;
    control.config_var = cvar;
    add_control(pane, control);
}

ModResult build_settings_tab(ModContext*, UiWindowHandle, UiElementHandle left, UiElementHandle right, void*, ModError*) {
    svc_ui->pane_add_section(mod_ctx, left, "Fast Link");
    add_toggle(left, "Enabled", g_cvarFastLink, "Makes Link Fast");

    svc_ui->pane_add_section(mod_ctx, left, "Invisible Link");
    add_toggle(left, "Enabled", g_cvarInvisibleLink, "Makes Link Invisible");

    svc_ui->pane_add_section(mod_ctx, left, "Slippery Floors");
    add_toggle(left, "Enabled", g_cvarSlipperyFloors, "Makes All Floors Slippery");

    svc_ui->pane_add_section(mod_ctx, left, "Random Controls");
    add_toggle(left, "Enabled", g_cvarRandomControls, "Randomizes Controls");

    return MOD_OK;
}

void on_open_settings(ModContext*, void*) {
    if (g_settingsWindow != 0) {
        return;
    }
    UiTabDesc tabs[1] = {UI_TAB_DESC_INIT};
    tabs[0].title = "Settings";
    tabs[0].build = build_settings_tab;
    UiWindowDesc desc = UI_WINDOW_DESC_INIT;
    desc.tabs = tabs;
    desc.tab_count = 1;
    desc.on_closed = on_settings_window_closed;
    if (svc_ui->window_push(mod_ctx, &desc, &g_settingsWindow) != MOD_OK) {
        svc_log->error(mod_ctx, "Failed to open settings window");
    }
}

ModResult build_panel(ModContext*, UiElementHandle panel, void*, ModError*) {
    UiControlDesc control = UI_CONTROL_DESC_INIT;
    control.kind = UI_CONTROL_TOGGLE;
    control.label = "Enable Shuffle Mode";
    control.binding = UI_BINDING_CONFIG_VAR;
    control.config_var = g_cvarEnableShuffleMode;
    add_control(panel, control);

    control = UI_CONTROL_DESC_INIT;
    control.kind = UI_CONTROL_NUMBER;
    control.label = "Shuffle Mode Timer (milliseconds)";
    control.binding = UI_BINDING_CONFIG_VAR;
    control.config_var = g_cvarShuffleModeTimer;
    add_control(panel, control);

    control = UI_CONTROL_DESC_INIT;
    control.kind = UI_CONTROL_BUTTON;
    control.label = "Open Settings";
    control.on_pressed = on_open_settings;
    add_control(panel, control);
    return MOD_OK;
}

// Config Register Functions
ModResult register_bool_option(const char* name, bool defaultValue, ConfigVarHandle& outHandle, ModError* error) {
    ConfigVarDesc cvarDesc = CONFIG_VAR_DESC_INIT;
    cvarDesc.name = name;
    cvarDesc.type = CONFIG_VAR_BOOL;
    cvarDesc.default_bool = defaultValue;
    if (svc_config->register_var(mod_ctx, &cvarDesc, &outHandle) != MOD_OK) {
        return mods::set_error(error, MOD_ERROR, "failed to register Challenge Mod option");
    }
    return MOD_OK;
}
ModResult register_int_option(const char* name, int64_t defaultValue, ConfigVarHandle& outHandle, ModError* error) {
    ConfigVarDesc cvarDesc = CONFIG_VAR_DESC_INIT;
    cvarDesc.name = name;
    cvarDesc.type = CONFIG_VAR_INT;
    cvarDesc.default_int = defaultValue;
    if (svc_config->register_var(mod_ctx, &cvarDesc, &outHandle) != MOD_OK) {
        return mods::set_error(error, MOD_ERROR, "failed to register Challenge Mod option");
    }
    return MOD_OK;
}

extern "C" {
    MOD_EXPORT ModResult mod_initialize(ModError* error) {
        svc_log->info(mod_ctx, "Initializing Challenge Mod");

        // Config Initialization
        ModResult result = register_bool_option("effectEnabled", false, g_cvarEnableShuffleMode, error);
        if (result != MOD_OK) {
            return result;
        }
        result = register_int_option("shuffleModeTimer", 6000, g_cvarShuffleModeTimer, error);
        if (result != MOD_OK) {
            return result;
        }
        result = register_bool_option("fastLink", false, g_cvarFastLink, error);
        if (result != MOD_OK) {
            return result;
        }
        result = register_bool_option("invisibleLink", false, g_cvarInvisibleLink, error);
        if (result != MOD_OK) {
            return result;
        }
        result = register_bool_option("slipperyFloors", false, g_cvarSlipperyFloors, error);
        if (result != MOD_OK) {
            return result;
        }
        result = register_bool_option("randomControls", false, g_cvarRandomControls, error);
        if (result != MOD_OK) {
            return result;
        }

        if (mods::hook_add_pre<SetIceSlipSpeed>(svc_hook, on_set_ice_slip_speed_pre) != MOD_OK) {
            return mods::set_error(error, MOD_ERROR, "failed to hook setIceSlipSpeed");
        }
        if (mods::hook_add_pre<LinkDraw>(svc_hook, on_draw_pre) != MOD_OK) {
            return mods::set_error(error, MOD_ERROR, "failed to hook draw");
        }
        if (mods::hook_add_post<LinkMove>(svc_hook, on_procMove_post) != MOD_OK) {
            return mods::set_error(error, MOD_ERROR, "failed to hook procMove");
        }
        if (mods::hook_add_pre<LinkFrontRoll>(svc_hook, on_proc_front_roll) != MOD_OK) {
            return mods::set_error(error, MOD_ERROR, "failed to hook procFrontRoll");
        }
        if (mods::hook_add_pre<LinkItemTriggerCheck>(svc_hook, on_item_trigger_check) != MOD_OK) {
            return mods::set_error(error, MOD_ERROR, "failed to hook itemTriggerCheck");
        }

        // Build Mod Panel
        UiModsPanelDesc panelDesc = UI_MODS_PANEL_DESC_INIT;
        panelDesc.build = build_panel;
        svc_ui->register_mods_panel(mod_ctx, &panelDesc);

        return MOD_OK;
    }

    MOD_EXPORT ModResult mod_update(ModError* error) {
        auto now = Clock::now();
        if (now - lastRun >= std::chrono::milliseconds(get_int_option(g_cvarShuffleModeTimer, 60000)) &&
            get_bool_option(g_cvarEnableShuffleMode, false)
        ) {
            lastRun = now;
            // Random Number Generator
            std::random_device rd;
            std::mt19937 rng(rd());
            std::uniform_int_distribution<int> dist(1, 4);
            int randomNumber = dist(rng);

            // initialize toast notification
            UiToastDesc toast = UI_TOAST_DESC_INIT;
            toast.type = "effect_notification";
            toast.title_rml = "Challenge Mod Effect";
            toast.duration_ms = 2000;

            switch (randomNumber) {
            case 1:
                if (get_bool_option(g_cvarFastLink, true)) {
                    toast.body_rml = "You're still fast!";
                } else {
                    set_bool_option(g_cvarFastLink, true);
                    toast.body_rml = "You're now fast!";
                }
                set_bool_option(g_cvarInvisibleLink, false);
                set_bool_option(g_cvarSlipperyFloors, false);
                set_bool_option(g_cvarRandomControls, false);
                break;

            case 2:
                if (get_bool_option(g_cvarInvisibleLink, true)) {
                    toast.body_rml = "You're still invisible!";
                } else {
                    set_bool_option(g_cvarInvisibleLink, true);
                    toast.body_rml = "You're now invisible!";
                }
                set_bool_option(g_cvarFastLink, false);
                set_bool_option(g_cvarSlipperyFloors, false);
                set_bool_option(g_cvarRandomControls, false);
                break;

            case 3:
                if (get_bool_option(g_cvarSlipperyFloors, true)) {
                    toast.body_rml = "You're still slippery!";
                } else {
                    set_bool_option(g_cvarSlipperyFloors, true);
                    toast.body_rml = "You're now slippery!";
                }
                set_bool_option(g_cvarFastLink, false);
                set_bool_option(g_cvarInvisibleLink, false);
                set_bool_option(g_cvarRandomControls, false);
                break;

            case 4:
                set_bool_option(g_cvarRandomControls, true);
                randomize_mapping();
                toast.body_rml = "Controls have been randomized!";
                set_bool_option(g_cvarFastLink, false);
                set_bool_option(g_cvarInvisibleLink, false);
                set_bool_option(g_cvarSlipperyFloors, false);
            }

            svc_ui->push_toast(mod_ctx, &toast);
        }
        return MOD_OK;
    }

    MOD_EXPORT ModResult mod_shutdown(ModError* error) {

        return MOD_OK;
    }
}
}