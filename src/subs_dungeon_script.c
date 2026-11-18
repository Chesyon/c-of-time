#include "subs_dungeon_script.h"

#include <pmdsky.h>
#include <cot.h>

#include "subs_extern.h"
#include "subs_items.h"
#include "extern.h"

#define DNOPCODE_END 0x00
#define DNOPCODE_DIALOGUE 0x01
#define DNOPCODE_DIALOGUE_WAIT 0x02
#define DNOPCODE_EFFECT 0x03
#define DNOPCODE_EFFECT_WAIT 0x04
#define DNOPCODE_WAIT 0x05
#define DNOPCODE_PLAY_SFX 0x06
#define DNOPCODE_PLAY_ME 0x07
#define DNOPCODE_JUMP 0x08
#define DNOPCODE_JUMP_EQ 0x09
#define DNOPCODE_JUMP_NE 0x0A

#define DNOPCODE_YES_NO 0x10

#define DNOPCODE_CHECK_ITEM 0x20
#define DNOPCODE_GIVE_ITEM 0x21
#define DNOPCODE_TAKE_ITEM 0x22
#define DNOPCODE_CHECK_BAG_COUNT 0x23
#define DNOPCODE_CHECK_MOVE 0x2A

#define DNOPCODE_CHECK_TALK_FLAG 0x30
#define DNOPCODE_SET_TALK_FLAG 0x31

#define DNOPCODE_WAS_ATTACKED 0x42
#define DNOPCODE_CHECK_KEY_LOST 0x43
#define DNOPCODE_REMOVE_KEY_LOST 0x44
#define DNOPCODE_WARP 0x45
#define DNOPCODE_CHECK_APPARENT_SPECIES 0x46
#define DNOPCODE_SET_NEXT_FLOOR_FLAG 0x47

#define DNOPCODE_SET_ANIMATION 0x48
#define DNOPCODE_SET_DIRECTION 0x49
#define DNOPCODE_DISPLAY_ACTION 0x50
#define DNOPCODE_WALK 0x51
#define DNOPCODE_PICKITEM 0x52
#define DNOPCODE_USEMOVE 0x53
#define DNOPCODE_INITDUNGEONRNG 0x54
#define DNOPCODE_SETMONSTERMOVE 0x55

#define LOG_CATEGORY "dungeon_script"

static inline void WaitFrames(int n) {
  for (int i = 0; i < n; i++) {
    AdvanceFrame(0);
  }
}

struct ScriptEngineState {
  void* ip; // Instruction pointer
  struct entity* npc_monster;
  bool condition_flag;
};

struct ScriptEngineString {
  uint16_t length;
  char chars[];
} __attribute__((packed));

struct DungeonScriptBuffer {
  void* data; // Must be released with `MemFree`!
  int size;
};

static bool LoadDungeonScriptToBuffer(char* name, struct DungeonScriptBuffer* buffer) {
  struct FSFile file;
  char file_name_buffer[128];
  snprintf(file_name_buffer, 128, "CUSTOM/DNSCRPT/%s.dnscrpt", name);
  DataTransferInit();
  FS_InitFile(&file);
  if (!FS_OpenFile(&file, file_name_buffer)) {
      DataTransferStop();
      COT_WARNFMT(LOG_CATEGORY, "Failed to load dungeon script '%s' (open failed, probably does not exist)", file_name_buffer);
      return false;
  }

  int size = FileGetSize(&file);
  void* script_buffer = MemAlloc(size, 0);

  int read_bytes = FileRead(&file, script_buffer, size);
  FileClose(&file);
  DataTransferStop();
  if (read_bytes <= 0) {
    COT_ERRORFMT(LOG_CATEGORY, "Failed to load dungeon script '%s' (0 bytes read)", file_name_buffer);
    MemFree(script_buffer);
    return false;
  }

  buffer->data = script_buffer;
  buffer->size = size;
  return true;
}

static struct entity* GetScriptCharacterByIndex(struct ScriptEngineState* state, uint8_t index) {  
  if (index == 255 && state->npc_monster != NULL) {
    return state->npc_monster;
  }

