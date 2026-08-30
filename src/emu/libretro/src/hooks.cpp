// Copyright (c) 2026 EKA2L1 Team.
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The handful of things the emulator expects its frontend to be able to do.
// They are declared in the emulator's own headers and defined by whoever is
// hosting it - qt, android, ios - so a core has to define them too, or the
// first Symbian application that asks for a text field takes the frontend
// down with it.
//
// A core has no windows to put a dialog in and no business drawing over the
// frontend's screen, so these answer rather than ask, and say what they did.

#include <common/applauncher.h>
#include <common/log.h>
#include <drivers/ui/input_dialog.h>

namespace eka2l1::common {
    bool launch_browser(const std::string &url) {
        // Opening a browser from inside someone else's fullscreen application
        // is not a favour to anybody. The URL goes to the log, where a curious
        // user can find it.
        LOG_INFO(FRONTEND_CMDLINE, "A title asked to open a browser at {}", url);
        return false;
    }
}

namespace eka2l1::drivers::ui {
    bool open_input_view(const std::u16string &initial_text, const int max_len, input_dialog_complete_callback complete_callback) {
        // Symbian is asking for text - a save name, a nickname. Until the core
        // can put that on screen, the honest answer is the text the title
        // already had, which is what a cancelled dialog would return.
        LOG_INFO(FRONTEND_CMDLINE, "A title asked for text input; answering with what it had");

        if (complete_callback) {
            complete_callback(initial_text);
        }

        return true;
    }

    void close_input_view() {
        // Nothing was opened.
    }

    void show_yes_no_dialog(const std::u16string &text, const std::u16string &button1_text, const std::u16string &button2_text,
        yes_no_dialog_complete_callback complete_callback) {
        // A question nobody can see must not be answered "yes": the title may
        // be asking whether to overwrite something.
        LOG_INFO(FRONTEND_CMDLINE, "A title asked a yes/no question; answering no");

        if (complete_callback) {
            complete_callback(0);
        }
    }
}
