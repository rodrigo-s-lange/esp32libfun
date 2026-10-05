#include "map_at.hpp"
#include "esp32libfun_at.hpp"
#include <cctype>
#include <cstring>

// map_at.cpp -- the application-owned "PIXELS_MAP" AT library.
//
// - AT+MAP?           -- lists the ACTIVE canvas/ledmap (what Composer is
//                        actually rendering through right now), then the
//                        durably SAVED-for-next-boot one, then the STAGED
//                        candidate (what AT+MAPSAVE would persist). Query
//                        only, never changes anything.
// - AT+MAPSET=<width>,<height>,<first>,<PROGRESSIVE|SERPENTINE|COLUMN_SERPENTINE> -- stages
//                        one whole candidate matrix descriptor, validated
//                        (positive dimensions, first+width*height in range)
//                        against whatever output topology is CURRENTLY
//                        durably saved for the next boot
//                        (OutputStore::current(), never an unsaved staged
//                        AT+OUTSET candidate) -- the topology that will
//                        actually be active the next time this map takes
//                        effect. An invalid candidate leaves the staged one
//                        exactly as it was. Stages only -- never the active
//                        map.
// - AT+MAPCLR         -- stages the identity map (no real matrix). Does
//                        NOT clear or touch the current scene, and does
//                        NOT write storage -- only AT+MAPSAVE persists.
// - AT+MAPSAVE        -- re-validates the whole staged candidate against
//                        the current OutputStore::current(), rejects it if
//                        incompatible with the durable nonempty current
//                        scene, then persists it. Takes effect on the NEXT
//                        EXPLICIT REBOOT only -- never live-replaces the
//                        running Composer::setLedmap() canvas (which
//                        cannot change once segments exist anyway). Later
//                        scene saves respect the combined saved next-boot
//                        topology AND map together (SceneStore::nextBoot()).
//                        The success response says "next reboot" explicitly.
//
// Name/capacity/storage policy lives entirely here and in map_store.*/
// map_config.* -- neither the Composer sidecar nor the adapter has any
// concept of staged/saved canvas configuration; generating the real ledmap
// table from a saved MapConfig and calling Composer::setLedmap() is
// main.cpp's own boot-time job (map_config.hpp's own generateLedmap()).

