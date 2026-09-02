// The backend keeps its original menu and raw indices. Only the C64 desktop's
// page numbering excludes the one synthetic parent supplied by each directory.
// Scan names in the main loop; IO handlers below use constant-time RAM math.
static uint16_t MenuViewParent = 0xffff;
static uint16_t MenuViewCount = 0;
// The requested register changes before MenuViewApply captures the old cursor.
// Keep the active page size with the active map until that rebuild completes.
static uint8_t MenuViewPageSize = MaxItemsPerPage;
static const uint16_t MenuViewInvalid = 0xffff;

uint16_t MenuViewToRaw(uint16_t item) {
   if (item >= MenuViewCount) return MenuViewInvalid;
   return item >= MenuViewParent ? item + 1 : item;
}

uint16_t MenuViewFromRaw(uint16_t item) {
   if (item >= NumItemsFull || item == MenuViewParent) return MenuViewInvalid;
   return item > MenuViewParent ? item - 1 : item;
}

bool MenuViewSelectionValid() { return MenuSource && SelItemFullIdx < NumItemsFull; }

bool MenuViewSelect(uint8_t item) {
   const uint8_t page = IO1[rwRegPageNumber];
   SelItemFullIdx = page && page <= IO1[rRegNumPages] && item < IO1[rRegNumItemsOnPage]
      ? MenuViewToRaw((page - 1) * MenuViewPageSize + item) : MenuViewInvalid;
   return MenuViewSelectionValid();
}

bool MenuViewSelectCursor() { return MenuViewSelect(IO1[rwRegCursorItemOnPg]); }

void MenuViewSetPage(uint8_t page) {
   const uint8_t pages = IO1[rRegNumPages];
   if (page < 1) page = 1;
   if (page > pages) page = pages;
   IO1[rwRegPageNumber] = page;
   const uint16_t base = (page - 1) * MenuViewPageSize;
   const uint16_t remaining = MenuViewCount > base ? MenuViewCount - base : 0;
   const uint8_t count = remaining > MenuViewPageSize ? MenuViewPageSize : remaining;
   IO1[rRegNumItemsOnPage] = count;
   if (IO1[rwRegCursorItemOnPg] >= count) IO1[rwRegCursorItemOnPg] = count ? count - 1 : 0;
   IO1[rwRegSelItemOnPage] = IO1[rwRegCursorItemOnPg];
   MenuViewSelectCursor();
}

void MenuViewSetCursorRaw(uint16_t raw) {
   uint16_t item = MenuViewFromRaw(raw);
   if (item == MenuViewInvalid) item = 0;
   IO1[rwRegCursorItemOnPg] = item % MenuViewPageSize;
   MenuViewSetPage(item / MenuViewPageSize + 1);
}

FLASHMEM void MenuViewRebuild() {
   MenuViewPageSize = IO1[rwRegMenuView] ? MaxDesktopItemsPerPage : MaxItemsPerPage;
   MenuViewParent = MenuViewInvalid;
   if (IO1[rwRegMenuView] && MenuSource) {
      for (uint16_t item = 0; item < NumItemsFull; ++item) {
         if (MenuSource[item].ItemType == rtDirectory && MenuSource[item].Name &&
             strcmp(MenuSource[item].Name, UpDirString) == 0) {
            MenuViewParent = item;
            break; // Directory/menu producers supply exactly one synthetic parent.
         }
      }
   }
   MenuViewCount = NumItemsFull - (MenuViewParent != MenuViewInvalid ? 1 : 0);
   IO1[rRegNumPages] = MenuViewCount ? (MenuViewCount - 1) / MenuViewPageSize + 1 : 1;
}

FLASHMEM void MenuViewApply() {
   // The old map still describes the cursor until the requested view is built.
   const uint8_t page = IO1[rwRegPageNumber];
   const uint16_t raw = page ? MenuViewToRaw((page - 1) * MenuViewPageSize + IO1[rwRegCursorItemOnPg]) : MenuViewInvalid;
   MenuViewRebuild();
   MenuViewSetCursorRaw(raw);
}
