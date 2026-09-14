#include "outputs_at.hpp"
#include "esp32libfun_at.hpp"
#include <cctype>
#include <cstring>

// outputs_at.cpp -- the application-owned "PIXELS_OUTPUTS" AT library.
//
// - AT+OUT?          -- lists the ACTIVE topology (what the adapter is
//                       actually driving right now), then the STAGED
//                       candidate (what AT+OUTSAVE would persist). Query
//                       only, never changes anything.
// - AT+OUTSET=<slot>,<pin>,<count>,<order> -- stages one output slot.
//                       Accepts GRB/RGB/RBG/GBR/BRG/BGR.
//                       The WHOLE resulting candidate is validated (no
//                       duplicate pins, no zero count) before the edit is
//                       kept; an invalid edit leaves the candidate exactly
//                       as it was. Stages only -- never the active topology.
// - AT+OUTDEL=<slot>  -- removes one slot from the candidate, shifting any
//                       later slots down to keep it dense (Composer's own
//                       contiguous multi-output convention has no holes).
//                       Rejected if it would leave zero slots (a topology
//                       always has at least one output). Stages only.
// - AT+OUTSAVE        -- validates the whole staged candidate and persists
//                       it via the bound OutputStore. Takes effect on the
//                       NEXT EXPLICIT REBOOT only -- never live-replaces
//                       the running adapter/Composer topology, and never
//                       discards the current saved scene: incompatible candidates
//                       are rejected until the scene is explicitly cleared.
//                       Later scene saves respect the saved next-boot topology.
//                       The success response says "next reboot"
//                       explicitly.
//
// Name/capacity/storage policy lives entirely here and in output_store.*/
// output_config.* -- neither the Composer sidecar nor the adapter has any
// concept of staged/saved output configuration.