  struct entity* entity = NULL;
  if (index > 0 && index <= 20) {
    entity = DUNGEON_PTR->entity_table.header.active_monster_ptrs[index - 1];
  } else if (index == 240) {
      // special case: monster 240 map to monster of specie id 230
      for (int i = 0; i < 20; i=i+1) {
          if (DUNGEON_PTR->entity_table.header.active_monster_ptrs[i] != NULL) {
              struct entity* local_entity = DUNGEON_PTR->entity_table.header.active_monster_ptrs[i];
              struct monster* local_monster = local_entity->info.monster;
              if (local_monster->id.val == 230) {
                  entity = local_entity;
              }
          }
      }
  } else {
      return NULL;
  }

  if (entity == NULL) {
      return NULL;
  }

  if (EntityIsValid(entity)) {
    return entity;
  } else {
    return NULL;
  }
}

// Note: This function returns the exit code, while all other
// functions return the number of bytes consumed.
static int HandleOpEnd(struct ScriptEngineState* state) {
  struct OpEnd {
    uint8_t exit_code;
  } __attribute__((packed));

  struct OpEnd* op = (struct OpEnd*)state->ip;

  COT_LOGFMT(LOG_CATEGORY, "OpEnd: exit_code=%d", op->exit_code);

  return op->exit_code;
}

static int HandleOpDialogue(struct ScriptEngineState* state, bool wait) {
  struct OpDialogue {
    uint8_t chara;
    uint8_t portrait;
    struct ScriptEngineString string;
  } __attribute__((packed));

  struct OpDialogue* op = (struct OpDialogue*)state->ip;

  if (wait) {
    COT_LOGFMT(LOG_CATEGORY, "OpDialogueWait: chara=%d portrait=%d string=%s", op->chara, op->portrait, op->string.chars);
  } else {
    COT_LOGFMT(LOG_CATEGORY, "OpDialogue: chara=%d portrait=%d string=%s", op->chara, op->portrait, op->string.chars);
  }

  struct entity* entity = GetScriptCharacterByIndex(state, op->chara);

  if (entity != NULL) {
    struct monster* monster = entity->info.monster;
    struct portrait_params portrait_box;
    InitPortraitDungeon(&portrait_box, monster->id.val, op->portrait);
    PrintDialogue(NULL, &portrait_box, op->string.chars, wait);
  } else {
    PrintDialogue(NULL, NULL, op->string.chars, wait);
  }

  return sizeof(struct OpDialogue) + op->string.length;
}

static int HandleOpEffect(struct ScriptEngineState* state, bool wait) {
  struct OpEffect {
    uint8_t chara;
    uint16_t effect;
  } __attribute__((packed));

  struct OpEffect* op = (struct OpEffect*)state->ip;

  if (wait) {
    COT_LOGFMT(LOG_CATEGORY, "OpEffectWait: chara=%d effect=%d", op->chara, op->effect);
  } else {
    COT_LOGFMT(LOG_CATEGORY, "OpEffect: chara=%d effect=%d", op->chara, op->effect);
  }

  struct entity* entity = GetScriptCharacterByIndex(state, op->chara);
  int wan_offset = GetEffectAnimationWanOffset(op->effect);
  PlayEffectAnimationEntity(entity, op->effect, wait, wan_offset && 0xff, 2, 0, -1, NULL);

  return sizeof(struct OpEffect);
}

static int HandleOpPlaySfx(struct ScriptEngineState* state) {
  struct OpPlaySfx {
    uint16_t sfx_id;
  } __attribute__((packed));

  struct OpPlaySfx* op = (struct OpPlaySfx*)state->ip;

  COT_LOGFMT(LOG_CATEGORY, "OpPlaySfx: sfx_id=%d", op->sfx_id);

  PlaySe(op->sfx_id, 0x100);

  return sizeof(struct OpPlaySfx);
}

