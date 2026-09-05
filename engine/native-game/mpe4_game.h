#ifndef MPE4_GAME_H
#define MPE4_GAME_H

#include <stddef.h>
#include <stdint.h>

// Define this as FLASHMEM in the Teensy integration. The portable core never
// owns a bus, an emulated CPU, a framebuffer, or an unbounded allocation.
#ifndef MPE4_CODE
#define MPE4_CODE
#endif
#ifndef MPE4_RODATA
#define MPE4_RODATA
#endif

namespace mpe4 {
enum Resource : uint8_t { Logic=0, Picture=1, View=2, Sound=3, Objects=4, Vocabulary=5, Font=6, NativeView=7 };
enum Error : uint8_t { Okay=0, ResourceMissing, ResourceBounds, BadLogic,
  UnsupportedAction, UnsupportedTest, ObjectBounds, StringBounds,
  StackOverflow, InstructionLimit, HostFailure, BadVocabulary, BadSave, NoPosition };
enum ObjectFlags : uint16_t { Animated=1, Drawn=2, Updating=4, Cycling=8,
  FixedLoop=16, FixedPriority=32, IgnoreHorizon=64, IgnoreObjects=128,
  IgnoreBlocks=256, OnWater=512, OnLand=1024, Motion=2048, SkipCycle=4096, Repositioned=8192 };
enum Modal : uint8_t { NoModal=0, Message, StringInput, NumberInput,
  Inventory, Menu, Pause, Quit, Restart, SaveSlots, RestoreSlots };
enum Step : uint8_t { Idle=0, Frame, Waiting, Yielded, Failed };
enum Key : uint8_t { Backspace=8, Enter=13, Escape=27, Left=0x80,
  Right=0x81, Up=0x82, Down=0x83, Home=0x84, End=0x85,
  PageUp=0x86, PageDown=0x87, F1=0x90 };
// Internal text-cell markers for the source interpreter's red/white window
// decoration. Printable text remains ASCII and the marker consumes no state.
enum WindowCell : uint8_t { WindowMarker=0x80, WindowTop=1, WindowBottom=2,
  WindowLeft=4, WindowRight=8 };

struct CelInfo { uint8_t width, height, loops, cels; };
struct Object {
  uint8_t x, y, view, loop, cel, priority, width, height, direction;
  uint8_t stepSize, stepTime, stepCounter, cycleTime, cycleCounter;
  uint8_t cycleMode, motionMode, targetX, targetY, motionFlag, cycleFlag;
  uint8_t moveStep, followDistance, wanderCounter;
  uint16_t flags;
};
struct Call { uint16_t ip, end; uint8_t logic; };
struct MenuItem { char text[24]; uint8_t menu, controller, enabled; };
struct Binding { uint8_t ascii, scan, controller; };
constexpr unsigned SaveSlotCount = 12;
enum SaveStatus : uint8_t { SaveEmpty=0, SaveReady, SaveUnavailable };
struct SaveInfo { SaveStatus status; uint8_t room, score; };
constexpr size_t LegacyStateBytes = 9528;
constexpr unsigned MaxBindings = 64;

// This structure is pointer-free and is the complete save/checkpoint domain.
// save/restore callbacks must envelope it with game identity/version/CRC and
// commit an entire save before reporting success. Restore redraws the visual
// script and current actors; it does not contain transport state.
struct State {
  uint32_t signature, random, scans, clockTicks;
  uint16_t scanStart[256], objectCount, scanTicks, modalTicks;
  uint8_t vars[256], flags[32], inventory[256], controllers[32];
  Object objects[32];
  char strings[25][41], input[81], previousInput[81], parsedText[81];
  uint16_t words[20];
  uint8_t wordOffset[20], wordLength[20], wordCount, inputLength;
  uint8_t text[1000], attributes[1000], savedText[1000], savedAttributes[1000];
  char menuTitles[8][16];
  MenuItem menuItems[40];
  Binding bindings[32];
  Call calls[16];
  // Bounded visual script: draw, overlay and add.to.pic are replayed on restore.
  uint8_t visualScript[768];
  uint16_t visualLength;
  uint8_t scriptLimit, scriptSavedLengthLow, scriptSavedLengthHigh;
  uint8_t callDepth, logic, horizon, priorityBase, picture, pendingRoom;
  uint8_t graphicsTop, inputRow, statusRow, foreground, background, cursor;
  uint8_t modal, modalString, modalMaximum, modalRow, modalColumn;
  uint8_t menuCount, menuItemCount, menuSelection, menuColumn, bindingCount;
  uint8_t soundFlag, key, keyScan, direction, block[4], shakeTicks, showObjectView;
  uint8_t pointerX, pointerY, pointerButtons;
  bool running, inScan, roomPending, firstScan, playerControl, inputEnabled;
  bool graphics, pictureVisible, statusVisible, textDirty, frameDirty;
  bool blockActive, soundActive, menuAllowed, holdKey, dialogue;
  bool skipPresentedIntro, restorePending, modalSaved, scriptEnabled, showObject;
  Error error;
  uint8_t errorLogic, errorOpcode;
  uint16_t errorIp;
  // Keep the complete native05 save prefix, including its tail padding.
  // Additional authored keys are appended so validated older saves can be
  // restored by zero-extending that prefix.
  alignas(uint32_t) Binding overflowBindings[32];
};
static_assert(offsetof(State, overflowBindings) == LegacyStateBytes, "native05 save prefix must remain byte-exact");
static_assert(sizeof(State) == 9624, "native AGI save extension must remain bounded");
static_assert(sizeof(State) <= 10240, "native AGI state must remain <=10 KiB");

struct Host {
  void *context;
  uint32_t (*resourceSize)(void *, uint8_t type, uint8_t id);
  bool (*readResource)(void *, uint8_t type, uint8_t id,
                       uint32_t offset, uint8_t *data, uint16_t count);
  bool (*drawPicture)(void *, uint8_t id, bool overlay);
  bool (*viewCelInfo)(void *, uint8_t view, uint8_t loop, uint8_t cel, CelInfo *);
  bool (*addToPicture)(void *, uint8_t view, uint8_t loop, uint8_t cel,
                        uint8_t x, uint8_t y, uint8_t priority, uint8_t margin);
  uint8_t (*priorityAt)(void *, uint8_t x, uint8_t y);
  bool (*playSound)(void *, uint8_t id);
  void (*stopSound)(void *);
  bool (*save)(void *, uint8_t slot, const State *, size_t);
  bool (*restore)(void *, uint8_t slot, State *, size_t);
  SaveInfo (*saveInfo)(void *, uint8_t slot);
};

struct Input {
  // One edge per call. ASCII printable keys, MPE4 Key for navigation; scan is
  // an IBM AGI scan code for set.key bindings. Direction is AGI 0..8 held state.
  uint8_t key, scan, direction;
  bool fire, soundFinished;
  uint16_t elapsed60Hz;
  bool pointerEvent;
  uint8_t pointerX, pointerY, pointerButtons;
};

class Game {
 public:
  Host host;
#if defined(MHS_AGI_EXTERNAL_STATE)
  // Independent module: the guest's checkpoint domain resides in RAM2.
  State &state;
  explicit Game(State &guest) : state(guest) {}
#else
  State state;
#endif
  MPE4_CODE bool start(const Host &, bool skipPresentedIntro = true, uint32_t seed = 1);
  MPE4_CODE Step tick(const Input &, uint32_t instructionBudget = 8192);
  MPE4_CODE bool parse(const char *);
  MPE4_CODE bool message(uint8_t logic, uint8_t number, char *out, uint16_t capacity);
  MPE4_CODE bool restoreVisuals();
  MPE4_CODE bool flag(uint8_t) const;
  MPE4_CODE void setFlag(uint8_t, bool);
 private:
  // Host events arriving between bounded interpreter slices belong to the
  // following complete scan. This transient queue is not save-game state.
  uint8_t queuedControllers[32];
  // have.key polls fresh events while an authored wait suspends a scan.
  // These transient events are separate from v19 and never enter save data.
  uint16_t pendingHaveKey;
  bool haveKeyWaiting;
  // A pointer can open the menu between interpreter slices. Its selection
  // must survive until the next complete scan, just like keyboard events.
  bool pointerMenu;
  // Refresh from storage when the picker opens; navigation performs no SD I/O.
  SaveInfo saveInfo[SaveSlotCount];
  MPE4_CODE bool reset(const Host &, bool, uint32_t, bool restarting);
  MPE4_CODE bool restartGame();
  MPE4_CODE bool fail(Error, uint8_t opcode = 0);
  MPE4_CODE bool read(uint8_t, uint8_t, uint32_t, uint8_t *, uint16_t);
  MPE4_CODE bool pushLogic(uint8_t);
  MPE4_CODE bool fetch(uint8_t &);
  MPE4_CODE bool fetch16(uint16_t &);
  MPE4_CODE bool expression(bool &);
  MPE4_CODE bool test(uint8_t, bool, bool &);
  MPE4_CODE bool action(uint8_t, const uint8_t *);
  MPE4_CODE bool run(uint32_t);
  MPE4_CODE bool newRoom(uint8_t);
  MPE4_CODE bool celInfo(uint8_t);
  MPE4_CODE void moveObjects();
  MPE4_CODE void updateEgoControls();
  MPE4_CODE bool passable(uint8_t, int16_t, int16_t, bool &, bool moving = true);
  MPE4_CODE bool fixPosition(uint8_t);
  MPE4_CODE void finishMove(uint8_t);
  MPE4_CODE void keyInput(const Input &);
  MPE4_CODE uint8_t pointerInput(const Input &);
  MPE4_CODE void drawInput();
  MPE4_CODE void drawStatus();
  MPE4_CODE void textAt(uint8_t, uint8_t, const char *, uint8_t width = 40);
  MPE4_CODE void clearLines(uint8_t, uint8_t, uint8_t);
  MPE4_CODE void showMessage(const char *, uint8_t = 0, uint8_t = 0, uint8_t = 0);
  MPE4_CODE void closeModal();
  MPE4_CODE void inventoryMenu();
  MPE4_CODE void saveSlots(bool save);
  MPE4_CODE void drawSaveSlots();
  MPE4_CODE bool restoreSlot(uint8_t slot);
  MPE4_CODE void renderMenu();
  MPE4_CODE void openMenu(bool fromPointer);
  MPE4_CODE Binding &binding(unsigned);
  MPE4_CODE const Binding &binding(unsigned) const;
  MPE4_CODE int c64FunctionController(uint8_t) const;
  MPE4_CODE void menuItemText(unsigned, char *, size_t) const;
  MPE4_CODE bool inventoryName(uint8_t, char *, uint8_t);
  MPE4_CODE bool messageImpl(uint8_t, uint8_t, char *, uint16_t, uint8_t);
  MPE4_CODE bool script(uint8_t, const uint8_t *, uint8_t);
};
}
#endif
