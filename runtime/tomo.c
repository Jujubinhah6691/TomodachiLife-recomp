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
/* RequestUpdateAudioRenderer(handle, const AudioRendererConfig *): the service renders one audio frame per update and reports
 * it in the config's output; the elapsed frame count the SDK reads back [sdk 0x2182f0] is the 64-bit word *(config + 0x40).
 * The game's audio framework waits on it before it runs its queued work (no mixing yet). */
/* The audren update, as the console's service answers it (layouts: Eden audio_core, renderer/behavior/info_updater.cpp).
 * Input (config +0x48, the SDK's own serialization): header 0x40 {revision, behaviour, memory pools, voices, voice
 * resources, effects, mixes, sinks, performance, splitter, render info sizes; total at +0x3c}, then the sections in that
 * order. Output (config +0x58, capacity +0x60): header 0x40 with the output sizes, then memory pools (0x10 each), voices
 * (0x10), effects (0x10 / 0x90 from revision 9), sinks (0x20), performance (0x10), behaviour (0xb0), render info (0x10).
 * The client [sdk 0x22116c..0x221238] checks every section size and that the total equals the capacity.
 * A SILENT renderer: no mixing; a playing voice consumes its wave buffers at its sample rate (240 samples a frame at
 * 48 kHz) so the game's streaming and sound-state logic run as on the console. */
typedef struct { uint64_t played; uint32_t consumed, popped, pos, last_index, seen; } SilentVoice;
static SilentVoice g_sv[1024];
static void audio_update(Ctx *c) {
    static int said;
    uint64_t cfg = A1;
    if (!cfg) { if (said++ < 3) printf("audio: update with no config (handle %llu)\n", (unsigned long long)A0); RET(0); return; }
    uint64_t in = rd64(cfg + 0x48), out = rd64(cfg + 0x58), cap = rd64(cfg + 0x60), frames = rd64(cfg + 0x40);
    if (!in || !out) { RET(0); return; }
    uint32_t rev = (rd32(in) >> 24) - '0';
    uint32_t vsz = rev >= 15 ? 0x188 : 0x170, esz_out = rev >= 9 ? 0x90 : 0x10;
    uint32_t npool = rd32(in + 8) / 0x20, nvoice = rd32(in + 0xc) / vsz, neffect = rd32(in + 0x14) / 0xc0, nsink = rd32(in + 0x1c) / 0x140;
    uint64_t ip = in + 0x40 + rd32(in + 4);              /* memory pools follow the behaviour parameter */
    uint64_t iv = ip + rd32(in + 8) + rd32(in + 0x10);   /* voices follow the voice channel resources */
    uint64_t ie = iv + rd32(in + 0xc);
    uint64_t total = 0x40 + npool * 0x10 + nvoice * 0x10 + neffect * esz_out + nsink * 0x20 + 0x10 + 0xb0 + 0x10;
    if (total > cap) { if (said++ < 3) printf("audio: update output needs %llu bytes, the buffer holds %llu\n", (unsigned long long)total, (unsigned long long)cap); RET(0x5299); return; }
    if (total != cap && said++ < 3) printf("audio: update output %llu bytes, buffer %llu (revision %u, %u pools %u voices %u effects %u sinks)\n", (unsigned long long)total, (unsigned long long)cap, rev, npool, nvoice, neffect, nsink);
    memset((void *)(uintptr_t)out, 0, (size_t)cap);
    uint64_t op = out + 0x40;
    for (uint32_t i = 0; i < npool; i++, op += 0x10) {   /* RequestAttach -> Attached, RequestDetach -> Detached */
        uint32_t s = rd32(ip + i * 0x20 + 0x10);
        wr32(op, s == 4 ? 5 : s == 2 ? 3 : s);
    }
    wr32(out + 8, npool * 0x10);
    for (uint32_t i = 0; i < nvoice; i++, op += 0x10) {
        uint64_t v = iv + i * vsz;
        uint32_t id = rd32(v), in_use = rd8(v + 9), play = rd8(v + 0xa), rate = rd32(v + 0xc), is_new = rd8(v + 8);
        uint32_t count = rev >= 15 ? rd32(v + 0x54) : rd32(v + 0x3c), index = rev >= 15 ? rd32(v + 0x58) : rd16(v + 0x40);
        uint64_t wb = v + (rev >= 15 ? 0x78 : 0x60);
        SilentVoice *s = &g_sv[id < 1024 ? id : 0];
        if (is_new || !s->seen) { memset(s, 0, sizeof *s); s->seen = 1; s->last_index = index; }
        if (in_use && play == 0 && !is_new) {
            s->popped += (index - s->last_index) & 3;    /* the client released that many buffers since the last update */
            s->last_index = index;
            uint32_t pending = s->consumed - s->popped, avail = count > pending ? count - pending : 0;
            uint32_t n = rate ? rate / 200 : 240;
            while (n && avail) {
                uint64_t b = wb + ((index + pending) & 3) * 0x38;
                uint32_t len = rd32(b + 0x14) - rd32(b + 0x10), left = len > s->pos ? len - s->pos : 0;
                if (left > n) { s->pos += n; s->played += n; n = 0; break; }
                s->played += left; n -= left; s->pos = 0;
                if (rd8(b + 0x18)) continue;             /* a looping buffer plays again */
                s->consumed++; pending++; avail--;
            }
        }
        wr64(op, is_new ? 0 : s->played);
        wr32(op + 8, is_new ? 0 : s->consumed);
    }
    wr32(out + 0xc, nvoice * 0x10);
    for (uint32_t i = 0; i < neffect; i++, op += esz_out) wr8(op, rd8(ie + i * 0xc0 + 2) ? 3 : 2);   /* enabled: Used, else Initialized */
    wr32(out + 0x14, neffect * esz_out);
    op += nsink * 0x20; wr32(out + 0x1c, nsink * 0x20);
    op += 0x10; wr32(out + 0x20, 0x10);                  /* performance: no history */
    op += 0xb0; wr32(out + 4, 0xb0);                     /* behaviour: no errors */
    wr64(op, frames + 1); wr32(out + 0x28, 0x10);
    wr32(out + 0x3c, (uint32_t)total);
    wr32(out, rd32(in));
    RET(0);
}
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
    {"_ZN2nn5audio26RequestUpdateAudioRendererENS0_19AudioRendererHandleEPKNS0_19AudioRendererConfigE", audio_update},
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