static int HandleOpPlayMe(struct ScriptEngineState* state) {
  struct OpPlayMe {
    uint16_t me_id;
  } __attribute__((packed));

  struct OpPlayMe* op = (struct OpPlayMe*)state->ip;

  COT_LOGFMT(LOG_CATEGORY, "OpPlayMe: me_id=%d", op->me_id);

  PlayME(op->me_id, 0x100, 0);

  return sizeof(struct OpPlayMe);
}

static int HandleOpWait(struct ScriptEngineState* state) {
  struct OpWait {
    uint16_t wait_frames;
  } __attribute__((packed));

  struct OpWait* op = (struct OpWait*)state->ip;

  COT_LOGFMT(LOG_CATEGORY, "OpWait: wait_frames=%d", op->wait_frames);
  WaitFrames(op->wait_frames);

  return sizeof(struct OpWait);
}

static int HandleOpJump(struct ScriptEngineState* state) {
  struct OpJump {
    int32_t jump_offset;
  } __attribute__((packed));

  struct OpJump* op = (struct OpJump*)state->ip;

  COT_LOGFMT(LOG_CATEGORY, "OpJump: jump_offset=%d", op->jump_offset);

  return op->jump_offset - 1; // -1 because the ip has already been incremented
}

static int HandleOpJumpConditional(struct ScriptEngineState* state, bool condition) {
  struct OpJumpConditional {
    int32_t jump_offset;
  } __attribute__((packed));

  struct OpJumpConditional* op = (struct OpJumpConditional*)state->ip;

  if (condition) {
    COT_LOGFMT(LOG_CATEGORY, "OpJumpEq: jump_offset=%d", op->jump_offset);
  } else {
    COT_LOGFMT(LOG_CATEGORY, "OpJumpNe: jump_offset=%d", op->jump_offset);
  }

  if (state->condition_flag == condition) {
    return op->jump_offset - 1; // -1 because the ip has already been incremented
  } else {
    return sizeof(struct OpJumpConditional);
  }
}

static int HandleOpYesNo(struct ScriptEngineState* state) {
  struct OpYesNo {
    uint8_t chara;
    uint8_t portrait;
    uint8_t default_val;
    struct ScriptEngineString string;
  } __attribute__((packed));

  struct OpYesNo* op = (struct OpYesNo*)state->ip;

  COT_LOGFMT(LOG_CATEGORY, "OpYesNo: chara=%d portrait=%d default=%d string=%s", op->chara, op->portrait, op->default_val, op->string.chars);

  struct entity* entity = GetScriptCharacterByIndex(state, op->chara);
  if (entity != NULL) {
    struct monster* monster = entity->info.monster;
    struct portrait_params portrait_box;
    InitPortraitDungeon(&portrait_box, monster->id.val, op->portrait);
    bool res = YesNoMenuWithStringPtr(&portrait_box, op->string.chars, op->default_val, 0, op->default_val) == 1;
    state->condition_flag = res;
  } else {
    bool res = YesNoMenuWithStringPtr(NULL, op->string.chars, op->default_val, 0, op->default_val) == 1;
    state->condition_flag = res;
  }

  return sizeof(struct OpYesNo) + op->string.length;
}

static int HandleOpCheckItem(struct ScriptEngineState* state) {
  struct OpCheckItem {
    uint16_t item_id;
    uint8_t amount;
  } __attribute__((packed));

  struct OpCheckItem* op = (struct OpCheckItem*)state->ip;

  COT_LOGFMT(LOG_CATEGORY, "OpCheckItem: item_id=%d amount=%d", op->item_id, op->amount);

  state->condition_flag = HasItemAmount(op->item_id, op->amount);

  return sizeof(struct OpCheckItem);
}

static int HandleOpGiveItem(struct ScriptEngineState* state) {
  struct OpGiveItem {
    uint16_t item_id;
    uint8_t amount;
  } __attribute__((packed));

  struct OpGiveItem* op = (struct OpGiveItem*)state->ip;

  COT_LOGFMT(LOG_CATEGORY, "OpGiveItem: item_id=%d amount=%d", op->item_id, op->amount);

  state->condition_flag = GiveItem(op->item_id, op->amount);

  return sizeof(struct OpGiveItem);
}

