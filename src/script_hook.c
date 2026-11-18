#include "script_hook.h"
#include "subs_dungeon_script.h"

#define LOG_CATEGORY "script_hook"

struct vec2_8 {
    int8_t x;
    int8_t y;
};

#define EVENT_AT_COORD_SIZE 16
struct vec2_8 EVENTS_AT_COORD[EVENT_AT_COORD_SIZE];
struct vec2_16 LAST_POS;
bool LAST_POS_SET = false;

static inline bool FS_IsSucceeded(volatile const struct FSFile * p_file)
{
    return (p_file->error == FS_RESULT_SUCCESS) ? true : false;
}

void LoadNewFloor() {
    MemZero(&EVENTS_AT_COORD, sizeof(EVENTS_AT_COORD));
    LAST_POS_SET = false;

    //NOTE: The FSi_*Command (FSi_TranslateCommand and FSi_SeekDirDirect, which call them) are only meant to be called in a specific context. Use FSi_SendCommand instead.
    
    char folder_buf[50];
    snprintf(folder_buf, 50, "CUSTOM/DNSCRPT/dun_%03d_floor_%02d/pos/", DUNGEON_PTR->id.val, DUNGEON_PTR->floor);

    COT_LOGFMT(LOG_CATEGORY, "listing files in %s", folder_buf);
    
    DataTransferInit();
    struct FSFile fstream;
    FS_InitFile(&fstream);

    struct FSDirPos dirpos;
    fstream.stat |= FS_FILE_STATUS_SYNC;
    if (!FSi_FindPath(&fstream, folder_buf, 0, &dirpos)|| !FS_IsSucceeded(&fstream)) {
        DataTransferStop();
        COT_LOGFMT(LOG_CATEGORY, "No position-specific script at %s", folder_buf);
        return;
    };

    fstream.arg.seekdir.pos = dirpos;
    fstream.stat |= FS_FILE_STATUS_SYNC;
    if (!FSi_SendCommand(&fstream, FS_COMMAND_SEEKDIR) || !FS_IsSucceeded(&fstream)) {
        COT_WARN(LOG_CATEGORY, "seek dir failed");
        DataTransferStop();
        return;
    };

    struct FSDirEntry direntry;
    uint8_t current_iter = 0;
    while (true) {
        fstream.arg.readdir.p_entry = &direntry;
        fstream.arg.readdir.skip_string = 0;
        fstream.stat |= FS_FILE_STATUS_SYNC;
        // Failure mean no more file to list
        if (!FSi_SendCommand(&fstream, FS_COMMAND_READDIR) || !FS_IsSucceeded(&fstream)) {
            break;
        }
        
        int x, y;
        if (Std_TsScanf(direntry.name, "%d_%d.dnscrpt", &x, &y) == 2) {
            EVENTS_AT_COORD[current_iter].x = x;
            EVENTS_AT_COORD[current_iter].y = y;
            current_iter++;
        } else {
            COT_WARNFMT(LOG_CATEGORY, "Ignored %s%s, invalid name", folder_buf, &direntry.name);
        }
        if (current_iter >= EVENT_AT_COORD_SIZE) {
            COT_WARNFMT(LOG_CATEGORY, "More than %d coord event found in %s, ignoring further ones", EVENT_AT_COORD_SIZE, folder_buf);
            break;
        }
    };
    COT_LOGFMT(LOG_CATEGORY, "%d coord events found", current_iter);
    DataTransferStop();
}

void RunScriptAtPosAsNeeeded(uint16_t x, uint16_t y) {
    if (LAST_POS_SET == true && LAST_POS.x == x && LAST_POS.y == y) {
        return;
    }
    LAST_POS_SET = true;
    LAST_POS.x = x;
    LAST_POS.y = y;
    for (int i = 0; i < EVENT_AT_COORD_SIZE; i++) {
        if (EVENTS_AT_COORD[i].x == x && EVENTS_AT_COORD[i].y == y) {
            char folder_buf[50];
            snprintf(folder_buf, 50, "dun_%03d_floor_%02d/pos/%03d_%03d", DUNGEON_PTR->id.val, DUNGEON_PTR->floor, x, y);
            RunDungeonScript(folder_buf, NULL);
        }
    }
}