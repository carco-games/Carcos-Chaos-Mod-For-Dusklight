#include <mods/svc/config.h>
#include <mods/svc/ui.h>
#include "d/actor/d_a_alink.h"
#include "mods/hook.hpp"
#include <mods/svc/hook.hpp>
#include "mods/svc/log.h"

#include <random>
#include <chrono>
#include <variant>

using Clock = std::chrono::steady_clock;
using DefaultValue = std::variant<bool, int64_t>;
static auto lastRun = Clock::now();

DEFINE_MOD();
IMPORT_SERVICE(LogService, svc_log);
IMPORT_SERVICE(ConfigService, svc_config);
IMPORT_SERVICE(UiService, svc_ui);
IMPORT_SERVICE(HookService, svc_hook);

namespace {
// Globals -------------------------------------
ConfigVarHandle g_cvarFastLink = 0;
ConfigVarHandle g_cvarInvisibleLink = 0;
ConfigVarHandle g_cvarMoonJump = 0;
ConfigVarHandle g_cvarSlipperyFloors = 0;
ConfigVarHandle g_cvarRandomControls = 0;
ConfigVarHandle g_cvarNoSword = 0;
ConfigVarHandle g_cvarInstaKill = 0;
ConfigVarHandle g_cvarAttackDrain = 0;

ConfigVarHandle g_cvarEnableShuffleMode = 0;
ConfigVarHandle g_cvarShuffleModeTimer = 0;
UiWindowHandle g_settingsWindow = 0;

// Effects Settings Config Vars
ConfigVarHandle g_cvarAttackDrainAmount = 0;
ConfigVarHandle g_cvarParryHealAmount = 0;
// ---------------------------------------------

// Enums ---------------------------------------
enum Effects_e {
    /* 0x0 */ FAST,
    /* 0x1 */ INVISIBLE,
    /* 0x2 */ MOON_JUMP,
    /* 0x3 */ SLIPPERY,
    /* 0x4 */ RANDOM_CONTROLS,
    /* 0x5 */ NO_SWORD,
    /* 0x6 */ INSTA_KILL,
    /* 0x7 */ ATTACK_DRAIN,
};
// ---------------------------------------------

// Hook Definitions ----------------------------
namespace hooks {
DEFINE_HOOK(&daAlink_c::setIceSlipSpeed, SetIceSlipSpeed);
DEFINE_HOOK(&daAlink_c::draw, LinkDraw);
DEFINE_HOOK_SYMBOL("duskExecute", void(), duskExecute);
DEFINE_HOOK(&daAlink_c::procMove, LinkMove);
DEFINE_HOOK(&daAlink_c::procFrontRoll, LinkFrontRoll);
DEFINE_HOOK(&daAlink_c::itemTriggerCheck, LinkItemTriggerCheck);
DEFINE_HOOK(&daAlink_c::damageMagnification, LinkDamageMagnification);
DEFINE_HOOK(&daAlink_c::setCutType, LinkSetCutType);
DEFINE_HOOK(&daAlink_c::procGuardSlipInit, LinkSlipGuard);
DEFINE_HOOK(&daAlink_c::procGuardAttackInit, LinkGuardAttackInit);
}
// ---------------------------------------------

// Structs -------------------------------------
namespace structs {
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

struct Effect {
    ConfigVarHandle& cVar;
    const char* activation_message;
    const char* continuation_message;
    void (*onActivate)() = nullptr;
};

using RegisterFunc = ModResult (*)(const char*, const DefaultValue&, ConfigVarHandle&, ModError*);
struct cVarRegistration {
    const char* name;
    DefaultValue defaultValue;
    ConfigVarHandle& cVar;
    RegisterFunc registerFunc;
};
}
// ---------------------------------------------

// Helpers -------------------------------------
namespace {
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

void randomize_mapping() {
    std::array<u8, 7> mapping = {0, 1, 2, 3, 4, 5, 6};
    static std::random_device rd;
    static std::mt19937 rng(rd());

    std::shuffle(mapping.begin(), mapping.end(), rng);
    structs::controlsMapping.BUTTON_X = mapping[0];
    structs::controlsMapping.BUTTON_Y = mapping[1];
    structs::controlsMapping.BUTTON_Z = mapping[2];
    structs::controlsMapping.BUTTON_B = mapping[3];
    structs::controlsMapping.BUTTON_A = mapping[4];
    structs::controlsMapping.BUTTON_L = mapping[5];
    structs::controlsMapping.BUTTON_R = mapping[6];
}
void randomizeControls() {
    set_bool_option(g_cvarRandomControls, true);
    randomize_mapping();
}

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
ModResult register_bool(const char* name, const DefaultValue& value, ConfigVarHandle& handle, ModError* error) {
    return register_bool_option(name, std::get<bool>(value), handle, error);
}
ModResult register_int(const char* name, const DefaultValue& value, ConfigVarHandle& handle, ModError* error) {
    return register_int_option(name, std::get<int64_t>(value), handle, error);
}
}
// ---------------------------------------------

static constexpr structs::Effect effects[] = {
    { g_cvarFastLink, "You're now fast!", "You're still fast!" },
    { g_cvarInvisibleLink, "You're now invisible!", "You're still invisible!" },
    { g_cvarMoonJump, "You can now moon jump!", "You can still moon jump!" },
    { g_cvarSlipperyFloors, "You're now slippery!", "You're still slippery!" },
    { g_cvarRandomControls, "Controls are now randomized!", "Controls are still randomized!", randomizeControls },
    { g_cvarNoSword, "Your sword would like to take a break", "Your sword is still sleepy" },
    { g_cvarInstaKill, "Hope you don't take any hits!", "You are still quite fragile"},
    {g_cvarAttackDrain, "Attacking now takes damage!", "Attacking still takes damage!"},
};

static constexpr structs::cVarRegistration registrations[] = {
    {"effectEnabled", false, g_cvarEnableShuffleMode, register_bool},
    {"shuffleModeTimer", 6000, g_cvarShuffleModeTimer, register_int},
    {"fastLink", false, g_cvarFastLink, register_bool},
    {"invisibleLink", false, g_cvarInvisibleLink, register_bool},
    {"moonJump", false, g_cvarMoonJump, register_bool},
    {"slipperyFloors", false, g_cvarSlipperyFloors, register_bool},
    {"randomControls", false, g_cvarRandomControls, register_bool},
    {"noSword", false, g_cvarNoSword, register_bool},
    {"instaKill", false, g_cvarInstaKill, register_bool},
    {"attackDrain", false, g_cvarAttackDrain, register_bool},
    {"attackDrainAmount", 1, g_cvarAttackDrainAmount, register_int},
    {"parryHealAmount", 1, g_cvarParryHealAmount, register_int},
};

// Struct Helpers ------------------------------
void activateEffect(const structs::Effect& effect, UiToastDesc toast)
{
    if (get_bool_option(effect.cVar, true)) {
        toast.body_rml = effect.continuation_message;
    } else {
        set_bool_option(effect.cVar, true);
        toast.body_rml = effect.activation_message;
    }

    // Disable all other effects.
    for (const auto& e : effects) {
        if (e.cVar != effect.cVar) {
            set_bool_option(e.cVar, false);
        }
    }

    svc_ui->push_toast(mod_ctx, &toast);
}
// ---------------------------------------------

// Hook Functions ------------------------------
namespace {
// Slippery Effect
static int slip_counter = 0;
HookAction on_set_ice_slip_speed_pre(ModContext*, void* args, void* retval, void*) {
    if (!get_bool_option(effects[SLIPPERY].cVar, false)) {
        return HOOK_CONTINUE;
    }

    daAlink_c* link = mods::arg<daAlink_c*>(args, 0);
    if (link->speed.x == 0.0f) {
        slip_counter++;
    } else {
        slip_counter = 0;
    }

    if (slip_counter == 80) {
        return HOOK_CONTINUE;
    }

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
    if (!get_bool_option(effects[INVISIBLE].cVar, false)) {
        return HOOK_CONTINUE;
    }

    return HOOK_SKIP_ORIGINAL;
}

// Fast Effect
void on_procMove_post(ModContext*, void* args, void* retval, void*) {
    if (!get_bool_option(effects[FAST].cVar, false)) {
        return;
    }

    daAlink_c* link = mods::arg<daAlink_c*>(args, 0);
    link->mMaxSpeed *= 5.7f;
    link->speedF *= 5.7f;
}
HookAction on_proc_front_roll(ModContext*, void* args, void* retval, void*) {
    if (!get_bool_option(effects[FAST].cVar, false)) {
        return HOOK_CONTINUE;
    }

    daAlink_c* link = mods::arg<daAlink_c*>(args, 0);
    link->mNormalSpeed *= 1.2f;
    return HOOK_CONTINUE;
}

// Random Controls / No Sword
HookAction on_item_trigger_check(ModContext*, void* args, void* retval, void*) {
    if (get_bool_option(effects[RANDOM_CONTROLS].cVar, false)) {
        daAlink_c* link = mods::arg<daAlink_c*>(args, 0);
        u8 i_btnFlag = mods::arg<u8>(args, 1);
        switch (i_btnFlag) {
        case daAlink_c::daAlink_ITEM_BTN::BTN_X:
            i_btnFlag = (1 << structs::controlsMapping.BUTTON_X);
            break;

        case daAlink_c::daAlink_ITEM_BTN::BTN_Y:
            i_btnFlag = (1 << structs::controlsMapping.BUTTON_Y);
            break;

        case daAlink_c::daAlink_ITEM_BTN::BTN_Z:
            i_btnFlag = (1 << structs::controlsMapping.BUTTON_Z);
            break;

        case daAlink_c::daAlink_ITEM_BTN::BTN_B:
            i_btnFlag = (1 << structs::controlsMapping.BUTTON_B);
            break;

        case daAlink_c::daAlink_ITEM_BTN::BTN_A:
            i_btnFlag = (1 << structs::controlsMapping.BUTTON_A);
            break;

        case daAlink_c::daAlink_ITEM_BTN::BTN_L:
            i_btnFlag = (1 << structs::controlsMapping.BUTTON_L);
            break;

        case daAlink_c::daAlink_ITEM_BTN::BTN_R:
            i_btnFlag = (1 << structs::controlsMapping.BUTTON_R);
            break;
        }

        link->mUseButtonFlags |= i_btnFlag;
        if (retval != nullptr) {
            *static_cast<BOOL*>(retval) = link->mItemTrigger & i_btnFlag;
        }
    } else if (get_bool_option(effects[NO_SWORD].cVar, false)) {
        u8 i_btnFlag = mods::arg<u8>(args, 1);
        if (i_btnFlag == daAlink_c::daAlink_ITEM_BTN::BTN_B) {
            return HOOK_SKIP_ORIGINAL;
        }
    } else {
        return HOOK_CONTINUE;
    }

    return HOOK_SKIP_ORIGINAL;
}

// Dusk Execute Hook
HookAction on_dusk_execute(ModContext*, void* args, void* retval, void*) {
    if (get_bool_option(effects[MOON_JUMP].cVar, false)) {
        if (mDoCPd_c::getHoldR(PAD_1) && mDoCPd_c::getHoldA(PAD_1)) {
            if (const auto link = g_dComIfG_gameInfo.play.getPlayer(0)) {
                link->speed.y = 56.0f;
            }
        }
    }

    return HOOK_CONTINUE;
}

// Insta Kill
static bool is_damage_from_attack_drain = false;
HookAction on_damage_magnification(ModContext*, void* args, void* retval, void*) {
    if (get_bool_option(effects[INSTA_KILL].cVar, false)) {
        if (retval != nullptr) {
            if (!is_damage_from_attack_drain) {
                daAlink_c* link = mods::arg<daAlink_c*>(args, 0);
                mDoAud_seStartLevel(Z2SE_OBJ_BOMB_EXPLODE, &link->current.pos, 0, 0);
                g_env_light.settingTevStruct(0, &link->current.pos, &link->tevStr);
                static const u16 explosionEffects[] = {0x161, 0x162, 0x163, 0x164, 0x165,
                                                      0x166, 0x167, 0x168, 0x1EC};
                cXyz explosionScale(1.0f, 1.0f, 1.0f);
                for (int i = 0; i < 9; i++) {
                    dComIfGp_particle_setColor(explosionEffects[i], &link->current.pos, &link->tevStr, NULL, NULL, 0.0f, 0xFF,
                                               &link->current.angle, &explosionScale, NULL, -1, NULL);
                }
                *static_cast<f32*>(retval) = 9999.0f;
            } else {
                *static_cast<f32*>(retval) = get_int_option(g_cvarAttackDrainAmount, 1);
                is_damage_from_attack_drain = false;
            }
            return HOOK_SKIP_ORIGINAL;
        }
    }

    return HOOK_CONTINUE;
}

// Attack Drain
void on_set_cut_type(ModContext*, void* args, void* retval, void*) {
    if (!get_bool_option(effects[ATTACK_DRAIN].cVar, false)) {
        return;
    }

    daAlink_c* link = mods::arg<daAlink_c*>(args, 0);
    u8 cutType = mods::arg<u8>(args, 1);
    svc_log->info(mod_ctx, std::format("Cut Type: {}", cutType).c_str());
    if (cutType == daAlink_c::daPy_CUT_TYPE::CUT_TYPE_GUARD_ATTACK) {
        return;
    }

    if (cutType != daAlink_c::daPy_CUT_TYPE::CUT_TYPE_NONE) {
        is_damage_from_attack_drain = true;
        link->setDamagePointNormal(1);
    }
}

void on_link_guard_attack_init_post(ModContext* ctx, void* args, void* retval, void* userdata) {
    daAlink_c* link = mods::arg<daAlink_c*>(args, 0);
    link->mGuardAtCps.SetAtType(AT_TYPE_SHIELD_ATTACK);
}

static int missedParryCount = 0;
HookAction on_link_slip_guard_pre(ModContext* ctx, void* args, void* retval, void* userdata) {
    daAlink_c* link = mods::arg<daAlink_c*>(args, 0);
    if (link->mProcID == daAlink_c::daAlink_PROC::PROC_GUARD_ATTACK)
    {
        dComIfGp_setItemLifeCount(get_int_option(g_cvarParryHealAmount, 1), 1);
        return HOOK_SKIP_ORIGINAL;
    } else {
        missedParryCount++;

        if (missedParryCount == 3) {
            // Create explosion effect at Link's position
            mDoAud_seStartLevel(Z2SE_OBJ_BOMB_EXPLODE, &link->current.pos, 0, 0);
            g_env_light.settingTevStruct(0, &link->current.pos, &link->tevStr);
            static const u16 explosionEffects[] = {0x161, 0x162, 0x163, 0x164, 0x165,
                                                  0x166, 0x167, 0x168, 0x1EC};
            cXyz explosionScale(1.0f, 1.0f, 1.0f);
            for (int i = 0; i < 9; i++) {
                dComIfGp_particle_setColor(explosionEffects[i], &link->current.pos, &link->tevStr, NULL, NULL, 0.0f, 0xFF,
                                           &link->current.angle, &explosionScale, NULL, -1, NULL);
            }
            dComIfGp_setItemLifeCount(-9999, 1);
            missedParryCount = 0;
        }
    }
    return HOOK_CONTINUE;
}

}
// ---------------------------------------------

// Hook Registration Helpers -------------------
namespace hook_registration {
struct HookRegistration {
    ModResult (*registerFunc)(const HookService*, ModError*);
};

template <typename Hook, HookPreFn Callback>
ModResult register_pre_hook(const HookService* svc, ModError* /*err*/)
{
    return mods::hook_add_pre<Hook>(svc, Callback);
}
template <typename Hook, HookPostFn Callback>
ModResult register_post_hook(const HookService* svc, ModError* /*err*/)
{
    return mods::hook_add_post<Hook>(svc, Callback);
}

static constexpr HookRegistration hooks[] = {
    { register_pre_hook<hooks::SetIceSlipSpeed, on_set_ice_slip_speed_pre> },
    { register_pre_hook<hooks::LinkDraw, on_draw_pre> },
    { register_post_hook<hooks::LinkMove, on_procMove_post> },
    { register_pre_hook<hooks::LinkFrontRoll, on_proc_front_roll> },
    { register_pre_hook<hooks::LinkItemTriggerCheck, on_item_trigger_check> },
    { register_pre_hook<hooks::duskExecute, on_dusk_execute> },
    { register_pre_hook<hooks::LinkDamageMagnification, on_damage_magnification> },
    { register_post_hook<hooks::LinkSetCutType, on_set_cut_type> },
    { register_pre_hook<hooks::LinkSlipGuard, on_link_slip_guard_pre> },
    { register_post_hook<hooks::LinkGuardAttackInit, on_link_guard_attack_init_post> },
};
}
// ---------------------------------------------

// UI Functions --------------------------------
namespace mod_ui {
void on_settings_window_closed(ModContext*, UiWindowHandle, void*) {
    g_settingsWindow = 0;
}

void add_control(UiElementHandle pane, const UiControlDesc& desc) {
    svc_ui->pane_add_control(mod_ctx, pane, &desc, nullptr);
}

void add_option(UiElementHandle pane, const char* label, ConfigVarHandle cvar, const char* help, UiControlKind kind) {
    UiControlDesc control = UI_CONTROL_DESC_INIT;
    control.kind = kind;
    control.label = label;
    control.help_rml = help;
    control.binding = UI_BINDING_CONFIG_VAR;
    control.config_var = cvar;
    add_control(pane, control);
}

ModResult build_effects_toggles_tab(ModContext*, UiWindowHandle, UiElementHandle left, UiElementHandle right, void*, ModError*) {
    svc_ui->pane_add_section(mod_ctx, left, "Attack Drain");
    add_option(left, "Enabled", g_cvarAttackDrain, "Attacking Drains Link's Health", UI_CONTROL_TOGGLE);

    svc_ui->pane_add_section(mod_ctx, left, "Fast Link");
    add_option(left, "Enabled", g_cvarFastLink, "Makes Link Fast", UI_CONTROL_TOGGLE);

    svc_ui->pane_add_section(mod_ctx, left, "Instant Kill");
    add_option(left, "Enabled", g_cvarInstaKill, "Makes Link Very Fragile", UI_CONTROL_TOGGLE);

    svc_ui->pane_add_section(mod_ctx, left, "Invisible Link");
    add_option(left, "Enabled", g_cvarInvisibleLink, "Makes Link Invisible", UI_CONTROL_TOGGLE);

    svc_ui->pane_add_section(mod_ctx, left, "Moon Jump");
    add_option(left, "Enabled", g_cvarMoonJump, "Allows Link To Moon Jump", UI_CONTROL_TOGGLE);

    svc_ui->pane_add_section(mod_ctx, left, "Sleepy Sword");
    add_option(left, "Enabled", g_cvarNoSword, "No Swinging That Sword!", UI_CONTROL_TOGGLE);

    svc_ui->pane_add_section(mod_ctx, left, "Slippery Floors");
    add_option(left, "Enabled", g_cvarSlipperyFloors, "Makes All Floors Slippery", UI_CONTROL_TOGGLE);

    svc_ui->pane_add_section(mod_ctx, left, "Random Controls");
    add_option(left, "Enabled", g_cvarRandomControls, "Randomizes Controls", UI_CONTROL_TOGGLE);

    return MOD_OK;
}

ModResult build_effects_settings_tab(ModContext*, UiWindowHandle, UiElementHandle left, UiElementHandle right, void*, ModError*) {
    svc_ui->pane_add_section(mod_ctx, left, "Attack Drain Settings");
    add_option(left, "Drain Amount", g_cvarAttackDrainAmount, "The amount of life attacks will drain", UI_CONTROL_NUMBER);
    add_option(left, "Parry Heal Amount", g_cvarParryHealAmount, "The amount of life parries will heal", UI_CONTROL_NUMBER);
    return MOD_OK;
}

void on_open_settings(ModContext*, void*) {
    if (g_settingsWindow != 0) {
        return;
    }
    UiTabDesc tabs[2] = {UI_TAB_DESC_INIT, UI_TAB_DESC_INIT};
    tabs[0].title = "Effect Toggles";
    tabs[0].build = build_effects_toggles_tab;
    tabs[1].title = "Effect Settings";
    tabs[1].build = build_effects_settings_tab;
    UiWindowDesc desc = UI_WINDOW_DESC_INIT;
    desc.tabs = tabs;
    desc.tab_count = 2;
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
}
// ---------------------------------------------

extern "C" {
    MOD_EXPORT ModResult mod_initialize(ModError* error) {
        svc_log->info(mod_ctx, "Initializing Challenge Mod");

        // Config Initializations
        for (auto& reg : registrations) {
            ModResult result = reg.registerFunc(reg.name, reg.defaultValue, reg.cVar, error);
            if (result != MOD_OK) {
                return result;
            }
        }

        // Hook Registrations
        for (const auto& hook : hook_registration::hooks) {
            if (hook.registerFunc(svc_hook, error) != MOD_OK)
                return MOD_ERROR;
        }

        // Build Mod Panel
        UiModsPanelDesc panelDesc = UI_MODS_PANEL_DESC_INIT;
        panelDesc.build = mod_ui::build_panel;
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
            std::uniform_int_distribution<int> dist(1, std::size(effects));
            int randomNumber = dist(rng);

            // initialize toast notification
            UiToastDesc toast = UI_TOAST_DESC_INIT;
            toast.type = "effect_notification";
            toast.title_rml = "Chaos Mod Effect";
            toast.duration_ms = 2000;

            activateEffect(effects[randomNumber - 1], toast);
        }
        return MOD_OK;
    }

    MOD_EXPORT ModResult mod_shutdown(ModError* error) {
        return MOD_OK;
    }
}
}