static int HandleOpTakeItem(struct ScriptEngineState* state) {
  struct OpTakeItem {
    uint16_t item_id;
  } __attribute__((packed));

  struct OpTakeItem* op = (struct OpTakeItem*)state->ip;

  COT_LOGFMT(LOG_CATEGORY, "OpTakeItem: item_id=%d", op->item_id);

  state->condition_flag = TakeItem(op->item_id);

  return sizeof(struct OpTakeItem);
}

static int HandleOpCheckBagCount(struct ScriptEngineState* state) {
  struct OpCheckBagCount {
    uint8_t amount;
  } __attribute__((packed));

  struct OpCheckBagCount* op = (struct OpCheckBagCount*)state->ip;

  COT_LOGFMT(LOG_CATEGORY, "OpCheckBagCount: amount=%d", op->amount);

  state->condition_flag = GetBagItemCount() >= op->amount;

  return sizeof(struct OpCheckBagCount);
}

static int HandleOpCheckMove(struct ScriptEngineState* state) {
  struct OpCheckMove {
    uint8_t chara;
    uint16_t move_id;
  } __attribute__((packed));

  struct OpCheckMove* op = (struct OpCheckMove*)state->ip;

  COT_LOGFMT(LOG_CATEGORY, "OpCheckMove: chara=%d move_id=%d", op->chara, op->move_id);

  state->condition_flag = false;
  struct entity* entity = GetScriptCharacterByIndex(state, op->chara);
  if (entity) {
    struct monster* monster = entity->info.monster;
    for (int i = 0; i < 4; i++) {
      struct move* move = &monster->moves[i];
      if (move->f_exists && move->id.val == op->move_id) {
        state->condition_flag = true;
        break;
      }
    }
  }

  return sizeof(struct OpCheckMove);
}

static int HandleOpCheckTalkFlag(struct ScriptEngineState* state) {
  struct OpCheckTalkFlag {
    uint8_t index;
  } __attribute__((packed));

  struct OpCheckTalkFlag* op = (struct OpCheckTalkFlag*)state->ip;

  COT_LOGFMT(LOG_CATEGORY, "OpCheckTalkFlag: flag=%d", op->index);

  state->condition_flag = LoadScriptVariableValueAtIndex(NULL, VAR_SCENARIO_TALK_BIT_FLAG, op->index);

  return sizeof(struct OpCheckTalkFlag);
}

static int HandleOpSetTalkFlag(struct ScriptEngineState* state) {
  struct OpSetTalkFlag {
    uint8_t index;
    uint8_t value;
  } __attribute__((packed));

  struct OpSetTalkFlag* op = (struct OpSetTalkFlag*)state->ip;

  COT_LOGFMT(LOG_CATEGORY, "OpSetTalkFlag: flag=%d value=%d", op->index, op->value);

  SaveScriptVariableValueAtIndex(NULL, VAR_SCENARIO_TALK_BIT_FLAG, op->index, op->value == 1 ? 1 : 0);

  return sizeof(struct OpSetTalkFlag);
}

static int HandleOpWasAttacked(struct ScriptEngineState* state) {
  struct OpWasAttacked {
    uint8_t chara;
  } __attribute__((packed));

  struct OpWasAttacked* op = (struct OpWasAttacked*)state->ip;

  COT_LOGFMT(LOG_CATEGORY, "OpWasAttacked: chara=%d", op->chara);

  struct entity* entity = GetScriptCharacterByIndex(state, op->chara);
  if (entity) {
    struct monster* monster = entity->info.monster;
    state->condition_flag = HasMonsterBeenAttackedInDungeons(monster->id.val);
  } else {
    state->condition_flag = false;
  }

  return sizeof(struct OpWasAttacked);
}

