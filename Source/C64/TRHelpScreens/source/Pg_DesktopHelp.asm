; Desktop-specific controls, bundled into the firmware Help program.
DesktopHelp:
   jsr CommonInit
   lda #<MsgDesktopHelp
   ldy #>MsgDesktopHelp
   jsr PrintString
   jmp WaitHelpMenuKey
MsgDesktopHelp:
   !tx EscC,EscSourcesColor, ChrRvsOn, " Desktop controls ", ChrReturn
   !tx "Click: select. Double-click: open.",ChrReturn
   !tx "F1 Help / F6 Music / F8 Control Panel",ChrReturn
   !tx "Panel: arrows select, RETURN opens.",ChrReturn
   !tx "Click X or press STOP to close.",ChrReturn
   !tx "TEENSY (top-left): Snake, Calculator",ChrReturn
   !tx "and Text Viewer: read-only, no editing.",ChrReturn
   !tx "HOME then STOP, arrows, RETURN: apps.",ChrReturn
   !tx "Games/Utilities icons are folders.",ChrReturn
   !tx "File > Boot Disk: SHIFT+RUN/STOP",ChrReturn
   !tx "Drive 8/9: LOAD ",34,"*",34,",device,1",ChrReturn
   !tx "Select a mounted disk or disk folder.",ChrReturn
   !tx "A Teensy SD/USB image is not drive 8.",ChrReturn
   !tx "Music: open .sid in Teensy, SD or USB.",ChrReturn
   !tx "Music > Use Default saves that SID.",ChrReturn
   !tx "Autoplay controls music at startup.",ChrReturn
   !tx "Advanced opens song/speed/voice tools.",ChrReturn
   !tx "C Copy / P Paste / D Delete",ChrReturn
   !tx "Delete is permanent; no trash folder."
   !tx 0
