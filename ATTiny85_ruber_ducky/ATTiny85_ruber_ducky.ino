#include "DigiKeyboard.h"

void setup() {

  DigiKeyboard.delay(2000);

  // Open Run
  DigiKeyboard.sendKeyStroke(KEY_R, MOD_GUI_LEFT);
  DigiKeyboard.delay(500);

  // Open Notepad
  DigiKeyboard.print("notepad");
  DigiKeyboard.sendKeyStroke(KEY_ENTER);
  DigiKeyboard.delay(1500);

  // Type the VBScript
  DigiKeyboard.print(
    "Dim VAR\r\n"
    "VAR = 0\r\n"
    "Do Until VAR = 6\r\n"
    "VAR = MsgBox(\"ARE YOU DUMB?\", 4 + 16, \"FACT TESTER\")\r\n"
    "Loop\r\n"
    "A = MsgBox(\"HAHAHA, I KNEW IT!!\", 0 + 64, \"FACT TESTER\")"
  );

  DigiKeyboard.delay(500);

  // Save As
  DigiKeyboard.sendKeyStroke(KEY_S, MOD_CONTROL_LEFT | MOD_SHIFT_LEFT);
  DigiKeyboard.delay(1200);

  // Save to Desktop
  DigiKeyboard.print("C:\\Users\\parth\\Desktop\\FactTester.vbs");
  DigiKeyboard.delay(300);
  DigiKeyboard.sendKeyStroke(KEY_ENTER);
  DigiKeyboard.delay(1000);

  // Close Notepad
  DigiKeyboard.sendKeyStroke(KEY_F4, MOD_ALT_LEFT);
  DigiKeyboard.delay(1000);

  // Open Run
  DigiKeyboard.sendKeyStroke(KEY_R, MOD_GUI_LEFT);
  DigiKeyboard.delay(500);

  // Open the VBS file
  DigiKeyboard.print("C:\\Users\\parth\\Desktop\\FactTester.vbs");
  DigiKeyboard.sendKeyStroke(KEY_ENTER);
}

void loop() {
}