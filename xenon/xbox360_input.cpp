#include "../source/stubs.h"
// Reemplazo de source/xenon_input/input.c para XDK / Xbox 360 (.xex)
// Mismo comportamiento y mismo mapeo de pad_mode que el original, pero
// leyendo el mando via XInput (XInputGetState) en vez de usb_do_poll()
// / get_controller_data() de libxenon.

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <xtl.h>

#include "input.h"
#include "winlnxdefs.h"
#include "main/main.h"
#include "api/m64p_plugin.h"
#include "r4300/r4300.h"

const char * pad_mode_name[]=
{
    "Stick / D-pad / C-buttons",
    "Stick / C-buttons / D-pad",
    "D-pad / Stick / C-buttons",
    "D-pad / C-buttons / Stick",
    "C-buttons / D-pad / Stick",
    "C-buttons / Stick / D-pad",
};

static CONTROL_INFO ControlInfo;

extern "C" void initiateControllers(CONTROL_INFO Control_Info)
{
    int i;
    ControlInfo = Control_Info;
    for(i=0;i<4;++i) ControlInfo.Controls[i].Present = TRUE;
    ControlInfo.Controls[0].Plugin = PLUGIN_MEMPAK;
}

#define STICK_DEAD_ZONE (32768*0.15)
#define STICK_FACTOR (0.70)

#define TRIGGER_THRESHOLD 100
#define STICK_THRESHOLD 20000

static int handleStickDeadZone(int value)
{
    if(abs(value)<STICK_DEAD_ZONE)
        return 0;

int sign=-1;
    if (value>=0) sign=1;
    int dz=(int)(STICK_DEAD_ZONE*sign);
    return (int)((value-dz)*STICK_FACTOR);
}
#include "xbox360_input.h"
static void poll_xinput(int Control, struct controller_data_s * c)
{
    XINPUT_STATE state;
    ZeroMemory(&state, sizeof(XINPUT_STATE));

    if (XInputGetState(Control, &state) != ERROR_SUCCESS)
    {
        ZeroMemory(c, sizeof(struct controller_data_s));
        return;
    }

    const XINPUT_GAMEPAD & gp = state.Gamepad;

    c->start = (gp.wButtons & XINPUT_GAMEPAD_START) ? 1 : 0;
    c->a     = (gp.wButtons & XINPUT_GAMEPAD_A) ? 1 : 0;
    c->b     = (gp.wButtons & XINPUT_GAMEPAD_B) ? 1 : 0;
    c->x     = (gp.wButtons & XINPUT_GAMEPAD_X) ? 1 : 0;
    c->y     = (gp.wButtons & XINPUT_GAMEPAD_Y) ? 1 : 0;
    c->lb    = (gp.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) ? 1 : 0;
    c->rb    = (gp.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) ? 1 : 0;

    c->lt = gp.bLeftTrigger;
    c->rt = gp.bRightTrigger;

    c->up    = (gp.wButtons & XINPUT_GAMEPAD_DPAD_UP) ? 1 : 0;
    c->down  = (gp.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) ? 1 : 0;
    c->left  = (gp.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) ? 1 : 0;
    c->right = (gp.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) ? 1 : 0;

    c->s1_x = gp.sThumbLX;
    c->s1_y = gp.sThumbLY;
    c->s2_x = gp.sThumbRX;
    c->s2_y = gp.sThumbRY;
}

