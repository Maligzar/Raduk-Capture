
20. **Panel open/closed state across a restart** (phase 101). Close two
    panels, leave two open with one of them maximised, press
    Ctrl+Shift+End, quit, relaunch. All four should come back exactly as
    they were, the maximised one still maximised, and the keyboard should
    be on the control window rather than on whichever panel restored
    last. The restore itself is not unit-testable -- it needs real
    windows -- which is precisely the gap that let phase 98 ship a
    one-directional fix.
