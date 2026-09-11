#include "presets_at.hpp"
#include "esp32libfun_at.hpp"
#include <cstring>

// presets_at.cpp -- the application-owned "PIXELS_PRESETS" AT library.
//
// - AT+PRESETS?     -- lists occupied positions: name and segment count.
//                      Query only, never changes anything.
// - AT+PRESETSAVE   -- <name>. Exports the Composer's own confirmed scene
//                      (esp_pixels_composer::exportConfirmedScene()) and
//                      saves it under name via the bound PresetStore. An
//                      existing name is replaced deliberately; the prior
//                      valid record survives any failure. Never a staged-
//                      but-unpublished edit -- exportConfirmedScene() only
//                      ever returns the last confirmed scene.
// - AT+PRESETLOAD   -- <name>. Loads and fully validates the named preset,
//                      then applies it via esp_pixels_composer::
//                      loadConfirmedScene() -- replacing the staged table
//                      and the active scene, including global brightness/
//                      pause, and discarding any pending staged edit. A
//                      corrupt/missing/incompatible preset is rejected
//                      before anything changes.
// - AT+PRESETDEL    -- <name>. Erases the named position. Never touches the
//                      active scene or the independently retained current
//                      scene.
//
// Preset names: 1-31 printable ASCII bytes (0x20-0x7E), leading/trailing
// whitespace trimmed by this file before validation; comparison is exact,
// case-sensitive byte match (see preset_codec.hpp's own validPresetName()).
//
// Name/capacity/storage policy lives entirely here and in preset_store.*/
// preset_codec.* -- the Composer sidecar this file calls into
// (exportConfirmedScene()/loadConfirmedScene()) has no concept of names,
// positions, or NVS at all.

namespace bench {

namespace {

esp_pixels_composer::Composer *s_composer = nullptr;
PresetStore *s_store = nullptr;
bool s_enabled = false;

const char *skipSpaces(const char *text)
{
    while (text != nullptr && (*text == ' ' || *text == '\t')) {
        ++text;
    }
    return text;
}

// Trims leading/trailing spaces/tabs from `args` (a view into the AT
// console's own line buffer, never copied here -- safe because every use
// below reads it synchronously, within this same handler call). Returns
// nullptr only if `args` itself is nullptr.
const char *trimName(const char *args, size_t &out_len)
{
    const char *start = skipSpaces(args);
    if (start == nullptr) {
        out_len = 0;
        return nullptr;
    }
    size_t len = std::strlen(start);
    while (len > 0 && (start[len - 1] == ' ' || start[len - 1] == '\t')) {
        --len;
    }
    out_len = len;
    return start;
}

// --- AT+PRESETS? ------------------------------------------------------

void printPresetLine(void *context, const PresetInfo &info)
{
    ++*static_cast<unsigned *>(context);
    esp32libfun::at.writeLine("PRESET name=%s segments=%u", info.name,
                              static_cast<unsigned>(info.segment_count));
}

void atPresetsQuery(const char *args)
{
    if (*skipSpaces(args) != '\0') {
        esp32libfun::at.writeError("this command takes no arguments");
        return;
    }
    unsigned count = 0;
    s_store->list(printPresetLine, &count);
    esp32libfun::at.writeOk("PRESETS occupied=%u/%u", count, kPresetSlotCount);
}

// --- AT+PRESETSAVE=<name> ----------------------------------------------

void atPresetSave(const char *args)
{
    size_t name_len = 0;
    const char *name = trimName(args, name_len);
    if (name == nullptr || name_len == 0 || !validPresetName(name, name_len)) {
        esp32libfun::at.writeError("use AT+PRESETSAVE=<name> (1-%u printable characters)",
                                   static_cast<unsigned>(kPresetNameMaxLen));
        return;
    }

    static esp_pixels_composer::AtScene scene; // large; kept off this task's own stack
    scene = esp_pixels_composer::AtScene{};
    esp_err_t err = esp_pixels_composer::exportConfirmedScene(*s_composer, scene);
    if (err != ESP_OK) {
        esp32libfun::at.writeError("no confirmed scene to save: %s; confirm with AT+SEGPUB first",
                                   esp_err_to_name(err));
        return;
    }

    err = s_store->save(name, name_len, scene);
    if (err != ESP_OK) {
        esp32libfun::at.writeError("save failed: %s; prior record for this name unchanged",
                                   esp_err_to_name(err));
        return;
    }
    esp32libfun::at.writeOk("PRESETSAVE name=%.*s segments=%u", static_cast<int>(name_len), name,
                            static_cast<unsigned>(scene.count));
}

// --- AT+PRESETLOAD=<name> -----------------------------------------------

void atPresetLoad(const char *args)
{
    size_t name_len = 0;
    const char *name = trimName(args, name_len);
    if (name == nullptr || name_len == 0 || !validPresetName(name, name_len)) {
        esp32libfun::at.writeError("use AT+PRESETLOAD=<name>");
        return;
    }

    static esp_pixels_composer::AtScene scene; // large; kept off this task's own stack
    scene = esp_pixels_composer::AtScene{};
    esp_err_t err = s_store->load(name, name_len, scene);
    if (err != ESP_OK) {
        esp32libfun::at.writeError("preset not usable: %s; active scene unchanged", esp_err_to_name(err));
        return;
    }

    err = esp_pixels_composer::loadConfirmedScene(*s_composer, scene);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NOT_FINISHED) {
            esp32libfun::at.writeError("load indeterminate; publish outcome unknown; confirm with AT+SEGPUB");
        } else {
            esp32libfun::at.writeError("load failed: %s", esp_err_to_name(err));
        }
        return;
    }
    esp32libfun::at.writeOk("PRESETLOAD name=%.*s segments=%u", static_cast<int>(name_len), name,
                            static_cast<unsigned>(scene.count));
}

