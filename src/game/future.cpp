/*
    Copyright (C) 2005 Michael K. McCarty & Fritz Bronner

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/
/** \file future.c This is responsible for Future Mission planning screen.
 *
 */

#include "future.h"

#include "display/graphics.h"
#include "display/surface.h"
#include "display/palettized_surface.h"
#include "display/legacy_surface.h"

#include <assert.h>

#include "Buzz_inc.h"
#include "admin.h"
#include "crew.h"
#include "draw.h"
#include "filesystem.h"
#include "futbub.h"
#include "game_main.h"
#include "gr.h"
#include "ioexception.h"
#include "logging.h"
#include "mc.h"
#include "mc2.h"
#include "pace.h"
#include "prest.h"
#include "sdlhelper.h"


LOG_DEFAULT_CATEGORY(future)


/*missStep.dat is plain text, with:
Mission Number (2 first bytes of each line)
A Coded letter, each drawing a different line (1105-1127 for all possible letters)
Numbers following each letter, which are the parameters of the function
Each line must finish with a Z, so the game stops reading
Any other char is ignored, but it's easier to read for a human that way */
char missStep[1024];
static inline char B_Mis(char x)
{
    return missStep[x] - 0x30;
}

bool JointFlag, MarsFlag, JupiterFlag, SaturnFlag;
display::LegacySurface *vh;

struct StepInfo {
    int16_t x_cor;
    int16_t y_cor;
} StepBub[MAXBUB];

// TODO: Localize these. Too many global/file global variables.
/* Used in SetParameters, PianoKey, UpSearchRout, DownSearchRout, Future,
 * and DrawMission
 */
std::vector<struct mStr> missionData;

extern int SEG;


/**
 * Hold the mission parameters used for searching mission by their
 * characteristics.
 */
struct MissionNavigator {
    struct NavButton {
        int value;
        bool lock;
    };

    NavButton duration, docking, EVA, LM, joint;
};


void Load_FUT_BUT(void);
bool MarsInRange(unsigned int year, unsigned int season);
bool JupiterInRange(unsigned int year, unsigned int season);
bool SaturnInRange(unsigned int year, unsigned int season);
bool JointMissionOK(char plr, char pad);
void DrawFuture(char plr, int mis, char pad, MissionNavigator &nav);
void ClearDisplay(void);
int GetMinus(char plr);
void SetParameters(void);
void DrawLocks(const MissionNavigator &nav);
void Toggle(int wh, int i);
void TogBox(int x, int y, int st);
void PianoKey(int X, MissionNavigator &nav);
void DrawPie(int s);
void PlaceRX(int s);
void ClearRX(int s);
bool NavMatch(const MissionNavigator &nav, const struct mStr &mission);
void NavReset(MissionNavigator &nav);
int UpSearchRout(int num, char plr, const MissionNavigator &navigator);
int DownSearchRout(int num, char plr, const MissionNavigator &navigator);
void PrintDuration(int duration);
void DrawMission(char plr, int X, int Y, int val, int pad, char bub,
                 MissionNavigator &navigator);


/* TODO: Documentation...
 *
 */
void Load_FUT_BUT(void)
{
    FILE *fin;
    unsigned i;
    fin = sOpen("NFUTBUT.BUT", "rb", 0);
    i = fread(display::graphics.legacyScreen()->pixels(), 1, MAX_X * MAX_Y, fin);
    fclose(fin);
    RLED_img(display::graphics.legacyScreen()->pixels(), vh->pixels(), i, vh->width(), vh->height());
    return;
}


/* Is Mars at the right point in its orbit where a rocket launched at
 * the given time will be able to intercept it?
 */
bool MarsInRange(unsigned int year, unsigned int season)
{
    return ((year == 60 && season == 0) || (year == 62 && season == 0) ||
            (year == 64 && season == 0) || (year == 66 && season == 0) ||
            (year == 69 && season == 1) || (year == 71 && season == 1) ||
            (year == 73 && season == 1));
}


/* Is Jupiter at the right point in its orbit where a rocket launched
 * at the given time will be able to intercept it?
 */
bool JupiterInRange(unsigned int year, unsigned int season)
{
    return (year == 60 || year == 64 || year == 68 || year == 72 ||
            year == 73 || year == 77);
}


/* Is Saturn at the right point in its orbit where a rocket launched
 * at the given time will be able to intercept it?
 */
bool SaturnInRange(unsigned int year, unsigned int season)
{
    return (year == 61 || year == 66 || year == 72);
}


/* Is there room among the launch pads to schedule a joint mission,
 * with the given pad being the first part?
 *
 * \param plr  The player scheduling the launch
 * \param pad  0, 1, or 2 for the primary, secondary, or tertiary pad.
 * \return     True if a joint mission may be scheduled on the pad.
 */
bool JointMissionOK(char plr, char pad)
{
    return (pad >= 0 && pad < MAX_LAUNCHPADS - 1) &&
           (Data->P[plr].LaunchFacility[pad + 1] == 1) &&
           ((Data->P[plr].Future[pad + 1].MissionCode == Mission_None) ||
            (Data->P[plr].Future[pad + 1].part == 1));
}


/* Draws the entire Future Missions display, including the mission-
 * specific information. Used to initialize the mission selector
 * interface.
 *
 * This relies on the global buffer vh, which must have been created
 * prior. The Future Missions button art is loaded into vh by this
 * function.
 *
 * This modifies the global variable Mis, via DrawMission().
 *
 * \param plr  The player scheduling the mission's design scheme.
 * \param mis  The mission type.
 * \param pad  0, 1, or 2 depending on which pad is being used.
 */