static int HandleOpWarp(struct ScriptEngineState* state) {
  struct OpWarp {
    uint8_t chara;
    uint8_t type;
    uint8_t x;
    uint8_t y;
  } __attribute__((packed));

  struct OpWarp* op = (struct OpWarp*)state->ip;

  COT_LOGFMT(LOG_CATEGORY, "OpWarp: chara=%d type=%d x=%d y=%d", op->chara, op->type, op->x, op->y);

  struct entity* entity = GetScriptCharacterByIndex(state, op->chara);

  if (entity) {
    struct position pos = {
      .x = op->x,
      .y = op->y,
    };
    TryWarp(entity, entity, op->type, &pos);
  }

  return sizeof(struct OpWarp);
}

static int HandleOpCheckApparentSpecies(struct ScriptEngineState* state) {
  struct OpCheckApparentSpecies {
    uint8_t chara;
    uint16_t monster_id;
  } __attribute__((packed));

  struct OpCheckApparentSpecies* op = (struct OpCheckApparentSpecies*)state->ip;

  COT_LOGFMT(LOG_CATEGORY, "OpCheckApparentSpecies: chara=%d monster_id=%d", op->chara, op->monster_id);

  state->condition_flag = false;
  struct entity* entity = GetScriptCharacterByIndex(state, op->chara);
  if (entity) {
    struct monster* monster = entity->info.monster;
    state->condition_flag = FemaleToMaleForm(monster->apparent_id.val) == FemaleToMaleForm(op->monster_id);
  }

  return sizeof(struct OpCheckApparentSpecies);
}

static int HandleOpSetNextFloorFlag(struct ScriptEngineState* state) {
  COT_LOG(LOG_CATEGORY, "OpSetNextFloorFlag");
  DUNGEON_PTR->end_floor_flag = true;

  return 0;
}

static int HandleOpSetAnimation(struct ScriptEngineState* state) {
    struct OpSetAnimation {
        uint8_t chara;
        uint8_t animation_group_id;
    } __attribute__((packed));

    struct OpSetAnimation* op = (struct OpSetAnimation*)state->ip;
    
    COT_LOG(LOG_CATEGORY, "OpSetAnimation");
    
    struct entity* entity = GetScriptCharacterByIndex(state, op->chara);

    if (entity) {
        DungeonSetAnimation(entity, op->animation_group_id);
    }

    return sizeof(struct OpSetAnimation);
}

static int HandleOpSetDirection(struct ScriptEngineState* state) {
    struct OpSetDirection {
        uint8_t chara;
        uint8_t animation_id;
    } __attribute__((packed));

    struct OpSetDirection* op = (struct OpSetDirection*)state->ip;
    
    COT_LOG(LOG_CATEGORY, "OpSetDirection");
    
    struct entity* entity = GetScriptCharacterByIndex(state, op->chara);

    if (entity) {
        entity->animation_id = op->animation_id;
        // should actually not be set. Likely used to compare with animation_id to check if it need to apply the change of direction.
        // Turning the line below on make the sprite change direction only once the current animation loop finish.
        //entity->animation_id_mirror0 = op->animation_id;
        entity->info.monster->action.direction.val = op->animation_id;
    }

    return sizeof(struct OpSetDirection);
}

static int HandleOpDisplayAction(struct ScriptEngineState* state) {
    COT_LOG(LOG_CATEGORY, "OpDisplayAction");
    DisplayActions(NULL);
    return 0;
}

static int HandleOpWalk(struct ScriptEngineState* state) {
    struct OpWalk {
        uint8_t chara;
        uint8_t direction;
    } __attribute__((packed));

    struct OpWalk* op = (struct OpWalk*)state->ip;
    
    COT_LOG(LOG_CATEGORY, "OpWalk");
    
    struct entity* entity = GetScriptCharacterByIndex(state, op->chara);

    if (entity) {
        entity->info.monster->action.action_id.val = ACTION_WALK;
        entity->info.monster->action.direction.val = op->direction;
        ExecuteMonsterAction(entity);
    }

    return sizeof(struct OpWalk);
}