// --- AT+PRESETDEL=<name> -------------------------------------------------

void atPresetDel(const char *args)
{
    size_t name_len = 0;
    const char *name = trimName(args, name_len);
    if (name == nullptr || name_len == 0 || !validPresetName(name, name_len)) {
        esp32libfun::at.writeError("use AT+PRESETDEL=<name>");
        return;
    }
    const esp_err_t err = s_store->remove(name, name_len);
    if (err != ESP_OK) {
        esp32libfun::at.writeError("delete failed: %s", esp_err_to_name(err));
        return;
    }
    esp32libfun::at.writeOk("PRESETDEL name=%.*s removed", static_cast<int>(name_len), name);
}

// --- registration --------------------------------------------------------

struct AtCommand {
    const char *name;
    esp32libfun::at_handler_t handler;
    const char *help;
};

const AtCommand kCommands[] = {
    {"AT+PRESETS?", atPresetsQuery, "List occupied presets"},
    {"AT+PRESETSAVE", atPresetSave, "Save the confirmed scene under a name: <name>"},
    {"AT+PRESETLOAD", atPresetLoad, "Load and publish a named preset: <name>"},
    {"AT+PRESETDEL", atPresetDel, "Delete a named preset: <name>"},
};

esp_err_t unregisterCommand(const char *command)
{
    const esp_err_t err = esp32libfun::at.unregisterCmd(command);
    return (err == ESP_OK || err == ESP_ERR_NOT_FOUND) ? ESP_OK : err;
}

} // namespace

esp_err_t presetsAt(esp_pixels_composer::Composer &composer, PresetStore &store, bool enable)
{
    if (!enable) {
        if (!s_enabled || s_composer != &composer) {
            return ESP_OK; // never bound to this instance -- a safe no-op
        }
        esp_err_t result = ESP_OK;
        for (const AtCommand &command : kCommands) {
            const esp_err_t err = unregisterCommand(command.name);
            if (result == ESP_OK && err != ESP_OK) { result = err; }
        }
        s_enabled = false;
        s_composer = nullptr;
        s_store = nullptr;
        return result;
    }

    if (s_enabled) {
        // Idempotent on the exact same pair; ESP_ERR_INVALID_STATE for any
        // other pair trying to take over, same convention Composer::at(true)
        // already has for its own single binding.
        return (s_composer == &composer && s_store == &store) ? ESP_OK : ESP_ERR_INVALID_STATE;
    }
    if (!esp32libfun::at.isInitialized()) {
        return ESP_ERR_INVALID_STATE;
    }

    constexpr size_t kCount = sizeof(kCommands) / sizeof(kCommands[0]);
    size_t registered = 0;
    esp_err_t err = ESP_OK;
    for (; registered < kCount; ++registered) {
        err = esp32libfun::at.registerCmd(kCommands[registered].name, kCommands[registered].handler,
                                          kCommands[registered].help, "PIXELS_PRESETS");
        if (err != ESP_OK) { break; }
    }
    if (err != ESP_OK) {
        while (registered > 0) {
            --registered;
            (void)unregisterCommand(kCommands[registered].name);
        }
        return err;
    }

    s_composer = &composer;
    s_store = &store;
    s_enabled = true;
    return ESP_OK;
}

} // namespace bench