namespace bench {

namespace {

MapConfig s_active{};
MapConfig s_staged{};
MapConfig s_saved{};
MapStore *s_store = nullptr;
SceneStore *s_scene_store = nullptr;
OutputStore *s_output_store = nullptr;
bool s_enabled = false;

const char *skipSpaces(const char *text)
{
    while (text != nullptr && (*text == ' ' || *text == '\t')) {
        ++text;
    }
    return text;
}

bool atEndOfArgs(const char *text)
{
    text = skipSpaces(text);
    return text != nullptr && *text == '\0';
}

bool parseU32(const char *text, uint32_t *value_out, const char **next_out)
{
    text = skipSpaces(text);
    if (text == nullptr || *text == '\0' || !isdigit(static_cast<unsigned char>(*text))) {
        return false;
    }
    uint32_t value = 0;
    while (isdigit(static_cast<unsigned char>(*text))) {
        const uint32_t digit = static_cast<uint32_t>(*text - '0');
        if (value > ((UINT32_MAX - digit) / 10U)) {
            return false; // would overflow
        }
        value = (value * 10U) + digit;
        ++text;
    }
    *value_out = value;
    if (next_out != nullptr) {
        *next_out = text;
    }
    return true;
}

bool consumeComma(const char **cursor)
{
    const char *text = skipSpaces(*cursor);
    if (text == nullptr || *text != ',') {
        return false;
    }
    *cursor = text + 1;
    return true;
}

static constexpr const char *kWiringNames[] = {"PROGRESSIVE", "SERPENTINE", "COLUMN_SERPENTINE"};

bool parseWiring(const char *text, MapWiring *wiring_out, const char **next_out)
{
    text = skipSpaces(text);
    if (text == nullptr) {
        return false;
    }
    for (uint8_t w = 0; w < 3; ++w) {
        const char *name = kWiringNames[w];
        size_t i = 0;
        while (name[i] != '\0' && text[i] != '\0' &&
               tolower(static_cast<unsigned char>(text[i])) == tolower(static_cast<unsigned char>(name[i]))) {
            ++i;
        }
        if (name[i] == '\0' && (*skipSpaces(text + i) == '\0')) {
            *wiring_out = static_cast<MapWiring>(w);
            if (next_out != nullptr) { *next_out = text + i; }
            return true;
        }
    }
    return false;
}

const char *wiringName(MapWiring wiring)
{
    return kWiringNames[static_cast<uint8_t>(wiring)];
}

void writeMapLine(const char *label, const MapConfig &map)
{
    if (map.isIdentity()) {
        esp32libfun::at.writeLine("MAP %s identity", label);
        return;
    }
    const uint32_t total_cells = static_cast<uint32_t>(map.width) * map.height;
    esp32libfun::at.writeLine("MAP %s width=%u height=%u first=%u wiring=%s range=[%u,%u)", label,
                              static_cast<unsigned>(map.width), static_cast<unsigned>(map.height),
                              static_cast<unsigned>(map.first), wiringName(map.wiring),
                              static_cast<unsigned>(map.first), static_cast<unsigned>(map.first + total_cells));
}

// --- AT+MAP? --------------------------------------------------------------

void atMapQuery(const char *args)
{
    if (!atEndOfArgs(args)) {
        esp32libfun::at.writeError("this command takes no arguments");
        return;
    }
    writeMapLine("active", s_active);
    writeMapLine("saved", s_saved);
    writeMapLine("staged", s_staged);
    esp32libfun::at.writeOk("MAP active=%s staged=%s", s_active.isIdentity() ? "identity" : "matrix",
                            s_staged.isIdentity() ? "identity" : "matrix");
}

// --- AT+MAPSET=<width>,<height>,<first>,<wiring> -------------------------

void atMapSet(const char *args)
{
    const char *cursor = skipSpaces(args);
    uint32_t width_u = 0, height_u = 0, first_u = 0;
    MapWiring wiring = MapWiring::Progressive;
    if (!parseU32(cursor, &width_u, &cursor) || width_u == 0 || width_u > UINT16_MAX ||
        !consumeComma(&cursor) || !parseU32(cursor, &height_u, &cursor) || height_u == 0 || height_u > UINT16_MAX ||
        !consumeComma(&cursor) || !parseU32(cursor, &first_u, &cursor) || first_u > UINT16_MAX ||
        !consumeComma(&cursor) || !parseWiring(cursor, &wiring, &cursor) || !atEndOfArgs(cursor)) {
        esp32libfun::at.writeError("use AT+MAPSET=<width>,<height>,<first>,"
                                   "<PROGRESSIVE|SERPENTINE|COLUMN_SERPENTINE> "
                                   "(width/height must be > 0)");
        return;
    }

    MapConfig candidate;
    candidate.width = static_cast<uint16_t>(width_u);
    candidate.height = static_cast<uint16_t>(height_u);
    candidate.first = static_cast<uint16_t>(first_u);
    candidate.wiring = wiring;
    const uint16_t total_pixels = s_output_store->current().totalPixels();
    if (!candidate.valid(total_pixels)) {
        esp32libfun::at.writeError("rejected: first+width*height must be <= total pixels (%u); staged unchanged",
                                   static_cast<unsigned>(total_pixels));
        return;
    }
    s_staged = candidate;
    esp32libfun::at.writeOk("MAPSET width=%u height=%u first=%u wiring=%s staged",
                            static_cast<unsigned>(candidate.width), static_cast<unsigned>(candidate.height),
                            static_cast<unsigned>(candidate.first), wiringName(candidate.wiring));
}

// --- AT+MAPCLR --------------------------------------------------------------

void atMapClr(const char *args)
{
    if (!atEndOfArgs(args)) {
        esp32libfun::at.writeError("this command takes no arguments");
        return;
    }
    s_staged = MapConfig{}; // identity -- does not touch storage or the current scene
    esp32libfun::at.writeOk("MAPCLR staged=identity");
}

// --- AT+MAPSAVE -------------------------------------------------------------

void atMapSave(const char *args)
{
    if (!atEndOfArgs(args)) {
        esp32libfun::at.writeError("this command takes no arguments");
        return;
    }
    const uint16_t total_pixels = s_output_store->current().totalPixels();
    if (!s_staged.valid(total_pixels)) {
        esp32libfun::at.writeError("staged candidate invalid for the currently saved output topology; nothing saved");
        return;
    }
    if (s_scene_store->canRestoreOn(s_output_store->current(), s_staged) != ESP_OK) {
        esp32libfun::at.writeError("current saved scene is incompatible; preserve its canvas mapping "
                                   "or explicitly clear and publish the scene first");
        return;
    }
    const esp_err_t err = s_store->save(s_staged);
    if (err != ESP_OK) {
        esp32libfun::at.writeError("save failed: %s; previous saved configuration unchanged", esp_err_to_name(err));
        return;
    }
    s_saved = s_staged;
    s_scene_store->nextBoot(s_output_store->current(), s_saved);
    esp32libfun::at.writeOk("MAPSAVE %s; applied on next explicit reboot, not now",
                            s_saved.isIdentity() ? "identity" : "matrix");
}

// --- registration --------------------------------------------------------

struct AtCommand {
    const char *name;
    esp32libfun::at_handler_t handler;
    const char *help;
};

const AtCommand kCommands[] = {
    {"AT+MAP?", atMapQuery, "List active, saved and staged canvas/ledmap"},
    {"AT+MAPSET", atMapSet,
     "Stage a matrix: <width>,<height>,<first>,<PROGRESSIVE|SERPENTINE|COLUMN_SERPENTINE>"},
    {"AT+MAPCLR", atMapClr, "Stage the identity map (no real matrix)"},
    {"AT+MAPSAVE", atMapSave, "Persist the staged map for the next reboot"},
};

esp_err_t unregisterCommand(const char *command)
{
    const esp_err_t err = esp32libfun::at.unregisterCmd(command);
    return (err == ESP_OK || err == ESP_ERR_NOT_FOUND) ? ESP_OK : err;
}

} // namespace

esp_err_t mapAt(const MapConfig &active, MapStore &store, SceneStore &scene_store, OutputStore &output_store,
                bool enable)
{
    if (!enable) {
        if (!s_enabled || s_store != &store) {
            return ESP_OK; // never bound to this store -- a safe no-op
        }
        esp_err_t result = ESP_OK;
        for (const AtCommand &command : kCommands) {
            const esp_err_t err = unregisterCommand(command.name);
            if (result == ESP_OK && err != ESP_OK) { result = err; }
        }
        s_enabled = false;
        s_store = nullptr;
        s_scene_store = nullptr;
        s_output_store = nullptr;
        return result;
    }

    if (s_enabled) {
        return (s_store == &store) ? ESP_OK : ESP_ERR_INVALID_STATE;
    }
    if (!esp32libfun::at.isInitialized()) {
        return ESP_ERR_INVALID_STATE;
    }

    constexpr size_t kCount = sizeof(kCommands) / sizeof(kCommands[0]);
    size_t registered = 0;
    esp_err_t err = ESP_OK;
    for (; registered < kCount; ++registered) {
        err = esp32libfun::at.registerCmd(kCommands[registered].name, kCommands[registered].handler,
                                          kCommands[registered].help, "PIXELS_MAP");
        if (err != ESP_OK) { break; }
    }
    if (err != ESP_OK) {
        while (registered > 0) {
            --registered;
            (void)unregisterCommand(kCommands[registered].name);
        }
        return err;
    }

    s_active = active;
    s_saved = store.current();
    s_staged = active; // staged candidate starts as a copy of the active map
    s_store = &store;
    s_scene_store = &scene_store;
    s_output_store = &output_store;
    s_enabled = true;
    return ESP_OK;
}

} // namespace bench