void DrawFuture(char plr, int mis, char pad, MissionNavigator &nav)
{
    FadeOut(2, 10, 0, 0);
    Load_FUT_BUT();

    boost::shared_ptr<display::PalettizedSurface> planets(Filesystem::readImage("images/fmin.img.0.png"));
    planets->exportPalette();

    display::graphics.screen()->clear();

    gr_sync();

    fill_rectangle(1, 1, 318, 21, 3);
    fill_rectangle(317, 22, 318, 198, 3);
    fill_rectangle(1, 197, 316, 198, 3);
    fill_rectangle(1, 22, 2, 196, 3);
    OutBox(0, 0, 319, 199);
    InBox(3, 3, 30, 19);
    InBox(3, 22, 316, 196);
    IOBox(242, 3, 315, 19);
    ShBox(5, 24, 183, 47);
    ShBox(5, 24, 201, 47); //name box
    ShBox(5, 74, 41, 82); // RESET
    ShBox(5, 49, 53, 72); //dur/man
    ShBox(43, 74, 53, 82);   // Duration lock
    ShBox(80, 74, 90, 82);   // Docking lock
    ShBox(117, 74, 127, 82); // EVA lock
    ShBox(154, 74, 164, 82); // LM lock
    ShBox(191, 74, 201, 82); // Joint mission lock
    ShBox(5, 84, 16, 130); //arrows up
    ShBox(5, 132, 16, 146); //middle box
    ShBox(5, 148, 16, 194); //    down
    ShBox(203, 24, 238, 31); // new right boxes

    // Mission penalty numerical display
    fill_rectangle(206, 36, 235, 44, 7);
    ShBox(203, 33, 238, 47);
    InBox(205, 35, 236, 45);

    // Mission scroll arrows
    draw_up_arrow(8, 95);
    draw_down_arrow(8, 157);

    // Display mission steps toggle
    vh->copyTo(display::graphics.legacyScreen(), 140, 5, 5, 132, 15, 146);

    // Draw the mission specification toggle buttons
    Toggle(5, 1);
    DrawPie(0);
    OutBox(5, 49, 53, 72);
    Toggle(1, 1);
    TogBox(55, 49, 0);
    Toggle(2, 1);
    TogBox(92, 49, 0);
    Toggle(3, 1);
    TogBox(129, 49, 0);
    Toggle(4, 1);

    if (JointFlag == false) {
        InBox(191, 74, 201, 82);
        TogBox(166, 49, 1);
    } else {
        OutBox(191, 74, 201, 82);
        TogBox(166, 49, 0);
    }

    gr_sync();

    DrawMission(plr, 8, 37, mis, pad, 1, nav);

    GetMinus(plr);

    display::graphics.setForegroundColor(5);

    /* lines of text are 1:8,30  2:8,37   3:8,44    */
    switch (pad) { // These used to say Pad 1, 2, 3  -Leon
    case 0:
        draw_string(8, 30, "PAD A:");
        break;

    case 1:
        draw_string(8, 30, "PAD B:");
        break;

    case 2:
        draw_string(8, 30, "PAD C:");
        break;
    }

    display::graphics.setForegroundColor(1);

    draw_string(9, 80, "RESET");
    draw_string(258, 13, "CONTINUE");

    display::graphics.setForegroundColor(11);

    if (Data->Season == 0) {
        draw_string(200, 9, "SPRING");
    } else {
        draw_string(205, 9, "FALL");
    }

    draw_string(206, 16, "19");
    draw_number(0, 0, Data->Year);
    display::graphics.setForegroundColor(1);
    draw_small_flag(plr, 4, 4);
    draw_heading(40, 5, "FUTURE MISSIONS", 0, -1);
    FadeIn(2, 10, 0, 0);

    return;
}


/* Draws the mission starfield. The background depicts any heavenly
 * bodies reachable by an interplanetary mission. Earth, the Moon,
 * Venus, and Mercury are always shown. Depending on the current year
 * and season, some combination of Mars, Jupiter, and Saturn may be
 * depicted.
 */
void ClearDisplay(void)
{
    boost::shared_ptr<display::PalettizedSurface> background(Filesystem::readImage("images/fmin.img.0.png"));

    display::graphics.screen()->draw(background, 202, 48, 40, 35, 202, 48);
    display::graphics.screen()->draw(background, 17, 83, 225, 113, 17, 83);
    display::graphics.screen()->draw(background, 242, 23, 74, 173, 242, 23);

    if (MarsFlag == true) {
        display::graphics.screen()->draw(background, 1, 1, 12, 11, 198, 153);
    }

    if (JupiterFlag == true) {
        display::graphics.screen()->draw(background, 14, 1, 51, 54, 214, 130);
    }

    if (SaturnFlag == true) {
        display::graphics.screen()->draw(background, 66, 1, 49, 53, 266, 135);
    }

    return;
}


/* TODO: Documentation...
 *
 */