/* ---- SDK-internal locks, cut out of the lifted SDK code (sdklift_roots.txt): their console code ends in kernel calls */
void os_sdk_mutex_lock(uint64_t m); void os_sdk_mutex_unlock(uint64_t m); int os_sdk_mutex_trylock(uint64_t m);
int os_sdk_mutex_mine(uint64_t m); int os_sdk_cv_wait(uint64_t cv, uint64_t m, int64_t ns); void os_sdk_cv_signal(uint64_t cv, int all);
void os_sdk_obj_reset(uint64_t key);                     /* os_threads.c */
static void smx_init(Ctx *c) { os_sdk_obj_reset(A0); }
static void smx_lock(Ctx *c) { os_sdk_mutex_lock(A0); }
static void smx_unlock(Ctx *c) { os_sdk_mutex_unlock(A0); }
static void smx_trylock(Ctx *c) { RET(os_sdk_mutex_trylock(A0)); }
static void smx_mine(Ctx *c) { RET(os_sdk_mutex_mine(A0)); }
static void scv_wait(Ctx *c) { os_sdk_cv_wait(A0, A1, -1); }
static void scv_timedwait(Ctx *c) { RET(os_sdk_cv_wait(A0, A1, (int64_t)A2)); }    /* ConditionVariableStatus: 0 timeout, 1 signalled */
static void scv_signal(Ctx *c) { os_sdk_cv_signal(A0, 0); }
static void scv_broadcast(Ctx *c) { os_sdk_cv_signal(A0, 1); }
static const ImportDef sdk_lock_overrides[] = {
    {"_ZN2nn2os12SdkMutexType10InitializeEv", smx_init}, {"_ZN2nn2os12SdkMutexType4LockEv", smx_lock},
    {"_ZN2nn2os12SdkMutexType6UnlockEv", smx_unlock}, {"_ZN2nn2os12SdkMutexType7TryLockEv", smx_trylock},
    {"_ZNK2nn2os12SdkMutexType23IsLockedByCurrentThreadEv", smx_mine},
    {"_ZN2nn2os21SdkRecursiveMutexType10InitializeEv", smx_init}, {"_ZN2nn2os21SdkRecursiveMutexType4LockEv", smx_lock},
    {"_ZN2nn2os21SdkRecursiveMutexType6UnlockEv", smx_unlock}, {"_ZN2nn2os21SdkRecursiveMutexType7TryLockEv", smx_trylock},
    {"_ZNK2nn2os21SdkRecursiveMutexType23IsLockedByCurrentThreadEv", smx_mine},
    {"_ZN2nn2os24SdkConditionVariableType10InitializeEv", smx_init},
    {"_ZN2nn2os24SdkConditionVariableType4WaitERNS0_12SdkMutexTypeE", scv_wait},
    {"_ZN2nn2os24SdkConditionVariableType4WaitERNS0_21SdkRecursiveMutexTypeE", scv_wait},
    {"_ZN2nn2os24SdkConditionVariableType9TimedWaitERNS0_12SdkMutexTypeENS_8TimeSpanE", scv_timedwait},
    {"_ZN2nn2os24SdkConditionVariableType9TimedWaitERNS0_21SdkRecursiveMutexTypeENS_8TimeSpanE", scv_timedwait},
    {"_ZN2nn2os24SdkConditionVariableType6SignalEv", scv_signal},
    {"_ZN2nn2os24SdkConditionVariableType9BroadcastEv", scv_broadcast},
    /* rumble: dropped for now (nnhid.c's SDL rumble could take these) */
    {"_ZN2nn3hid18SendVibrationValueERKNS0_21VibrationDeviceHandleERKNS0_14VibrationValueE", ret0},
    {"_ZN2nn3hid6detail18SendVibrationValueERKNS0_21VibrationDeviceHandleERKNS0_14VibrationValueE", ret0},
    {"_ZN2nn3hid6detail19SendVibrationValuesEPKNS0_21VibrationDeviceHandleEPKNS0_14VibrationValueEi", ret0},
};
void sdk_host_override(const char *name, ImportFn fn);   /* sdklift_rt.c */