extern "C" void getKeys(int Control, BUTTONS *Keys)
{
    static struct controller_data_s cdata[4], *c;
    BUTTONS b;
    memset(&b, 0, sizeof(b));

    poll_xinput(Control, &cdata[Control]);
    c = &cdata[Control];

    b.START_BUTTON=c->start;

    b.A_BUTTON=c->a;
    b.B_BUTTON=c->b;
    b.Z_TRIG=c->x || c->y || (c->rt>TRIGGER_THRESHOLD) || (c->lt>TRIGGER_THRESHOLD);

    b.L_TRIG=c->lb;
    b.R_TRIG=c->rb;

    switch(pad_mode)
    {
        case PADMODE_SDC:
            b.X_AXIS=handleStickDeadZone(c->s1_x)/256;
            b.Y_AXIS=handleStickDeadZone(c->s1_y)/256;

            b.U_DPAD=c->up;
            b.D_DPAD=c->down;
            b.L_DPAD=c->left;
            b.R_DPAD=c->right;

            b.U_CBUTTON=c->s2_y>STICK_THRESHOLD;
            b.D_CBUTTON=c->s2_y<-STICK_THRESHOLD;
            b.L_CBUTTON=c->s2_x<-STICK_THRESHOLD;
            b.R_CBUTTON=c->s2_x>STICK_THRESHOLD;
            break;
        case PADMODE_SCD:
            b.X_AXIS=handleStickDeadZone(c->s1_x)/256;
            b.Y_AXIS=handleStickDeadZone(c->s1_y)/256;

            b.U_CBUTTON=c->up;
            b.D_CBUTTON=c->down;
            b.L_CBUTTON=c->left;
            b.R_CBUTTON=c->right;

            b.U_DPAD=c->s2_y>STICK_THRESHOLD;
            b.D_DPAD=c->s2_y<-STICK_THRESHOLD;
            b.L_DPAD=c->s2_x<-STICK_THRESHOLD;
            b.R_DPAD=c->s2_x>STICK_THRESHOLD;
            break;
        case PADMODE_DSC:
            b.U_DPAD=c->s1_y>STICK_THRESHOLD;
            b.D_DPAD=c->s1_y<-STICK_THRESHOLD;
            b.L_DPAD=c->s1_x<-STICK_THRESHOLD;
            b.R_DPAD=c->s1_x>STICK_THRESHOLD;

            b.X_AXIS=(c->left?-128:0) + (c->right?127:0);
            b.Y_AXIS=(c->up?127:0) + (c->down?-128:0);

            b.U_CBUTTON=c->s2_y>STICK_THRESHOLD;
            b.D_CBUTTON=c->s2_y<-STICK_THRESHOLD;
            b.L_CBUTTON=c->s2_x<-STICK_THRESHOLD;
            b.R_CBUTTON=c->s2_x>STICK_THRESHOLD;
            break;
        case PADMODE_DCS:
            b.U_DPAD=c->s1_y>STICK_THRESHOLD;
            b.D_DPAD=c->s1_y<-STICK_THRESHOLD;
            b.L_DPAD=c->s1_x<-STICK_THRESHOLD;
            b.R_DPAD=c->s1_x>STICK_THRESHOLD;

            b.U_CBUTTON=c->up;
            b.D_CBUTTON=c->down;
            b.L_CBUTTON=c->left;
            b.R_CBUTTON=c->right;

            b.X_AXIS=handleStickDeadZone(c->s2_x)/256;
            b.Y_AXIS=handleStickDeadZone(c->s2_y)/256;
            break;
        case PADMODE_CDS:
            b.U_CBUTTON=c->s1_y>STICK_THRESHOLD;
            b.D_CBUTTON=c->s1_y<-STICK_THRESHOLD;
            b.L_CBUTTON=c->s1_x<-STICK_THRESHOLD;
            b.R_CBUTTON=c->s1_x>STICK_THRESHOLD;

            b.U_DPAD=c->up;
            b.D_DPAD=c->down;
            b.L_DPAD=c->left;
            b.R_DPAD=c->right;

            b.X_AXIS=handleStickDeadZone(c->s2_x)/256;
            b.Y_AXIS=handleStickDeadZone(c->s2_y)/256;
            break;
        case PADMODE_CSD:
            b.U_CBUTTON=c->s1_y>STICK_THRESHOLD;
            b.D_CBUTTON=c->s1_y<-STICK_THRESHOLD;
            b.L_CBUTTON=c->s1_x<-STICK_THRESHOLD;
            b.R_CBUTTON=c->s1_x>STICK_THRESHOLD;

            b.X_AXIS=(c->left?-128:0) + (c->right?127:0);
            b.Y_AXIS=(c->up?127:0) + (c->down?-128:0);

            b.U_DPAD=c->s2_y>STICK_THRESHOLD;
            b.D_DPAD=c->s2_y<-STICK_THRESHOLD;
            b.L_DPAD=c->s2_x<-STICK_THRESHOLD;
            b.R_DPAD=c->s2_x>STICK_THRESHOLD;
            break;
    }

    Keys->Value = b.Value;
}

void controllerCommand(int Control, unsigned char *Command)
{
}

void readController(int Control, unsigned char *Command)
{
}

void get_controller_data(struct controller_data_s * c, int Control)
{
    poll_xinput(Control, c);
    XINPUT_STATE state;
    ZeroMemory(&state, sizeof(XINPUT_STATE));
    if (XInputGetState(Control, &state) == ERROR_SUCCESS)
    {
        c->back = (state.Gamepad.wButtons & XINPUT_GAMEPAD_BACK) ? 1 : 0;
    }
    else
    {
        c->back = 0;
    }
    c->logo = 0; // XInput no expone el boton Guide/Xbox
}