int GetMinus(char plr)
{
    char i;
    int u;

    i = PrestMin(plr);
    fill_rectangle(206, 36, 235, 44, 7);

    if (i < 3) {
        u = 1;    //ok
    } else if (i < 9) {
        u = 10;    //caution
    } else {
        u = 19;    //danger
    }

    vh->copyTo(display::graphics.legacyScreen(), 203, u, 203, 24, 238, 31);
    display::graphics.setForegroundColor(11);

    if (i > 0) {
        draw_string(210, 42, "-");
    } else {
        grMoveTo(210, 42);
    }

    draw_number(0, 0, i);
    display::graphics.setForegroundColor(1);
    return 0;
}

/**
 * Cache a subset of mission data in a local array.
 *
 * Populates the global array missionData with stored mission data.
 */
void SetParameters(void)
{
    if (! missionData.empty()) {
        return;
    }

    FILE *fin = sOpen("MISSION.DAT", "rb", 0);

    if (fin == NULL) {
        throw IOException("Could not open MISSION.DAT");
    }

    for (int i = 0; i < 62; i++) {
        struct mStr entry;

        if (fread(&entry, sizeof entry, 1, fin) != 1) {
            missionData.clear();
            throw IOException("Error reading entry in MISSION.DAT");
        }

        missionData.push_back(entry);
    }

    fclose(fin);
    return;
}

/* TODO: Documentation...
 *
 * \param nav TODO.
 */
void DrawLocks(const MissionNavigator &nav)
{
    bool lock[5] = { nav.duration.lock, nav.docking.lock, nav.EVA.lock,
                     nav.LM.lock, nav.joint.lock
                   };

    for (int i = 0; i < 5; i++) {
        if (lock[i] == true) {
            PlaceRX(i + 1);
        } else {
            ClearRX(i + 1);
        }
    }

    return;
}


/* Draws the illustration on a mission parameter button. Each button
 * has two illustrations, one for the selected state and another for
 * the unselected state.
 *
 * The illustrations are stored in a buffer via the global pointer vh,
 * which reads the information in Load_FUT_BUT().
 *
 * \param wh the button
 * \param i in or out
 */
void Toggle(int wh, int i)
{
    TRACE3("->Toggle(wh %d, i %d)", wh, i);

    switch (wh) {
    case 1:
        if (i == 1) {
            vh->copyTo(display::graphics.legacyScreen(), 1, 21, 55, 49, 89, 81);
        } else {
            vh->copyTo(display::graphics.legacyScreen(), 1, 56, 55, 49, 89, 81);
        }

        break;

    case 2:
        if (i == 1)  {
            vh->copyTo(display::graphics.legacyScreen(), 38, 21, 92, 49, 127, 81);
        } else {
            vh->copyTo(display::graphics.legacyScreen(), 38, 56, 92, 49, 127, 81);
        }

        break;

    case 3:
        if (i == 1)  {
            vh->copyTo(display::graphics.legacyScreen(), 75, 21, 129, 49, 163, 81);
        } else {
            vh->copyTo(display::graphics.legacyScreen(), 75, 56, 129, 49, 163, 81);
        }

        break;

    case 4:
        if (i == 1)  {
            vh->copyTo(display::graphics.legacyScreen(), 112, 21, 166, 49, 200, 81);
        } else {
            vh->copyTo(display::graphics.legacyScreen(), 112, 56, 166, 49, 200, 81);
        }

        break;

    case 5:
        if (i == 1)  {
            vh->copyTo(display::graphics.legacyScreen(), 153, 1, 5, 49, 52, 71);
        } else {
            vh->copyTo(display::graphics.legacyScreen(), 153, 26, 5, 49, 52, 71);
        }

        break;

    default:
        break;
    }

    TRACE1("<-Toggle()");
    return;
}


/* Draws a notched box outline for a mission parameter button.
 *
 * This is a custom form of the standard InBox/OutBox functions, using
 * the same color choices.
 *
 * \param x   The x coordinate of the upper-left corner.
 * \param y   The y coordinate of the upper-left corner.
 * \param st  1 if the button is depressed, 0 otherwise
 */
void TogBox(int x, int y, int st)
{
    TRACE4("->TogBox(x %d, y %d, st %d)", x, y, st);
    char sta[2][2] = {{2, 4}, {4, 2}};

    display::graphics.setForegroundColor(sta[st][0]);
    grMoveTo(0 + x, y + 32);
    grLineTo(0 + x, y + 0);
    grLineTo(34 + x, y + 0);
    display::graphics.setForegroundColor(sta[st][1]);
    grMoveTo(x + 0, y + 33);
    grLineTo(23 + x, y + 33);
    grLineTo(23 + x, y + 23);
    grLineTo(x + 35, y + 23);
    grLineTo(x + 35, y + 0);

    TRACE1("<-TogBox()");
    return;
}


/* Set the mission navigation buttons to match the parameters of the
 * chosen mission.
 *
 * \param X  the mission code (mStr.Index or MissionType.MissionCode).
 * \param nav TODO.
 */
void PianoKey(int X, MissionNavigator &nav)
{
    TRACE2("->PianoKey(X %d)", X);

    if (! nav.docking.lock) {
        nav.docking.value = missionData[X].Doc;
        Toggle(1, nav.docking.value);
    }

    if (! nav.EVA.lock) {
        nav.EVA.value = missionData[X].EVA;
        Toggle(2, nav.EVA.value);
    }

    if (! nav.LM.lock) {
        nav.LM.value = missionData[X].LM;
        Toggle(3, nav.LM.value);
    }

    if (! nav.joint.lock) {
        nav.joint.value = missionData[X].Jt;
        Toggle(4, nav.joint.value ? 0 : 1);
    }

    if (! nav.duration.lock) {
        nav.duration.value = missionData[X].Days;
        assert(nav.duration.value >= 0);
        Toggle(5, nav.duration.value ? 1 : 0);

        if (nav.duration.value) {
            DrawPie(nav.duration.value);
        }
    }

    DrawLocks(nav);
    TRACE1("<-PianoKey()");
    return;
}


