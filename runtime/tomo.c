/* Tomodachi Life (SDK 20.5.3): imports SSBU's main does not have, where the SDK code is a service call (IPC) or a
 * system answer and the lifted SDK code cannot be used. The pure SDK functions of this game are lifted instead
 * (sdklift_roots.txt, block "Tomodachi Life"). Offline-console answers as nnmisc.c gives them: success, nothing,
 * "not available". Registered after the C runtime (boot.c), so only names nobody else implements are bound here. */
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include "runtime.h"
#include "hostrt.h"

#define A0 (c->x[0])
#define A1 (c->x[1])
#define A2 (c->x[2])
#define A3 (c->x[3])
#define RET(v) (c->x[0] = (uint64_t)(v))
#define RESULT_UNAVAILABLE 0x3e87aull                    /* the "not available" Result nnmisc.c / nnsdk.c use */

static void keep(Ctx *c) { (void)c; }
static void ret0(Ctx *c) { RET(0); }
static void ret1(Ctx *c) { RET(1); }
static void unavailable(Ctx *c) { RET(RESULT_UNAVAILABLE); }
static void abort_trap(Ctx *c) { guest_trap("the game aborted (nn::diag / nn::detail abort or libc++ verbose abort)", c->x[30] - 4); }

void os_init_event(uint64_t key, int autoclear);         /* os_threads.c */

/* ---- nn::hid: GetNpadStates(State *out, int count, const NpadIdType &id): newest first; one state is given */
void hid_fill_pad_state(uint64_t out, uint32_t id, int kind);   /* nnhid.c */
static void npad_states_fullkey(Ctx *c) { if ((int)A1 < 1) { RET(0); return; } hid_fill_pad_state(A0, rd32(A2), 1); RET(1); }
static void npad_states_other(Ctx *c) { if ((int)A1 < 1) { RET(0); return; } hid_fill_pad_state(A0, rd32(A2), 0); RET(1); }
static void touch_states(Ctx *c) {                       /* TouchScreenState<16>: sampling, count, 16 touches: no touch */
    if ((int)A1 < 1) { RET(0); return; }
    memset((void *)(uintptr_t)A0, 0, 0x290);
    RET(1);
}
static void controller_support(Ctx *c) {                 /* ControllerSupportResultInfo { int playerCount; NpadIdType selectedId; } */
    wr32(A0, 1); wr32(A0 + 4, 0);
    RET(0);
}