static int HandleOpPickItem(struct ScriptEngineState* state) {
    struct OpPickItem {
        uint8_t chara;
    } __attribute__((packed));

    struct OpPickItem* op = (struct OpPickItem*)state->ip;

    COT_LOGFMT(LOG_CATEGORY, "OpPickItem: chara=%d", op->chara);

    struct entity* entity = GetScriptCharacterByIndex(state, op->chara);

    if (entity) {
        entity->info.monster->action.action_id.val = entity == LEADER_PTR ? ACTION_PICK_UP_PLAYER : ACTION_PICK_UP_AI;
        entity->info.monster->action.direction.val = 0;
        ExecuteMonsterAction(entity);
    }

    return sizeof(struct OpPickItem);
}

static int HandleOpUseMove(struct ScriptEngineState* state) {
    struct OpUseMove {
        uint8_t chara;
        uint8_t move_key;
    } __attribute__((packed));

    struct OpUseMove* op = (struct OpUseMove*)state->ip;

    COT_LOGFMT(LOG_CATEGORY, "OpUserMove: chara=%d, move=%d", op->chara, op->move_key);

    struct entity* entity = GetScriptCharacterByIndex(state, op->chara);

    if (entity) {
        struct monster* monster = entity->info.monster;
        if (entity == LEADER_PTR) {
            SetActionUseMovePlayer(&monster->action, GetTeamMemberIndex(entity), op->move_key);
        } else {
            SetActionUseMoveAi(&monster->action, op->move_key, monster->action.direction.val);
        }
        ExecuteMonsterAction(entity);
    }

    return sizeof(struct OpUseMove);
}

static int HandleOpInitDungeonRNG(struct ScriptEngineState* state) {
    struct OpInitDungeonRNG {
        uint32_t seed;
    } __attribute__((packed));

    struct OpInitDungeonRNG* op = (struct OpInitDungeonRNG*)state->ip;

    COT_LOGFMT(LOG_CATEGORY, "OpInitDungeonRNG: seed=%d", op->seed);

    InitDungeonRng(op->seed);

    return sizeof(struct OpInitDungeonRNG);
}

static int HandleOpSetMonsterMove(struct ScriptEngineState* state) {
    struct OpSetMonsterMove {
        uint8_t chara;
        uint8_t slot;
        uint16_t move_id;
        uint8_t pp;
    } __attribute__((packed));

    struct OpSetMonsterMove* op = (struct OpSetMonsterMove*)state->ip;

    COT_LOGFMT(LOG_CATEGORY, "OpSetMonsterMove: chara=%d, slot=%d, move_id=%d, pp=%d", op->chara, op->slot, op->move_id, op->pp);

    if (op->slot >= 4) {
        COT_LOG(LOG_CATEGORY, "Slot too big");
    }
    struct entity* entity = GetScriptCharacterByIndex(state, op->chara);

    if (entity) {
        struct monster * monster = entity->info.monster;
        MemZero(&monster->moves[op->slot], sizeof(struct move));
        monster->moves[op->slot].f_exists = true;
        monster->moves[op->slot].id.val = op->move_id;
        monster->moves[op->slot].pp = op->pp;
    }

    return sizeof(struct OpSetMonsterMove);
    
}

