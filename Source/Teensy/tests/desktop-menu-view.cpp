#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#define FLASHMEM
enum { rwRegPageNumber = 14, rRegNumPages = 15, rRegNumItemsOnPage = 13,
       rwRegCursorItemOnPg = 12, rwRegSelItemOnPage = 11, rwRegMenuView = 106,
       rtDirectory = 2, rtFilePrg = 6 };
static const uint16_t MaxItemsPerPage = 19;
static const char* UpDirString = "/.. <Up Dir>";
struct StructMenuItem { uint8_t ItemType; const char* Name; };
static uint8_t registers[107];
static volatile uint8_t* IO1 = registers;
static StructMenuItem* MenuSource;
static uint16_t NumItemsFull, SelItemFullIdx;

// Compile the production map and register integration, not a second algorithm.
#include "../MinimalBoot/Common/IO_Handlers/DesktopMenuView.c"

int main() {
   unsigned scenarios = 0;
   for (unsigned files : {0u, 1u, 18u, 19u, 20u, 37u, 38u, 39u, 3999u}) {
      for (unsigned parent : {0u, files / 2, files}) {
         std::vector<std::string> names;
         names.reserve(files + 1);
         for (unsigned i = 0; i <= files; ++i) names.push_back("FILE" + std::to_string(i) + ".PRG");
         names[parent] = UpDirString;
         std::vector<StructMenuItem> menu;
         for (unsigned i = 0; i <= files; ++i)
            menu.push_back({static_cast<uint8_t>(i == parent ? rtDirectory : rtFilePrg), names[i].c_str()});
         MenuSource = menu.data();
         NumItemsFull = static_cast<uint16_t>(menu.size());
         for (unsigned desktop : {0u, 1u}) {
            IO1[rwRegMenuView] = desktop;
            MenuViewRebuild();
            const unsigned visible = files + (desktop ? 0 : 1);
            assert(MenuViewCount == visible);
            assert(IO1[rRegNumPages] == (visible ? (visible - 1) / 19 + 1 : 1));
            for (unsigned i = 0; i < visible; ++i) {
               MenuViewSetPage(i / 19 + 1);
               assert(MenuViewSelect(i % 19));
               const unsigned raw = desktop && i >= parent ? i + 1 : i;
               assert(SelItemFullIdx == raw);
               assert(MenuViewFromRaw(raw) == i);
               // Metadata, launch and file operations all consume this raw item.
               assert(MenuSource[SelItemFullIdx].Name == names[raw].c_str());
               if (desktop) assert(std::strcmp(MenuSource[SelItemFullIdx].Name, UpDirString) != 0);
               MenuViewSetCursorRaw(raw);
               assert(IO1[rwRegPageNumber] == i / 19 + 1);
               assert(IO1[rwRegCursorItemOnPg] == i % 19);
               assert(SelItemFullIdx == raw);
            }
            MenuViewSetPage(255);
            const unsigned lastCount = visible ? (visible - 1) % 19 + 1 : 0;
            assert(IO1[rRegNumItemsOnPage] == lastCount);
            assert(!MenuViewSelect(static_cast<uint8_t>(lastCount)));
            assert(SelItemFullIdx >= NumItemsFull);
            assert(!MenuViewSelectionValid());
            MenuViewSetPage(0);
            assert(IO1[rwRegPageNumber] == 1);
            if (!visible) {
               assert(!MenuViewSelectCursor());
               assert(IO1[rRegNumItemsOnPage] == 0);
               assert(SelItemFullIdx == MenuViewInvalid);
            }
            ++scenarios;
         }
         if (files) {
            const unsigned raw = parent == 0 ? 1 : 0;
            IO1[rwRegMenuView] = 0;
            MenuViewRebuild();
            MenuViewSetCursorRaw(raw);
            IO1[rwRegMenuView] = 1;
            MenuViewApply();
            assert(SelItemFullIdx == raw);
            IO1[rwRegMenuView] = 0;
            MenuViewApply();
            assert(SelItemFullIdx == raw);
            // Classic parent selection switches safely to the first real file.
            MenuViewSetCursorRaw(parent);
            IO1[rwRegMenuView] = 1;
            MenuViewApply();
            assert(SelItemFullIdx != parent);
            assert(MenuViewSelectionValid());
         }
      }
   }
   // A matching filename is not navigation; near matches are not navigation.
   StructMenuItem ordinary[] = {{rtFilePrg, UpDirString}, {rtDirectory, "/..GAMES"}, {rtDirectory, "/Games"}};
   MenuSource = ordinary;
   NumItemsFull = 3;
   IO1[rwRegMenuView] = 1;
   MenuViewRebuild();
   assert(MenuViewCount == 3 && MenuViewParent == MenuViewInvalid);
   for (unsigned i = 0; i < 3; ++i) assert(MenuViewToRaw(i) == i);
   std::puts((std::to_string(scenarios + 1) + " desktop menu view scenarios passed").c_str());
}