/* ---- nn::time: ClockSnapshot and the adjustable clocks, from the host's clock (UTC as the device's time zone) */
static int64_t posix_now(void) {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return (int64_t)(((((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime) - 116444736000000000ull) / 10000000ull);
}
static void time_user_clock_now(Ctx *c) { wr64(A0, (uint64_t)posix_now()); RET(0); }
static void time_location_name(Ctx *c) { memset((void *)(uintptr_t)A0, 0, 0x24); memcpy((void *)(uintptr_t)A0, "UTC", 3); }
/* ClockSnapshot (0xd0 bytes in SDK 20, opaque to the game): our own layout, read only by the accessors below */
#define SNAP_TIME 0x00
#define SNAP_CAL 0x08                                    /* CalendarTime: year (16), month, day, hour, minute, second (8 each) */
static void time_snapshot_create(Ctx *c) {
    int64_t t = posix_now();
    uint64_t s = A0;
    memset((void *)(uintptr_t)s, 0, 0xd0);
    wr64(s + SNAP_TIME, (uint64_t)t);
    SYSTEMTIME st;
    GetSystemTime(&st);
    wr16(s + SNAP_CAL, st.wYear); wr8(s + SNAP_CAL + 2, (uint8_t)st.wMonth); wr8(s + SNAP_CAL + 3, (uint8_t)st.wDay);
    wr8(s + SNAP_CAL + 4, (uint8_t)st.wHour); wr8(s + SNAP_CAL + 5, (uint8_t)st.wMinute); wr8(s + SNAP_CAL + 6, (uint8_t)st.wSecond);
    RET(0);
}
static void snap_posix(Ctx *c) { RET(rd64(A0 + SNAP_TIME)); }
static void snap_calendar(Ctx *c) { RET(rd64(A0 + SNAP_CAL)); }     /* CalendarTime fits in x0 (8 bytes) */
static void snap_context(Ctx *c) {                       /* SystemClockContext (0x20) returned through x8 */
    uint64_t out = c->x[8];
    memset((void *)(uintptr_t)out, 0, 0x20);
    wr64(out, rd64(A0 + SNAP_TIME));
}
static void snap_location(Ctx *c) {                      /* LocationName (0x24) through x8 */
    uint64_t out = c->x[8];
    memset((void *)(uintptr_t)out, 0, 0x24);
    memcpy((void *)(uintptr_t)out, "UTC", 3);
}
static void time_difference(Ctx *c) { RET(0); }          /* TimeSpan between two snapshots of the user clock: none adjusted */
static void time_to_posix(Ctx *c) {                      /* (int *count, PosixTime *out, int max, CalendarTime const &, TimeZoneRule const &) */
    uint64_t cal = A3;
    SYSTEMTIME st = {0};
    st.wYear = rd16(cal); st.wMonth = rd8(cal + 2); st.wDay = rd8(cal + 3); st.wHour = rd8(cal + 4); st.wMinute = rd8(cal + 5); st.wSecond = rd8(cal + 6);
    FILETIME ft;
    if (!SystemTimeToFileTime(&st, &ft)) { wr32(A0, 0); RET(RESULT_UNAVAILABLE); return; }
    int64_t t = (int64_t)(((((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime) - 116444736000000000ull) / 10000000ull);
    if ((int)A2 > 0) wr64(A1, (uint64_t)t);
    wr32(A0, 1);
    RET(0);
}

/* ---- nn::fs extras */
static void fs_free_space(Ctx *c) { wr64(A0, 0x40000000ull); RET(0); }       /* 1 GiB free */
static void fs_save_size_max(Ctx *c) { wr64(A0, 0x10000000ull); wr64(A1, 0x10000000ull); RET(0); }
static void fs_save_exists(Ctx *c) { RET(1); }
static void fs_read_file_opt(Ctx *c) {                   /* ReadFile(size_t *out, handle, offset, buf, size, ReadOption): the 5-argument form */
    ImportFn f = import_lookup("_ZN2nn2fs8ReadFileEPmNS0_10FileHandleElPvm");
    if (!f) { RET(RESULT_UNAVAILABLE); return; }
    f(c);
}

/* ---- nn::mii::Database offline: no Miis in the console's database */
static void mii_db_get_index(Ctx *c) { wr32(A0, (uint32_t)-1); RET(RESULT_UNAVAILABLE); }

/* ---- nn::ngc::ProfanityFilter: no word is profane */
static void ngc_check_words(Ctx *c) { if (A0) memset((void *)(uintptr_t)A0, 0, (size_t)(uint32_t)A1 * 4); RET(0); }

/* ---- nn::swkbd::InlineKeyboard: no software keyboard; the work buffer is still sized and the image is empty */
static void swkbd_work_size(Ctx *c) { RET(0x1000); }
static void swkbd_image_req(Ctx *c) { wr64(A0, 0x1000); wr64(A1, 0x1000); }
static void swkbd_window_size(Ctx *c) { wr32(A1, 1280); wr32(A2, 720); }

/* ---- nn::prepo: the report is dropped; the "over frequency" event is one that is never signalled */
static uint8_t g_prepo_event[0x40];
static void prepo_limit_event(Ctx *c) {
    static int done;
    if (!done) { os_init_event((uint64_t)(uintptr_t)g_prepo_event, 0); done = 1; }
    RET((uint64_t)(uintptr_t)g_prepo_event);
}


/* ---- nn::audio renderer service. The configuration API is the SDK's own (lifted); these are the calls that go to the
 * audren service on the console. For now the renderer only keeps time: once started it signals the game's system event
 * every audio frame (5 ms, the console's rate), and an update is accepted without mixing (no sound yet). */
void os_signal_event(uint64_t key);                      /* os_threads.c */
typedef struct { uint64_t event; HANDLE timer; } Renderer;
static Renderer g_ren[4];
static void CALLBACK ren_tick(void *p, BOOLEAN unused) { (void)unused; os_signal_event(((Renderer *)p)->event); }
static void audio_open(Ctx *c) {                         /* (AudioRendererHandle *out, SystemEvent *event, const Parameter &, void *work, size_t) */
    int i = 0;
    while (i < 4 && g_ren[i].event) i++;
    if (i == 4) { RET(RESULT_UNAVAILABLE); return; }
    g_ren[i].event = A1;
    os_init_event(A1, 1);
    wr64(A0, (uint64_t)(i + 1));
    RET(0);
}
static Renderer *ren_of(uint64_t h) { return h >= 1 && h <= 4 ? &g_ren[h - 1] : NULL; }
static void audio_start(Ctx *c) {
    Renderer *r = ren_of(A0);
    if (r && !r->timer) CreateTimerQueueTimer(&r->timer, NULL, ren_tick, r, 5, 5, WT_EXECUTEDEFAULT);
    RET(0);
}
static void audio_stop(Ctx *c) {
    Renderer *r = ren_of(A0);
    if (r && r->timer) { DeleteTimerQueueTimer(NULL, r->timer, INVALID_HANDLE_VALUE); r->timer = NULL; }
    RET(0);
}
static void audio_close(Ctx *c) { audio_stop(c); Renderer *r = ren_of(A0); if (r) r->event = 0; }
static void audio_work_size(Ctx *c) { RET(0x100000); }
static void ret_float1(Ctx *c) { c->v[0].d[0] = 0; c->v[0].d[1] = 0; c->v[0].f[0] = 1.0f; }
static void audio_device_list(Ctx *c) {                  /* (AudioDeviceName *out, int count) -> count written; a name is 256 bytes */
    if ((int)A1 < 1) { RET(0); return; }
    memset((void *)(uintptr_t)A0, 0, 256);
    strcpy((char *)(uintptr_t)A0, "AudioTvOutput");
    RET(1);
}
static void audio_active_device(Ctx *c) { memset((void *)(uintptr_t)A0, 0, 256); strcpy((char *)(uintptr_t)A0, "AudioTvOutput"); RET(0); }
static void audio_channels(Ctx *c) { RET(2); }

static const ImportDef tomo_imports[] = {
    {"_ZN2nn5audio17OpenAudioRendererEPNS0_19AudioRendererHandleEPNS_2os11SystemEventERKNS0_22AudioRendererParameterEPvm", audio_open},
    {"_ZN2nn5audio18StartAudioRendererENS0_19AudioRendererHandleE", audio_start},
    {"_ZN2nn5audio17StopAudioRendererENS0_19AudioRendererHandleE", audio_stop},
    {"_ZN2nn5audio18CloseAudioRendererENS0_19AudioRendererHandleE", audio_close},
    {"_ZN2nn5audio26RequestUpdateAudioRendererENS0_19AudioRendererHandleEPKNS0_19AudioRendererConfigE", ret0},
    {"_ZN2nn5audio30GetAudioRendererWorkBufferSizeERKNS0_22AudioRendererParameterE", audio_work_size},
    {"_ZN2nn5audio34GetAudioRendererVoiceDropParameterENS0_19AudioRendererHandleE", ret_float1},
    {"_ZN2nn5audio25ListAudioOutputDeviceNameEPNS0_15AudioDeviceNameEi", audio_device_list},
    {"_ZN2nn5audio30GetActiveAudioOutputDeviceNameEPNS0_15AudioDeviceNameE", audio_active_device},
    {"_ZN2nn5audio26GetAudioDeviceOutputVolumeEPKNS0_15AudioDeviceNameE", ret_float1},
    {"_ZN2nn5audio41GetActiveAudioDeviceChannelCountForOutputEv", audio_channels},
    {"_ZN2nn3hid13GetNpadStatesEPNS0_16NpadFullKeyStateEiRKj", npad_states_fullkey},
    {"_ZN2nn3hid13GetNpadStatesEPNS0_16NpadJoyDualStateEiRKj", npad_states_other},
    {"_ZN2nn3hid13GetNpadStatesEPNS0_16NpadJoyLeftStateEiRKj", npad_states_other},
    {"_ZN2nn3hid13GetNpadStatesEPNS0_17NpadHandheldStateEiRKj", npad_states_other},
    {"_ZN2nn3hid13GetNpadStatesEPNS0_17NpadJoyRightStateEiRKj", npad_states_other},
    {"_ZN2nn3hid17InitializeGestureEv", keep},
    {"_ZN2nn3hid16GetGestureStatesEPNS0_12GestureStateEi", ret0},
    {"_ZN2nn3hid20GetTouchScreenStatesILm16EEEiPNS0_16TouchScreenStateIXT_EEEi", touch_states},
    {"_ZN2nn3hid22GetSixAxisSensorStatesEPNS0_18SixAxisSensorStateEiRKNS0_19SixAxisSensorHandleE", ret0},
    {"_ZN2nn3hid21ShowControllerSupportEPNS0_27ControllerSupportResultInfoERKNS0_20ControllerSupportArgE", controller_support},
    {"_ZN2nn3hid15VibrationWriter5WriteERKNS0_14VibrationValueE", keep},
    {"_ZN2nn4time25AdjustableUserSystemClock14GetCurrentTimeEPNS0_9PosixTimeE", time_user_clock_now},
    {"_ZN2nn4time25AdjustableUserSystemClock6AdjustEv", ret0},
    {"_ZN2nn4time28AdjustableNetworkSystemClock6AdjustEv", ret0},
    {"_ZN2nn4time21GetDeviceLocationNameEPNS0_12LocationNameE", time_location_name},
    {"_ZN2nn4time13ClockSnapshot31CreateWithAdjustableSystemClockEPS1_", time_snapshot_create},
    {"_ZNK2nn4time13ClockSnapshot35GetStandardUserSystemClockPosixTimeEv", snap_posix},
    {"_ZNK2nn4time13ClockSnapshot38GetStandardUserSystemClockCalendarTimeEv", snap_calendar},
    {"_ZNK2nn4time13ClockSnapshot33GetStandardUserSystemClockContextEv", snap_context},
    {"_ZNK2nn4time13ClockSnapshot15GetLocationNameEv", snap_location},
    {"_ZN2nn4time48CalculateStandardUserSystemClockDifferenceByUserERKNS0_13ClockSnapshotES3_", time_difference},
    {"_ZN2nn4time11ToPosixTimeEPiPNS0_9PosixTimeEiRKNS0_12CalendarTimeERKNS0_12TimeZoneRuleE", time_to_posix},
    {"_ZN2nn2fs16GetFreeSpaceSizeEPlPKc", fs_free_space},
    {"_ZN2nn2fs18GetSaveDataSizeMaxEPlS1_", fs_save_size_max},
    {"_ZN2nn2fs14ExtendSaveDataERKNS_7account3UidEll", ret0},
    {"_ZN2nn2fs18IsSaveDataExistingENS_13ApplicationIdERKNS_7account3UidE", fs_save_exists},
    {"_ZN2nn2fs21MountSaveDataReadOnlyEPKcNS_13ApplicationIdERKNS_7account3UidE", unavailable},
    {"_ZN2nn2fs26SetPriorityOnCurrentThreadENS0_8PriorityE", keep},
    {"_ZN2nn2fs8ReadFileEPmNS0_10FileHandleElPvmRKNS0_10ReadOptionE", fs_read_file_opt},
    {"_ZN2nn2oe13GetRegionCodeEv", ret1},                /* 1 = America */
    {"_ZN2nn2oe18GetPerformanceModeEv", ret1},           /* 1 = boost (docked) */
    {"_ZN2nn2oe28SetResumeNotificationEnabledEb", keep},
    {"_ZN2nn2oe33EnableAlbumImageTakenNotificationEv", keep},
    {"_ZN2nn2oe38SetUserInactivityDetectionTimeExtendedEb", keep},
    {"_ZN2nn2oe44SetPerformanceModeChangedNotificationEnabledEb", keep},
    {"_ZN2nn2pl16IsRunningOnOunceEv", ret0},             /* not the Switch 2 */
    {"_ZN2nn2ro12ProtectRelroEPKvS2_S2_S2_S2_", keep},
    {"_ZN2nn3ldn11SetProtocolENS0_8ProtocolE", keep},
    {"_ZN2nn3ssl10InitializeEj", ret0}, {"_ZN2nn3ssl8FinalizeEv", ret0},
    {"_ZN2nn4pctl25TryBeginFreeCommunicationEb", ret1}, {"_ZN2nn4pctl20EndFreeCommunicationEv", keep},
    {"_ZN2nn6socket10InitializeERKNS0_6ConfigE", ret0},
    {"_ZN2nn6socket12GetLastErrorEv", ret0},
    {"_ZN2nn4diag12GetBacktraceEPmi", ret0},
    {"_ZN2nn4diag13GetSymbolNameEPcmm", ret0},
    {"_ZN2nn4diag6detail9AbortImplEPKNS_6ResultE", abort_trap},
    {"_ZN2nn4diag6detail9AbortImplEv", abort_trap},
    {"_ZN2nn6detail21UnexpectedDefaultImplEv", abort_trap},
    {"_ZNSt3__122__libcpp_verbose_abortEPKcz", abort_trap},
    {"_ZN2nn3mii19IsEnabledSpecialMiiEv", ret0},
    {"_ZN2nn3mii6detail19GetMergedRegionCodeEv", ret1},
    {"_ZN2nn3mii8Database8FinalizeEv", keep},
    {"_ZNK2nn3mii8Database11IsAvailableEv", ret1},
    {"_ZNK2nn3mii8Database8GetIndexEPiRKNS0_8CharInfoE", mii_db_get_index},
    {"_ZN2nn3ngc15ProfanityFilter8FinalizeEv", ret0},
    {"_ZN2nn3ngc15ProfanityFilter17GetContentVersionEv", ret1},
    {"_ZN2nn3ngc15ProfanityFilter19CheckProfanityWordsEPjjPPKDsm", ngc_check_words},
    {"_ZN2nn3ngc15ProfanityFilter19CheckProfanityWordsEPjjPPKcm", ngc_check_words},
    {"_ZN2nn5swkbd14InlineKeyboardC1Ev", keep},
    {"_ZN2nn5swkbd14InlineKeyboard10InitializeEPv", ret1},
    {"_ZN2nn5swkbd14InlineKeyboard8FinalizeEv", keep},
    {"_ZN2nn5swkbd14InlineKeyboard25GetRequiredWorkBufferSizeEv", swkbd_work_size},
    {"_ZN2nn5swkbd14InlineKeyboard25GetImageMemoryRequirementEPmS2_", swkbd_image_req},
    {"_ZN2nn5swkbd14InlineKeyboard8GetImageEPvm", keep},
    {"_ZN2nn5swkbd14InlineKeyboard4CalcEv", keep},
    {"_ZN2nn5swkbd14InlineKeyboard6AppearERKNS0_9AppearArgE", keep},
    {"_ZN2nn5swkbd14InlineKeyboard9DisappearEv", keep},
    {"_ZN2nn5swkbd14InlineKeyboard12SetCursorPosEi", keep},
    {"_ZN2nn5swkbd14InlineKeyboard12SetInputTextERKNS0_9InputTextE", keep},
    {"_ZN2nn5swkbd14InlineKeyboard22SetMovedCursorCallbackEPFvPKNS0_14MovedCursorArgEE", keep},
    {"_ZN2nn5swkbd14InlineKeyboard23SetDecidedEnterCallbackEPFvPKNS0_15DecidedEnterArgEE", keep},
    {"_ZN2nn5swkbd14InlineKeyboard24SetChangedStringCallbackEPFvPKNS0_16ChangedStringArgEE", keep},
    {"_ZN2nn5swkbd14InlineKeyboard24SetDecidedCancelCallbackEPFvvE", keep},
    {"_ZN2nn5swkbd14InlineKeyboard27SetFinishedKeyboardCallbackEPFvvE", keep},
    {"_ZN2nn5swkbd14InlineKeyboard29SetFinishedInitializeCallbackEPFvvE", keep},
    {"_ZNK2nn5swkbd14InlineKeyboard13GetWindowSizeEPiS2_", swkbd_window_size},
    {"_ZN2nn5swkbd9InputText3SetEPKDs", keep},
    {"_ZN2nn5prepo10PlayReport4SaveEv", ret0},
    {"_ZN2nn5prepo27AddInternetConnectionStatusEPNS0_10PlayReportEPKcS4_S4_", ret0},
    {"_ZN2nn5prepo30GetOverSaveFrequencyLimitEventEv", prepo_limit_event},
};

void register_tomo_imports(void) {
    for (size_t i = 0; i < sizeof tomo_imports / sizeof tomo_imports[0]; i++)
        if (!import_lookup(tomo_imports[i].name)) register_imports(&tomo_imports[i], 1);
}