/* Draw a piechart with 0-6 pieces, filled in clockwise starting at the
 * top.
 *
 * This relies on the global buffer vh, which must have been created
 * prior. The Future Missions button art is loaded into vh by this
 * function.
 *
 * TODO: Check to ensure 0 <= s <= 6.
 *
 * \param s  How many slices are filled in on the piechart.
 */
void DrawPie(int s)
{
    int off;

    if (s == 0) {
        off = 1;
    } else {
        off = s * 20;
    }

    vh->copyTo(display::graphics.legacyScreen(), off, 1, 7, 51, 25, 69);
    return;
}


/* Draws a restriction (lock) button in its active (restricted) state.
 * The restriction button indicates whether the linked mission parameter
 * setting may be modified.
 *
 * The button index is:
 *   1: Mission Duration
 *   2: Docking status
 *   3: EVA status
 *   4: Lunar Module status
 *   5: Joint Mission status
 *
 * TODO: Edit parameters to use the same values as Toggle, etc.
 *
 * \param s  The button index.
 */
void PlaceRX(int s)
{
    switch (s) {
    case 1:
        fill_rectangle(44, 75, 52, 81, 8);
        break;

    case 2:
        fill_rectangle(81, 75, 89, 81, 8);
        break;

    case 3:
        fill_rectangle(118, 75, 126, 81, 8);
        break;

    case 4:
        fill_rectangle(155, 75, 163, 81, 8);
        break;

    case 5:
        fill_rectangle(192, 75, 200, 81, 8);
        break;

    default:
        break;
    }

    return;
}

/* Draws a restriction (lock) button in its inactive (unrestricted)
 * state. The restriction button indicates whether the linked mission
 * parameter setting may be modified.
 *
 * The button index is:
 *   1: Mission Duration
 *   2: Docking status
 *   3: EVA status
 *   4: Lunar Module status
 *   5: Joint Mission status
 *
 * TODO: Use the same parameter input as Toggle, etc.
 *
 * \param s  The button index.
 */
void ClearRX(int s)
{
    switch (s) {
    case 1:
        fill_rectangle(44, 75, 52, 81, 3);
        break;

    case 2:
        fill_rectangle(81, 75, 89, 81, 3);
        break;

    case 3:
        fill_rectangle(118, 75, 126, 81, 3);
        break;

    case 4:
        fill_rectangle(155, 75, 163, 81, 3);
        break;

    case 5:
        fill_rectangle(192, 75, 200, 81, 3);
        break;

    default:
        break;
    }

    return;
}


/**
 * Determine if the mission is compatible with the requirements locked
 * in the navigator.
 *
 * \param nav a navigator object with locked values for required fields.
 * \param mission a mission template.
 * \return false if any of the locked navigator values contradict a
 *         mission parameter, true otherwise.
 */
bool NavMatch(const MissionNavigator &nav, const struct mStr &mission)
{
    return (! nav.docking.lock || nav.docking.value == mission.Doc) &&
           (! nav.EVA.lock || nav.EVA.value == mission.EVA) &&
           (! nav.LM.lock || nav.LM.value == mission.LM) &&
           (! nav.joint.lock || nav.joint.value == mission.Jt) &&
           (! nav.duration.lock || nav.duration.value == mission.Days ||
            (mission.Dur && nav.duration.value >= mission.Days));
}


/**
 * Reset all values in the mission navigator to 0 and release all locks.
 *
 * \param nav A set of mission parameters to be cleared.
 */
void NavReset(MissionNavigator &nav)
{
    nav.duration.value = 0;
    nav.docking.value = 0;
    nav.EVA.value = 0;
    nav.LM.value = 0;
    nav.joint.value = 0;
    nav.duration.lock = false;
    nav.docking.lock = false;
    nav.EVA.lock = false;
    nav.LM.lock = false;
    nav.joint.lock = false;
}



/* TODO: Documentation...
 *
 * TODO: This can be tightened up...
 */
int UpSearchRout(int num, char plr, const MissionNavigator &navigator)
{
    bool found = false;
    int orig = num;

    if (++num >= 56 + plr) {
        num = 0;
    }

    while (! found) {

        if (num == Mission_MarsFlyby && MarsFlag == false ||
            num == Mission_JupiterFlyby && JupiterFlag == false ||
            num == Mission_SaturnFlyby && SaturnFlag == false) {
            found = false;
        } else {
            found = NavMatch(navigator, missionData[num]);
        }

        if (num == orig) {
            return 0;
        }

        if (found == false) {
            if (++num > 56 + plr) {
                num = 0;
            }
        }
    } /* end while */

    return num;
}


/* TODO: Documentation...
 *
 * TODO: This can be tightened up...
 */
