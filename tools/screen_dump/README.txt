# Host screen-render harness (NOT part of the firmware build).
# Dumps the exact perform-process framebuffer as RLE SVG rects.
# Build: copy the 3 real draw TUs in so their #include "oled_pager.h"
# resolves to the host shim here, then compile + run:
#   cp ../../src/ui/ui_draw_text.cpp ../../src/ui/ui_draw_shapes.cpp ../../src/ui/ui_draw_controls.cpp .
#   c++ -std=c++14 -I . -I ../../src/ui *.cpp -o screendump && ./screendump
# (the copied TUs + binary are throwaway; delete after.)