/* TOMO_HEAPLOG=1 (diagnosis): every failed allocation of the game's sead::ExpHeap::tryAlloc [game 0x71001e68c0]
 * (this, size, alignment) with the heap's name (sead::Heap: the name pointer after the IDisposer, +0x20) and its size. */
int hook_install(uint64_t addr, void (*repl)(Ctx *, void *), void *user, ImportFn *orig);      /* fighter_sdk/hook_host.c */
static ImportFn g_try_alloc;
static void heaplog_try_alloc(Ctx *c, void *user) {
    (void)user;
    uint64_t heap = A0, size = A1;
    uint64_t from = c->x[30];
    g_try_alloc(c);
    const char *name = "?";
    uint64_t np = rd64(heap + 0x20);
    if (np > 0x10000 && !IsBadReadPtr((void *)(uintptr_t)np, 1)) name = (const char *)(uintptr_t)np;
    if (c->x[0]) {                                       /* TOMO_HEAPLOG=<heap name>: that heap's allocations too */
        static int n;
        const char *want = getenv("TOMO_HEAPLOG");
        if (want[0] != '1' && !strcmp(name, want) && n++ < 400)
            printf("heaplog: %s +0x%llx at %llx (from %llx)\n", name, (unsigned long long)size, (unsigned long long)c->x[0], (unsigned long long)from);
        return;
    }
    static int said;
    if (said++ >= 40) return;
    printf("heaplog: %.40s (heap %llx) could not give 0x%llx bytes (alignment %d), called from %llx\n", name,
           (unsigned long long)heap, (unsigned long long)size, (int)(int32_t)A2, (unsigned long long)from);
}
/* TOMO_TASKLOG=1 (diagnosis): the scene start-up's task step [game 0x71005e1ffc] (task): every change of a task's state
 * (+0x10) or sub-state (+0x68), with its method table */
static ImportFn g_task_step;
static void tasklog_step(Ctx *c, void *user) {
    (void)user;
    uint64_t t = A0;
    g_task_step(c);
    static struct { uint64_t t; uint32_t s, s2; } seen[32];
    uint32_t s = rd32(t + 0x10), s2 = rd32(t + 0x68);
    int i = 0;
    while (i < 32 && seen[i].t && seen[i].t != t) i++;
    if (i == 32) return;
    if (seen[i].t == t && seen[i].s == s && seen[i].s2 == s2) return;
    seen[i].t = t; seen[i].s = s; seen[i].s2 = s2;
    printf("tasklog: task %llx (method table %llx) state %u sub %u\n", (unsigned long long)t, (unsigned long long)rd64(t), s, s2);
}
static void heaplog_install(void) {
    if (getenv("TOMO_TASKLOG")) printf("tasklog: hook %s\n", hook_install(0x71005e1ffcull, tasklog_step, NULL, &g_task_step) ? "installed" : "NOT installed");
    if (!getenv("TOMO_HEAPLOG")) return;
    printf("heaplog: hook %s\n", hook_install(0x71001e68c0ull, heaplog_try_alloc, NULL, &g_try_alloc) ? "installed" : "NOT installed");
}

static void ret7(Ctx *c) { RET(7); }
/* answers that replace the SSBU runtime's do-nothing ones: a do-nothing handler leaves `this` in x0, which this game reads
 * as a failed Result (nn::mii::Database::Initialize) or as a core mask */
static const ImportDef tomo_overrides[] = {
    {"_ZN2nn3mii8Database10InitializeEv", ret0},
    {"_ZN2nn4time13IsInitializedEv", ret1},
    {"_ZN2nn2os26GetThreadAvailableCoreMaskEv", ret7},
};
void register_tomo_imports(void) {
    for (size_t i = 0; i < sizeof tomo_imports / sizeof tomo_imports[0]; i++)
        if (!import_lookup(tomo_imports[i].name)) register_imports(&tomo_imports[i], 1);
    register_imports(tomo_overrides, (int)(sizeof tomo_overrides / sizeof tomo_overrides[0]));
    for (size_t i = 0; i < sizeof sdk_lock_overrides / sizeof sdk_lock_overrides[0]; i++)
        sdk_host_override(sdk_lock_overrides[i].name, sdk_lock_overrides[i].fn);
    heaplog_install();
}