int DownSearchRout(int num, char plr, const MissionNavigator &navigator)
{
    bool found = false;
    int orig = num;

    if (--num < 0) {
        num = 56 + plr;
    }

    // TODO: Redo while loop so finding match immediately returns num?
    while (! found) {

        if (num == Mission_MarsFlyby && MarsFlag == false ||
            num == Mission_JupiterFlyby && JupiterFlag == false ||
            num == Mission_SaturnFlyby && SaturnFlag == false) {
            found = false;
        } else {
            found = NavMatch(navigator, missionData[num]);
        }

        if (num == orig) {
            return 0;
        }

        if (! found) {
            if (--num < 0) {
                num = 56 + plr;
            }
        }
    } /* end while */

    return num;
}


/* The main control loop for the Future Missions feature.
 */
void Future(char plr)
{
    /** \todo the whole Future()-function is 500 >lines and unreadable */
    TRACE1("->Future(plr)");
    const int MaxDur = 6;
    int pad = 0;
    int setting = -1, prev_setting = -1;

    display::LegacySurface local(166, 9);
    display::LegacySurface local2(177, 197);
    vh = new display::LegacySurface(240, 90);

    unsigned int year = Data->Year;
    unsigned int season = Data->Season;
    TRACE3("--- Setting year=Year (%d), season=Season (%d)", year, season);

    try {
        SetParameters();
    } catch (IOException &err) {
        CRITICAL1(err.what());
        return;
    }

    MarsFlag = MarsInRange(year, season);
    JupiterFlag = JupiterInRange(year, season);
    SaturnFlag = SaturnInRange(year, season);

    while ((pad = FutureCheck(plr, 0)) != 5) {
        keyHelpText = "k011";
        helpText = "i011";
        char misType = 0;
        ClrFut(plr, pad);

        JointFlag = JointMissionOK(plr, pad); // initialize joint flag
        MissionNavigator nav;
        NavReset(nav);

        if (! JointFlag) {
            nav.joint.value = 0;
            nav.joint.lock = true;
        }

        DrawFuture(plr, misType, pad, nav);

        while (1) {
            key = 0;
            GetMouse();

            prev_setting = setting;
            setting = -1;

            // SEG determines the number of control points used in creating
            // the B-splines for drawing the mission flight path.
            // The more control points, the smoother the path should
            // appear.
            if (key == '-' && SEG > 1) {
                SEG--;
            } else if (key == '+' && SEG < 500) {
                SEG++;
            } else if (key >= 65 && key < Bub_Count + 65) {
                setting = key - 65;
            }

            // If the mouse is over one of the Mission Step bubbles,
            // display the step information.
            for (int i = 0; i < Bub_Count; i++) {
                if (x >= StepBub[i].x_cor && x <= StepBub[i].x_cor + 7 &&
                    y >= StepBub[i].y_cor && y <= StepBub[i].y_cor + 7) {
                    setting = i;
                    break;
                }
            }

            if (setting >= 0) {
                if (prev_setting < 0) {
                    local.copyFrom(display::graphics.legacyScreen(), 18, 186, 183, 194);
                }

                if (prev_setting != setting) {
                    ShBox(18, 186, 183, 194);
                    display::graphics.setForegroundColor(1);
                    MisStep(21, 192, Mev[setting].loc);
                }
            } else if (setting < 0 && prev_setting >= 0) {
                local.copyTo(display::graphics.legacyScreen(), 18, 186);
            }

            if (nav.duration.value >= missionData[misType].Days &&
                ((x >= 244 && y >= 5 && x <= 313 && y <= 17 && mousebuttons > 0) ||
                 key == K_ENTER)) {
                InBox(244, 5, 313, 17);
                WaitForMouseUp();

                if (key > 0) {
                    delay(300);
                }

                key = 0;
                OutBox(244, 5, 313, 17);

                // Copy the screen contents to a buffer. If the mission
                // requires a capsule to be assigned, a pop-up will be
                // created listing the options. Once the pop-up is
                // dismissed the screen may be redrawn from the buffer.
                local2.copyFrom(display::graphics.legacyScreen(), 74, 3, 250, 199);
                int NewType = missionData[misType].mCrew;
                Data->P[plr].Future[pad].Duration = nav.duration.value;

                int Ok = HardCrewAssign(plr, pad, misType, NewType);

                local2.copyTo(display::graphics.legacyScreen(), 74, 3);

                if (Ok == 1) {
                    Data->P[plr].Future[pad].Duration = nav.duration.value;
                    break;        // return to launchpad loop
                } else {
                    ClrFut(plr, pad);
                    // Set the Mission code after being cleared.
                    Data->P[plr].Future[pad].MissionCode = misType;
                    continue;
                }
            } else if ((x >= 43 && y >= 74 && x <= 53 && y <= 82 && mousebuttons > 0) ||
                       key == '!') { // Duration restriction lock
                nav.duration.lock = (! nav.duration.lock);

                if (nav.duration.lock) {
                    InBox(43, 74, 53, 82);
                    PlaceRX(1);
                } else {
                    OutBox(43, 74, 53, 82);
                    ClearRX(1);
                }

                WaitForMouseUp();

            } else if (nav.duration.lock != true &&
                       ((x >= 5 && y >= 49 && x <= 53 && y <= 72 && mousebuttons > 0) ||
                        key == '1')) { // Duration toggle
                InBox(5, 49, 53, 72);

                if (nav.duration.value == MaxDur) {
                    nav.duration.value = 0;
                    Toggle(5, 0);
                } else {
                    nav.duration.value++;

                    if (nav.duration.value == 1) {
                        Toggle(5, 1);
                    }

                    DrawPie(nav.duration.value);
                }

                WaitForMouseUp();

                // Why was this line here? Foreground gets set in OutBox
                display::graphics.setForegroundColor(34);
                OutBox(5, 49, 53, 72);
            } else if ((x >= 5 && y >= 74 && x <= 41 && y <= 82 && mousebuttons > 0) ||
                       (key == K_ESCAPE)) { // Reset mission selection
                InBox(5, 74, 41, 82);

                WaitForMouseUp();

                misType = 0;
                NavReset(nav);

                if (JointFlag == false) {
                    nav.joint.value = 0;
                    nav.joint.lock = true;
                    InBox(191, 74, 201, 82);
                    TogBox(166, 49, 1);
                } else {
                    OutBox(191, 74, 201, 82);
                }

                OutBox(5, 49, 53, 72);
                OutBox(43, 74, 53, 82);
                OutBox(80, 74, 90, 82);
                OutBox(117, 74, 127, 82);
                OutBox(154, 74, 164, 82);

                ClrFut(plr, pad);
                DrawMission(plr, 8, 37, misType, pad, 1, nav);
                GetMinus(plr);
                OutBox(5, 74, 41, 82);

            } else if ((x >= 80 && y >= 74 && x <= 90 && y <= 82 && mousebuttons > 0) ||
                       key == '@') { // Docking restriction lock
                nav.docking.lock = (! nav.docking.lock);

                if (nav.docking.lock) {
                    InBox(80, 74, 90, 82);
                    PlaceRX(2);
                } else {
                    OutBox(80, 74, 90, 82);
                    ClearRX(2);
                }

                WaitForMouseUp();

            } else if (nav.docking.lock == false &&
                       (((x >= 55 && y >= 49 && x <= 90 && y <= 82) && mousebuttons > 0) ||
                        key == '2')) { // Docking toggle
                TogBox(55, 49, 1);

                nav.docking.value = nav.docking.value ? 0 : 1;
                Toggle(1, nav.docking.value);

                WaitForMouseUp();
                TogBox(55, 49, 0);

            } else if ((x >= 117 && y >= 74 && x <= 127 && y <= 82 && mousebuttons > 0) ||
                       key == '#') { // EVA Restriction button
                nav.EVA.lock = (! nav.EVA.lock);

                if (nav.EVA.lock) {
                    InBox(117, 74, 127, 82);
                    PlaceRX(3);
                } else {
                    OutBox(117, 74, 127, 82);
                    ClearRX(3);
                }

                WaitForMouseUp();

            } else if (nav.EVA.lock == false &&
                       ((x >= 92 && y >= 49 && x <= 127 && y <= 82 && mousebuttons > 0) ||
                        key == '3')) { // EVA toggle
                TogBox(92, 49, 1);

                nav.EVA.value = nav.EVA.value ? 0 : 1;
                Toggle(2, nav.EVA.value);

                WaitForMouseUp();
                TogBox(92, 49, 0);

            } else if ((x >= 154 && y >= 74 && x <= 164 && y <= 82 && mousebuttons > 0) ||
                       key == '$') { // Lunar Module Restriction button
                nav.LM.lock = (! nav.LM.lock);

                if (nav.LM.lock) {
                    InBox(154, 74, 164, 82);
                    PlaceRX(4);
                } else {
                    OutBox(154, 74, 164, 82);
                    ClearRX(4);
                }

                WaitForMouseUp();

            } else if (nav.LM.lock == false &&
                       ((x >= 129 && y >= 49 && x <= 164 && y <= 82 && mousebuttons > 0) ||
                        key == '4')) { // LEM toggle
                TogBox(129, 49, 1);

                nav.LM.value = nav.LM.value ? 0 : 1;
                Toggle(3, nav.LM.value);

                WaitForMouseUp();
                TogBox(129, 49, 0);

            } else if (JointFlag == true &&
                       ((x > 191 && y >= 74 && x <= 201 && y <= 82 && mousebuttons > 0) ||
                        key == '%')) { // Joint Mission Restriction button
                nav.joint.lock = (! nav.joint.lock);

                if (nav.joint.lock) {
                    InBox(191, 74, 201, 82);
                    PlaceRX(5);
                } else {
                    OutBox(191, 74, 201, 82);
                    ClearRX(5);
                }

                WaitForMouseUp();

            } else if (nav.joint.lock == false && JointFlag == true &&
                       ((x >= 166 && y >= 49 && x <= 201 && y <= 82 && mousebuttons > 0) ||
                        key == '5')) { // Joint Mission
                TogBox(166, 49, 1);

                nav.joint.value = nav.joint.value ? 0 : 1;
                Toggle(4, nav.joint.value ? 0 : 1);

                WaitForMouseUp();
                TogBox(166, 49, 0);

            } else if ((x >= 5 && y >= 84 && x <= 16 && y <= 130 && mousebuttons > 0) ||
                       (key == UP_ARROW)) {
                // Scroll up among Mission Types
                InBox(5, 84, 16, 130);

                for (int i = 0; i < 50; i++) {
                    key = 0;
                    GetMouse();
                    delay(10);

                    if (mousebuttons == 0) {
                        misType = UpSearchRout(misType, plr, nav);
                        Data->P[plr].Future[pad].MissionCode = misType;
                        i = 51;
                    }
                }

                // Keep scrolling while mouse/key is held down.
                while (mousebuttons == 1 || key == UP_ARROW) {
                    misType = UpSearchRout(misType, plr, nav);
                    Data->P[plr].Future[pad].MissionCode = misType;
                    DrawMission(plr, 8, 37, misType, pad, 3, nav);
                    delay(100);
                    key = 0;
                    GetMouse();
                }

                DrawMission(plr, 8, 37, misType, pad, 3, nav);
                OutBox(5, 84, 16, 130);
            } else if ((x >= 5 && y >= 132 && x < 16 && y <= 146 && mousebuttons > 0) ||
                       (key == K_SPACE)) {
                // Turn on Mission Steps display
                InBox(5, 132, 16, 146);
                WaitForMouseUp();
                delay(50);
                misType = Data->P[plr].Future[pad].MissionCode;
                assert(0 <= misType);

                if (misType != 0) {
                    DrawMission(plr, 8, 37, misType, pad, 1, nav);
                } else {
                    DrawMission(plr, 8, 37, misType, pad, 3, nav);
                }

                OutBox(5, 132, 16, 146);
            } else if ((x >= 5 && y >= 148 && x <= 16 && y <= 194 && mousebuttons > 0) ||
                       (key == DN_ARROW)) {
                // Scroll down among Mission Types
                InBox(5, 148, 16, 194);

                for (int i = 0; i < 50; i++) {
                    key = 0;
                    GetMouse();
                    delay(10);

                    if (mousebuttons == 0) {
                        misType = DownSearchRout(misType, plr, nav);
                        Data->P[plr].Future[pad].MissionCode = misType;
                        i = 51;
                    }

                }

                // Keep scrolling while mouse/key is held down.
                while (mousebuttons == 1 || key == DN_ARROW) {
                    misType = DownSearchRout(misType, plr, nav);
                    Data->P[plr].Future[pad].MissionCode = misType;
                    DrawMission(plr, 8, 37, misType, pad, 3, nav);
                    delay(100);
                    key = 0;
                    GetMouse();
                }

                DrawMission(plr, 8, 37, misType, pad, 3, nav);
                OutBox(5, 148, 16, 194);
            }
        }                              // Input while loop
    }                              // Launch pad selection loop

    delete vh;
    vh = NULL;
    TRACE1("<-Future()");
}

