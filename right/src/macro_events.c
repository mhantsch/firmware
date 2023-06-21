#include "layer.h"
#include "string.h"
#include "str_utils.h"
#include "macro_events.h"
#include "config_parser/parse_macro.h"
#include "macros.h"
#include "keymap.h"
#include "led_display.h"
#include "debug.h"


static macro_index_t anyLayerChangeMacro = MacroIndex_None;
static macro_index_t layerChangeMacro[LayerId_Count];
static macro_index_t keymapLayerChangeMacro[LayerId_Count];

/**
 * Future possible extensions:
 * - generalize change to always handle "in" and "out" events
 * - add onKeymapLayerChange, onKeymapKeyPress, onKeyPress, onLayerChange events. These would be indexed when keymap is changed and kept at hand, so we could run them without any performance impact.
 */

/**
 * Macro events should be executed in order and wait for each other - first onInit, then `onKmeymapChange any`, finally other `onKeymapChange` ones.
 */
static uint8_t previousEventMacroSlot = 255;

void MacroEvent_OnInit()
{
    const char* s = "$onInit";
    uint8_t idx = FindMacroIndexByName(s, s + strlen(s), false);
    if (idx != 255) {
        previousEventMacroSlot = Macros_StartMacro(idx, NULL, 255, false);
    }
}

static void startMacroInSlot(macro_index_t macroIndex, uint8_t* slotId) {
    if (*slotId != 255 && MacroState[*slotId].ms.macroPlaying) {
        *slotId = Macros_QueueMacro(macroIndex, NULL, *slotId);
    } else {
        *slotId = Macros_StartMacro(macroIndex, NULL, 255, false);
    }
}

static void processOnKeymapChange(const char* curAbbrev, const char* curAbbrevEnd)
{
    for (int i = 0; i < AllMacrosCount; i++) {
        const char *thisName, *thisNameEnd;
        FindMacroName(&AllMacros[i], &thisName, &thisNameEnd);

        if (TokenMatches(thisName, thisNameEnd, "$onKeymapChange")) {
            const char* macroArg = NextTok(thisName,thisNameEnd);

            if (TokenMatches2(macroArg, thisNameEnd, curAbbrev, curAbbrevEnd)) {
                startMacroInSlot(i, &previousEventMacroSlot);
            }
        }
    }

}

void MacroEvent_OnKeymapChange(uint8_t keymapIdx)
{
    keymap_reference_t *keymap = AllKeymaps + keymapIdx;
    const char* curAbbrev = keymap->abbreviation;
    const char* curAbbrevEnd = keymap->abbreviation + keymap->abbreviationLen;

    const char* any = "any";

    processOnKeymapChange(any, any + strlen(any));
    processOnKeymapChange(curAbbrev, curAbbrevEnd);
}

void MacroEvent_RegisterLayerMacros()
{
    keymap_reference_t *keymap = AllKeymaps + CurrentKeymapIndex;
    const char* curAbbrev = keymap->abbreviation;
    const char* curAbbrevEnd = keymap->abbreviation + keymap->abbreviationLen;

    anyLayerChangeMacro = MacroIndex_None;
    memset(layerChangeMacro, MacroIndex_None, sizeof layerChangeMacro);
    memset(keymapLayerChangeMacro, MacroIndex_None, sizeof layerChangeMacro);

    for (int i = 0; i < AllMacrosCount; i++) {
        const char *thisName, *thisNameEnd;
        FindMacroName(&AllMacros[i], &thisName, &thisNameEnd);

        if (TokenMatches(thisName, thisNameEnd, "$onKeymapLayerChange")) {
            const char* macroArg = NextTok(thisName,thisNameEnd);
            const char* macroArg2 = NextTok(macroArg,thisNameEnd);
            const layer_id_t layerId = Macros_ParseLayerId(macroArg2, thisNameEnd);

            if (TokenMatches2(macroArg, thisNameEnd, curAbbrev, curAbbrevEnd)) {
                keymapLayerChangeMacro[layerId] = i;
            }
        }

        if (TokenMatches(thisName, thisNameEnd, "$onLayerChange")) {
            const char* macroArg = NextTok(thisName,thisNameEnd);
            if (TokenMatches(macroArg, thisNameEnd, "any")) {
                anyLayerChangeMacro = i;
            } else {
                const layer_id_t layerId = Macros_ParseLayerId(macroArg, thisNameEnd);
                layerChangeMacro[layerId] = i;
            }
        }
    }
}

void MacroEvent_OnLayerChange(layer_id_t layerId)
{
    macro_index_t macrosToTry[3] = {
        anyLayerChangeMacro,
        layerChangeMacro[layerId],
        keymapLayerChangeMacro[layerId],
    };

    for (uint8_t i = 0; i < sizeof macrosToTry; i++) {
        startMacroInSlot(macrosToTry[i], &previousEventMacroSlot);
    }

    previousEventMacroSlot = 255;
}