namespace bench {

namespace {

Topology s_active = defaultTopology();
Topology s_staged = defaultTopology();
OutputStore *s_store = nullptr;
SceneStore *s_scene_store = nullptr;
Topology s_saved = defaultTopology();
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

// Case-insensitive exact RGB byte-order tokens.
static constexpr const char *kOrders[] = {"GRB", "RGB", "RBG", "GBR", "BRG", "BGR"};

bool parseOrder(const char *text, uint8_t *order_out, const char **next_out)
{
    text = skipSpaces(text);
    if (text == nullptr) {
        return false;
    }
    for (uint8_t order = 0; order < kOutputOrderCount; ++order) {
        size_t i = 0;
        while (i < 3 && text[i] != '\0' &&
               tolower(static_cast<unsigned char>(text[i])) ==
               tolower(static_cast<unsigned char>(kOrders[order][i]))) { ++i; }
        if (i == 3 && (*skipSpaces(text + i) == '\0')) {
            *order_out = order;
            if (next_out != nullptr) { *next_out = text + i; }
            return true;
        }
    }
    return false;
}

const char *orderName(uint8_t order)
{
    return order < kOutputOrderCount ? kOrders[order] : "?";
}

void writeTopologyLines(const char *label, const Topology &topology)
{
    for (uint8_t i = 0; i < topology.count; ++i) {
        const OutputSpec &o = topology.outputs[i];
        esp32libfun::at.writeLine("OUT %s slot=%u pin=%d count=%u order=%s", label, static_cast<unsigned>(i),
                                  static_cast<int>(o.pin), static_cast<unsigned>(o.count), orderName(o.order));
    }
}

// --- AT+OUT? ------------------------------------------------------------

void atOutQuery(const char *args)
{
    if (!atEndOfArgs(args)) {
        esp32libfun::at.writeError("this command takes no arguments");
        return;
    }
    writeTopologyLines("active", s_active);
    writeTopologyLines("saved", s_saved);
    writeTopologyLines("staged", s_staged);
    esp32libfun::at.writeOk("OUT active=%u staged=%u", static_cast<unsigned>(s_active.count),
                            static_cast<unsigned>(s_staged.count));
}

// --- AT+OUTSET=<slot>,<pin>,<count>,<order> ------------------------------

void atOutSet(const char *args)
{
    const char *cursor = skipSpaces(args);
    uint32_t slot_u = 0;
    uint32_t pin_u = 0;
    uint32_t count_u = 0;
    uint8_t order = 0;
    if (!parseU32(cursor, &slot_u, &cursor) || slot_u >= kMaxOutputs ||
        !consumeComma(&cursor) || !parseU32(cursor, &pin_u, &cursor) || pin_u > 0x7FFFU ||
        !consumeComma(&cursor) || !parseU32(cursor, &count_u, &cursor) || count_u == 0 || count_u > 0xFFFFU ||
        !consumeComma(&cursor) || !parseOrder(cursor, &order, &cursor) || !atEndOfArgs(cursor)) {
        esp32libfun::at.writeError("use AT+OUTSET=<slot 0-%u>,<pin>,<count>,<GRB|RGB|RBG|GBR|BRG|BGR>",
                                   static_cast<unsigned>(kMaxOutputs - 1));
        return;
    }

    const uint8_t slot = static_cast<uint8_t>(slot_u);
    Topology candidate = s_staged;
    if (slot >= candidate.count) {
        candidate.count = static_cast<uint8_t>(slot + 1);
    }
    candidate.outputs[slot] = OutputSpec{static_cast<int16_t>(pin_u), static_cast<uint16_t>(count_u), order};
    if (!candidate.valid()) {
        esp32libfun::at.writeError("rejected: invalid/reserved pin, order, count or duplicate pin; staged unchanged");
        return;
    }
    s_staged = candidate;
    esp32libfun::at.writeOk("OUTSET slot=%u pin=%u count=%u order=%s staged", static_cast<unsigned>(slot),
                            static_cast<unsigned>(pin_u), static_cast<unsigned>(count_u), orderName(order));
}

// --- AT+OUTDEL=<slot> -----------------------------------------------------

void atOutDel(const char *args)
{
    const char *cursor = skipSpaces(args);
    uint32_t slot_u = 0;
    if (!parseU32(cursor, &slot_u, &cursor) || !atEndOfArgs(cursor) || slot_u >= s_staged.count) {
        esp32libfun::at.writeError("use AT+OUTDEL=<slot>, an existing staged slot");
        return;
    }
    if (s_staged.count <= 1) {
        esp32libfun::at.writeError("rejected: a topology always needs at least one output; staged candidate unchanged");
        return;
    }
    const uint8_t slot = static_cast<uint8_t>(slot_u);
    for (uint8_t i = slot; static_cast<uint8_t>(i + 1) < s_staged.count; ++i) {
        s_staged.outputs[i] = s_staged.outputs[i + 1];
    }
    --s_staged.count;
    esp32libfun::at.writeOk("OUTDEL slot=%u removed; later slots renumbered", static_cast<unsigned>(slot));
}

// --- AT+OUTSAVE ------------------------------------------------------------

void atOutSave(const char *args)
{
    if (!atEndOfArgs(args)) {
        esp32libfun::at.writeError("this command takes no arguments");
        return;
    }
    if (!s_staged.valid()) {
        esp32libfun::at.writeError("staged candidate invalid; nothing saved");
        return;
    }
    if (s_scene_store->canRestoreOn(s_staged) != ESP_OK) {
        esp32libfun::at.writeError("current saved scene is incompatible; keep its output prefix or explicitly clear and publish the scene first");
        return;
    }
    const esp_err_t err = s_store->save(s_staged);
    if (err != ESP_OK) {
        esp32libfun::at.writeError("save failed: %s; previous saved configuration unchanged", esp_err_to_name(err));
        return;
    }
    s_saved = s_staged;
    s_scene_store->nextBootTopology(s_saved);
    esp32libfun::at.writeOk("OUTSAVE outputs=%u; applied on next explicit reboot, not now",
                            static_cast<unsigned>(s_staged.count));
}

// --- registration --------------------------------------------------------

struct AtCommand {
    const char *name;
    esp32libfun::at_handler_t handler;
    const char *help;
};

const AtCommand kCommands[] = {
    {"AT+OUT?", atOutQuery, "List active and staged output topology"},
    {"AT+OUTSET", atOutSet, "Stage an output slot: <slot>,<pin>,<count>,<order>"},
    {"AT+OUTDEL", atOutDel, "Remove a staged output slot: <slot>"},
    {"AT+OUTSAVE", atOutSave, "Persist the staged topology for the next reboot"},
};

esp_err_t unregisterCommand(const char *command)
{
    const esp_err_t err = esp32libfun::at.unregisterCmd(command);
    return (err == ESP_OK || err == ESP_ERR_NOT_FOUND) ? ESP_OK : err;
}

} // namespace

esp_err_t outputsAt(const Topology &active, OutputStore &store, SceneStore &scene_store, bool enable)
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
                                          kCommands[registered].help, "PIXELS_OUTPUTS");
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
    s_saved = active;
    s_staged = active; // staged candidate starts as a copy of the active topology
    s_store = &store;
    s_scene_store = &scene_store;
    s_enabled = true;
    return ESP_OK;
}

} // namespace bench