/** draws the bubble on the screen,
 * starts with upper left coor
 *
 * \param x x-coord of the upper left corner of the bubble
 * \param y y-coord of the upper left corner of the bubble
 */
void Bd(int x, int y)
{
    int x1, y1, x2, y2;
    x1 = x - 2;
    y1 = y;
    x2 = x - 1;
    y2 = y - 1;
    fill_rectangle(x1, y1, x1 + 8, y1 + 4, 21);
    fill_rectangle(x2, y2, x2 + 6, y2 + 6, 21);
    display::graphics.setForegroundColor(1);
    grMoveTo(x, y + 4);
    /** \note references Bub_Count to determine the number of the character to draw in the bubble */
    draw_character(65 + Bub_Count);
    StepBub[Bub_Count].x_cor = x1;
    StepBub[Bub_Count].y_cor = y1;
    ++Bub_Count;
    return;
}

/** Update the selected mission view with the given duration.
 *
 * TODO: Ensure -1 <= duration <= 6.
 *
 * \param duration  0 for unmanned, 1-6 for duration A through F
 *
 * \todo Link this at whatever place the duration is actually defined
 */
void PrintDuration(int duration)
{
    fill_rectangle(112, 25, 199, 30, 3); // Draw over old duration
    display::graphics.setForegroundColor(5);

    switch (duration) {
    case -1:
        draw_string(112, 30, "NO DURATION");
        break;

    case 0:
        draw_string(112, 30, "NO DURATION");
        break;

    case 1:
        draw_string(112, 30, "1 - 2 DAYS (A)");
        break;

    case 2:
        draw_string(112, 30, "3 - 5 DAYS (B)");
        break;

    case 3:
        draw_string(112, 30, "6 - 7 DAYS (C)");
        break;

    case 4:
        draw_string(112, 30, "8 - 12 DAYS (D)");
        break;

    case 5:
        draw_string(112, 30, "13 - 16 DAYS (E)");
        break;

    case 6:
        draw_string(112, 30, "17 - 20 DAYS (F)");
        break;
    }

    return;
}