uint8_t RunDungeonScript(char* name, struct entity* npc_monster) {
  struct DungeonScriptBuffer script_buffer;
  if (!LoadDungeonScriptToBuffer(name, &script_buffer)) {
    return 0;
  }

  struct ScriptEngineState state = {
    .ip = script_buffer.data,
    .npc_monster = npc_monster,
    .condition_flag = false,
  };

  while (true) {
    uint8_t opcode = *(uint8_t*)state.ip;
    state.ip += 1;
    if ((uint8_t*) state.ip >= (uint8_t*)script_buffer.data + script_buffer.size) {
      COT_ERROR(LOG_CATEGORY, "Dungeon script out of bounds");
      WaitForever();
    }

    switch(opcode) {
      case DNOPCODE_END: {
        int result = HandleOpEnd(&state);
        MemFree(script_buffer.data);
        DisplayActions(NULL);
        return result;
      }
      case DNOPCODE_DIALOGUE: {
        state.ip += HandleOpDialogue(&state, false);
        break;
      }
      case DNOPCODE_DIALOGUE_WAIT: {
        state.ip += HandleOpDialogue(&state, true);
        break;
      }
      case DNOPCODE_EFFECT: {
        state.ip += HandleOpEffect(&state, false);
        break;
      }
      case DNOPCODE_EFFECT_WAIT: {
        state.ip += HandleOpEffect(&state, true);
        break;
      }
      case DNOPCODE_WAIT: {
        state.ip += HandleOpWait(&state);
        break;
      }
      case DNOPCODE_JUMP: {
        state.ip += HandleOpJump(&state);
        break;
      }
      case DNOPCODE_PLAY_SFX: {
        state.ip += HandleOpPlaySfx(&state);
        break;
      }
      case DNOPCODE_PLAY_ME: {
        state.ip += HandleOpPlayMe(&state);
        break;
      }
      case DNOPCODE_JUMP_EQ: {
        state.ip += HandleOpJumpConditional(&state, true);
        break;
      }
      case DNOPCODE_JUMP_NE: {
        state.ip += HandleOpJumpConditional(&state, false);
        break;
      }

      case DNOPCODE_YES_NO: {
        state.ip += HandleOpYesNo(&state);
        break;
      }

      case DNOPCODE_CHECK_ITEM: {
        state.ip += HandleOpCheckItem(&state);
        break;
      }
      case DNOPCODE_GIVE_ITEM: {
        state.ip += HandleOpGiveItem(&state);
        break;
      }
      case DNOPCODE_TAKE_ITEM: {
        state.ip += HandleOpTakeItem(&state);
        break;
      }
      case DNOPCODE_CHECK_BAG_COUNT: {
        state.ip += HandleOpCheckBagCount(&state);
        break;
      }
      case DNOPCODE_CHECK_MOVE: {
        state.ip += HandleOpCheckMove(&state);
        break;
      }

      case DNOPCODE_CHECK_TALK_FLAG: {
        state.ip += HandleOpCheckTalkFlag(&state);
        break;
      }
      case DNOPCODE_SET_TALK_FLAG: {
        state.ip += HandleOpSetTalkFlag(&state);
        break;
      }

      case DNOPCODE_WAS_ATTACKED: {
        state.ip += HandleOpWasAttacked(&state);
        break;
      }
      case DNOPCODE_WARP: {
        state.ip += HandleOpWarp(&state);
        break;
      }
      case DNOPCODE_CHECK_APPARENT_SPECIES: {
        state.ip += HandleOpCheckApparentSpecies(&state);
        break;
      }
      case DNOPCODE_SET_NEXT_FLOOR_FLAG: {
        state.ip += HandleOpSetNextFloorFlag(&state);
        break;
      }
      case DNOPCODE_SET_ANIMATION: {
          state.ip += HandleOpSetAnimation(&state);
          break;
      }
      case DNOPCODE_SET_DIRECTION: {
          state.ip += HandleOpSetDirection(&state);
          break;
      }
      case DNOPCODE_DISPLAY_ACTION: {
          state.ip += HandleOpDisplayAction(&state);
          break;
      }
      case DNOPCODE_WALK: {
          state.ip += HandleOpWalk(&state);
          break;
      }
      case DNOPCODE_PICKITEM: {
          state.ip += HandleOpPickItem(&state);
          break;
      }
      case DNOPCODE_USEMOVE: {
          state.ip += HandleOpUseMove(&state);
          break;
      }
      case DNOPCODE_INITDUNGEONRNG: {
          state.ip += HandleOpInitDungeonRNG(&state);
          break;
      }
      case DNOPCODE_SETMONSTERMOVE: {
          state.ip += HandleOpSetMonsterMove(&state);
          break;
      }

      default: {
        COT_ERRORFMT(LOG_CATEGORY, "Unknown dungeon script opcode %d", opcode);
        WaitForever();
        break;
      }
    }
  }
}