/* Prints the name of the selected mission.
 *
 * This writes the name of the mission associated with the given mission
 * code
 *
 * \note This sets the global variable Mis, via GetMisType().
 *
 * \param val  The mission code.
 * \param xx   The x-coordinates for the name block's upper-left corner.
 * \param yy   The y-coordinates for the name block's upper-left corner.
 * \param len  The number of characters at which to start a new line.
 */
void MissionName(int val, int xx, int yy, int len)
{
    TRACE5("->MissionName(val %d, xx %d, yy %d, len %d)",
           val, xx, yy, len);
    int i, j = 0;

    GetMisType(val);

    grMoveTo(xx, yy);

    for (i = 0; i < 50; i++) {
        if (j > len && Mis.Name[i] == ' ') {
            yy += 7;
            j = 0;
            grMoveTo(xx, yy);
        } else {
            draw_character(Mis.Name[i]);
        }

        j++;

        if (Mis.Name[i] == '\0') {
            break;
        }
    }

    TRACE1("<-MissionName");

    return;
}

/**
 * Update the mission display to reflect the given mission, including
 * the Type, name, duration, navigation buttons, and, if selected,
 * flight path.
 *
 * This modifies the global value Mis. Specifically, it calls
 * MissionName(), which modifies Mis.
 *
 * TODO: Move Flight Path illustration to its own function...
 *
 * \param plr Player
 * \param X screen coord for mission name string
 * \param Y screen coord for mission name string
 * \param val the mission type (MissionType.MissionCode / mStr.Index)
 * \param pad the pad (0, 1, or 2) where the mission is being launched.
 * \param bub if set to 0 or 3 the function will not draw stuff
 * \param nav TODO
 */
void DrawMission(char plr, int X, int Y, int val, int pad, char bub,
                 MissionNavigator &nav)
{
    TRACE6("->DrawMission(plr, X %d, Y %d, val %d, int %d, bub %c)",
           X, Y, val, pad, bub);

    memset(Mev, 0x00, sizeof Mev);

    if (bub == 1 || bub == 3) {
        PianoKey(val, nav);
        Bub_Count = 0; // set the initial bub_count
        ClearDisplay();
        fill_rectangle(6, 31, 199, 46, 3);
        fill_rectangle(80, 25, 112, 30, 3);
        display::graphics.setForegroundColor(5);
        draw_string(55, 30, "TYPE: ");
        draw_number(0, 0, val);
        display::graphics.setForegroundColor(5);

        // TODO: Clean this up...
        if (missionData[val].Days > 0) {
            if (nav.duration.lock &&
                nav.duration.value > missionData[val].Days &&
                missionData[val].Dur == 1) {
                PrintDuration(nav.duration.value);
            } else {
                PrintDuration(missionData[val].Days);
            }
        } else {
            PrintDuration(nav.duration.value);
        }
    } else {
        display::graphics.setForegroundColor(1);
    }

    MissionName(val, X, Y, 24);

    if (bub == 3) {
        GetMinus(plr);
    }

    if (bub == 0 || bub == 3) {
        return;
    }

    // Read steps from missStep.dat
    FILE *MSteps = sOpen("missSteps.dat", "r", FT_DATA);

    if (! MSteps || fgets(missStep, 1024, MSteps) == NULL) {
        memset(missStep, 0, sizeof missStep);
    }

    while (!feof(MSteps) && ((missStep[0] - 0x30) * 10 + (missStep[1] - 0x30)) != val) {
        if (fgets(missStep, 1024, MSteps) == NULL) {
            break;
        }
    }

    fclose(MSteps);

    for (int n = 2; missStep[n] != 'Z'; n++) {
        switch (missStep[n]) {
        case 'A':
            Draw_IJ(B_Mis(++n));
            break;

        case 'B':
            Draw_IJV(B_Mis(++n));
            break;

        case 'C':
            OrbOut(B_Mis(n + 1), B_Mis(n + 2), B_Mis(n + 3));
            n += 3;
            break;

        case 'D':
            LefEarth(B_Mis(n + 1), B_Mis(n + 2));
            n += 2;
            break;

        case 'E':
            OrbIn(B_Mis(n + 1), B_Mis(n + 2), B_Mis(n + 3));
            n += 3;
            break;

        case 'F':
            OrbMid(B_Mis(n + 1), B_Mis(n + 2), B_Mis(n + 3), B_Mis(n + 4));
            n += 4;
            break;

        case 'G':
            LefOrb(B_Mis(n + 1), B_Mis(n + 2), B_Mis(n + 3), B_Mis(n + 4));
            n += 4;
            break;

        case 'H':
            Draw_LowS(B_Mis(n + 1), B_Mis(n + 2), B_Mis(n + 3), B_Mis(n + 4), B_Mis(n + 5), B_Mis(n + 6));
            n += 6;
            break;

        case 'I':
            Fly_By();
            break;

        case 'J':
            VenMarMerc(B_Mis(++n));
            break;

        case 'K':
            Draw_PQR();
            break;

        case 'L':
            Draw_PST();
            break;

        case 'M':
            Draw_GH(B_Mis(n + 1), B_Mis(n + 2));
            n += 2;
            break;

        case 'N':
            Q_Patch();
            break;

        case 'O':
            RghtMoon(B_Mis(n + 1), B_Mis(n + 2));
            n += 2;
            break;

        case 'P':
            DrawLunPas(B_Mis(n + 1), B_Mis(n + 2), B_Mis(n + 3), B_Mis(n + 4));
            n += 4;
            break;

        case 'Q':
            DrawLefMoon(B_Mis(n + 1), B_Mis(n + 2));
            n += 2;
            break;

        case 'R':
            DrawSTUV(B_Mis(n + 1), B_Mis(n + 2), B_Mis(n + 3), B_Mis(n + 4));
            n += 4;
            break;

        case 'S':
            Draw_HighS(B_Mis(n + 1), B_Mis(n + 2), B_Mis(n + 3));
            n += 3;
            break;

        case 'T':
            DrawMoon(B_Mis(n + 1), B_Mis(n + 2), B_Mis(n + 3), B_Mis(n + 4), B_Mis(n + 5), B_Mis(n + 6), B_Mis(n + 7));
            n += 7;
            break;

        case 'U':
            LefGap(B_Mis(++n));
            break;

        case 'V':
            S_Patch(B_Mis(++n));
            break;

        case 'W':
            DrawZ();
            break;

        default :
            break;
        }
    }

    gr_sync();
    MissionCodes(plr, val, pad);
    TRACE1("<-DrawMission()");
}  // end function DrawMission

/* vim: set noet ts=4 sw=4 tw=77: */
