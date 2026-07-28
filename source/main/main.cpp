#include "../stubs.h"
/**
 * Mupen64 - main.c
 * Copyright (C) 2002 Hacktarux
 *
 * Mupen64 homepage: http://mupen64.emulation64.com
 * email address: hacktarux@yahoo.fr
 * 
 * If you want to contribute to the project please contact
 * me first (maybe someone is already making what you are
 * planning to do).
 *
 *
 * This program is free software; you can redistribute it and/
 * or modify it under the terms of the GNU General Public Li-
 * cence as published by the Free Software Foundation; either
 * version 2 of the Licence, or any later version.
 *
 * This program is distributed in the hope that it will be use-
 * ful, but WITHOUT ANY WARRANTY; without even the implied war-
 * ranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public Licence for more details.
 *
 * You should have received a copy of the GNU General Public
 * Licence along with this program; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139,
 * USA.
 *
**/

/* This is the command line version of the MUPEN64's entry point,
 * if you want to implement an interface, you should look here
 */

// Emulateur Nintendo 64, MUPEN64, Fichier Principal 
// main.c
#include <stdio.h>
#include <xtl.h>

extern "C" FILE *_dbglog = NULL;
static void DLOG(const char *msg) {
    if (_dbglog) { fprintf(_dbglog, "%s\n", msg); fflush(_dbglog); }
}

#define M64P_CORE_PROTOTYPES 1

#include "version.h"

#include <stdlib.h>
#include <fcntl.h>
#if !defined(_XBOX)
#include <unistd.h>
#endif

#include "winlnxdefs.h"

extern "C" {
	#include "main.h"
	#include "guifuncs.h"
	#include "rom.h"
	#include "r4300/r4300.h"
	#include "r4300/recomph.h"
	#include "memory/memory.h"
	#include "plugin/plugin.h"
	#include "savestates.h"

	#include "api/config_core.h"
	#include "api/callbacks.h"
}

#include "api/m64p_config.h"


#include <malloc.h>
#include <signal.h>
#include <math.h>
#include <stdarg.h>

#include <xenon/d3d9_backend.h>
#include <xenon/xbox360_input.h>

#undef MAX_PATH
#undef X_OK
#include "../zlx/Browser.h"
#include "../zlx/Draw.h"
#include "../zlx/Hw.h"
#include "../zlx/GlobalVideo.h"


#include "Rice_GX_Xenos/xenos_math.h"
#include "Rice_GX_Xenos/COLOR.h"
#include "Rice_GX_Xenos/Config.h"
#include "Rice_GX_Xenos/Config.h"
#include "xenon_input/input.h"
#include "r4300/ppc/Wrappers.h"

/* version number for Core config section */
#define CONFIG_PARAM_VERSION 1.01

extern unsigned char inc_about[];
void * tex_about;

ZLX::Font Font;
ZLX::Browser Browser;
lpBrowserActionEntry enh_action;
lpBrowserActionEntry cpu_action;
lpBrowserActionEntry lim_action;
lpBrowserActionEntry pad_action;

m64p_handle g_CoreConfig = NULL;
int g_MemHasBeenBSwapped = 0;

int use_framelimit = 1;
int use_expansion = 1;
int pad_mode = 0;

extern SettingInfo TextureEnhancementSettings[];

char txtbuffer[1024];

int run_rom(char * romfile);

void WaitNoButtonPress()
{
	struct controller_data_s c = {0};

	for(;;){
		XINPUT_STATE __xis; XInputGetState(0,&__xis);
		get_controller_data(&c, 0);
		
		if(!(c.a || c.b || c.x || c.y || c.back || c.logo || c.start ||
				c.rb || c.lb || c.up || c.down || c.left || c.right))
			return;
	}
}

void ActionAbout(void * other)
{
	struct controller_data_s ctrl = {0};
	float x=0.2,y=-0.75,nl=0.1;
	
	WaitNoButtonPress();

	// Begin to draw
	Browser.Begin();

	ZLX::Draw::DrawTexturedRect(-1.0f, -1.0f, 2.0f, 2.0f, tex_about);

	Browser.getFont()->Begin();

	Browser.getFont()->DrawTextF(MUPEN_NAME " version " MUPEN_VERSION, -1, x,y);y+=nl;
	y+=nl*2;
	Browser.getFont()->DrawTextF("Credits:", -1, x,y);y+=nl;
	Browser.getFont()->DrawTextF("Wii64 / Mupen64 teams (guess why :)", -1, x,y);y+=nl;
	Browser.getFont()->DrawTextF("GliGli (Xbox 360 port)", -1, x,y);y+=nl;
	Browser.getFont()->DrawTextF("Ced2911 (GUI library)", -1, x,y);y+=nl;
	Browser.getFont()->DrawTextF("Razkar (Backgrounds)", -1, x,y);y+=nl;
	Browser.getFont()->DrawTextF("Ported to .xex with XDK", -1, x,y);y+=nl;

	Browser.getFont()->End();

	// Draw all text + form
	Browser.Render();

	// Draw is finished
	Browser.End();

	for(;;){
		XINPUT_STATE __xis; XInputGetState(0,&__xis);
		ctrl.a=(__xis.Gamepad.wButtons & XINPUT_GAMEPAD_A)?1:0; ctrl.b=(__xis.Gamepad.wButtons & XINPUT_GAMEPAD_B)?1:0;
		
		if(ctrl.a || ctrl.b){
			return;
		}
	}

	WaitNoButtonPress();
}

void ActionLaunchFile(void * param)
{
        char * filename = (char *)param;
	WaitNoButtonPress();

	if( run_rom(filename) )
	{
		sprintf(txtbuffer,"Could not load file:\n\n%s\n\nIt is probably not a N64 rom.",filename);
		Browser.Alert(txtbuffer);
	}
	
	WaitNoButtonPress();
}

void ActionShutdown(void * unused)
{
    XLaunchNewImage(NULL, 0);
	for(;;);
}

void ActionReboot(void * unused)
{
    XLaunchNewImage(NULL, 0);
	for(;;);
}

void ActionXell(void * unused)
{
    exit(0);
}

void SetEnhName()
{
#ifdef XENOS_GFX
	if(cache.enable2xSaI)
        enh_action->name = "Texture enhancement: 2xSAI";
	else
        enh_action->name = "Texture enhancement: None";
#else
	static char en[256]="";
	
	strcpy(en,"Textures: ");
	strcat(en,TextureEnhancementSettings[options.textureEnhancement].description);
	enh_action->name = en;
#endif
}

void ActionToggleEnh(void * other)
{
#ifdef XENOS_GFX
	cache.enable2xSaI=!cache.enable2xSaI;
#else
	options.textureEnhancement=(options.textureEnhancement+1)%TEXTURE_SHARPEN_ENHANCEMENT;
#endif
	
	SetEnhName();
}

void SetCpuName()
{
	switch(r4300emu)
	{
		case CORE_DYNAREC:
	        switch(failsafeRec)
			{
			case 0:
				cpu_action->name = "CPU core: Dynarec";
				break;
			case FAILSAFE_REC_NO_LINK:
				cpu_action->name = "CPU core: Dynarec (No linking)";
				break;
			case FAILSAFE_REC_NO_VM:
				cpu_action->name = "CPU core: Dynarec (No VM)";
				break;
			case FAILSAFE_REC_NO_VM|FAILSAFE_REC_NO_LINK:
				cpu_action->name = "CPU core: Dynarec (No VM & no linking)";
				break;
			}
			break;
		case CORE_INTERPRETER:
	        cpu_action->name = "CPU core: Interpreter (Cached)";
			break;
		case CORE_PURE_INTERPRETER:
	        cpu_action->name = "CPU core: Interpreter";
			break;
	}
}
void ActionToggleCpu(void * other)
{
	// Forzado: siempre modo interprete puro, nunca dynarec
	r4300emu=CORE_PURE_INTERPRETER;
	failsafeRec=0;

	SetCpuName();
}

void SetLimName()
{
	if(use_framelimit)
        lim_action->name = "Framerate limiting: Yes";
	else
        lim_action->name = "Framerate limiting: No";
}

void ActionToggleLim(void * other)
{
	use_framelimit=!use_framelimit;
	
	SetLimName();
}


void SetPadName()
{
	static char pm[256]="";
	
	strcpy(pm,"Controls (l->r): ");
	strcat(pm,pad_mode_name[pad_mode]);
	
	pad_action->name=pm;
}

void ActionTogglePad(void * other)
{
	pad_mode=(pad_mode+1)%6;
	
	SetPadName();
}

void cls_GUI()
{
	Browser.Begin();
	ZLX::Draw::DrawColoredRect(-1,-1,2,2,0xff000000);
    ZLX::g_pVideoDevice->Clear(CLEAR_COLOR_BUFFER, 0xff000000);
	Browser.End();
}

void do_GUI()
{

	tex_about = ZLX::loadPNGFromMemory(inc_about);
	
	{
        lpBrowserActionEntry action = new BrowserActionEntry();
        action->name = "Controls / about Mupen64-360 ...";
        action->action = ActionAbout;
        action->param = NULL;
        Browser.AddAction(action);
    }
	{
        lpBrowserActionEntry action = new BrowserActionEntry();
        action->name = "-";
        action->action = NULL;
        action->param = NULL;
        Browser.AddAction(action);
    }

    {
        pad_action = new BrowserActionEntry();
        pad_action->param = NULL;
		SetPadName();
        pad_action->action = ActionTogglePad;
        Browser.AddAction(pad_action);
    }

    {
        enh_action = new BrowserActionEntry();
        enh_action->param = NULL;
		SetEnhName();
        enh_action->action = ActionToggleEnh;
        Browser.AddAction(enh_action);
    }

    {
        cpu_action = new BrowserActionEntry();
        cpu_action->param = NULL;
		SetCpuName();
        cpu_action->action = ActionToggleCpu;
        Browser.AddAction(cpu_action);
    }

    {
        lim_action = new BrowserActionEntry();
        lim_action->param = NULL;
		SetLimName();
        lim_action->action = ActionToggleLim;
        Browser.AddAction(lim_action);
    }

	{
        lpBrowserActionEntry action = new BrowserActionEntry();
        action->name = "-";
        action->action = NULL;
        action->param = NULL;
        Browser.AddAction(action);
    }
	{
        lpBrowserActionEntry action = new BrowserActionEntry();
        action->name = "Shutdown";
        action->action = ActionShutdown;
        action->param = NULL;
        Browser.AddAction(action);
    }
	{
        lpBrowserActionEntry action = new BrowserActionEntry();
        action->name = "Reboot";
        action->action = ActionReboot;
        action->param = NULL;
        Browser.AddAction(action);
    }
    {
        lpBrowserActionEntry action = new BrowserActionEntry();
        action->name = "Return to Xell";
        action->action = ActionXell;
        action->param = NULL;
        Browser.AddAction(action);
    }
	
	// colors
    Browser.SetDefaultColor(0xffffffff);
    Browser.SetSelectedColor(0xffff5050);
	
	// masks
    Browser.SetFocusColor(0xffffffff);
    Browser.SetBlurColor(0x70ffffff);
	
	Browser.SetLaunchAction(ActionLaunchFile);
	
    Browser.Run(ROM_DIR);
}

void display_loading_progress(int p)
{
   Browser.SetProgressValue(p/100.0f);
}

void new_frame()
{
}

void new_vi()
{
	struct controller_data_s c = {0};

	XINPUT_STATE __xis; XInputGetState(0,&__xis);
	get_controller_data(&c, 0);

    if (c.logo)
	{
		stop=1;
	}

    if (c.back && lim_action)
	{
		ActionToggleLim(NULL);
		WaitNoButtonPress();
	}
}

const char *get_savestatepath(void)
{
    return MUPEN_DIR"sstates\\";
}

const char *get_savesrampath(void)
{
    return MUPEN_DIR"saves\\";
}

void main_message(m64p_msg_level level, unsigned int corner, const char *format, ...)
{
    va_list ap;
    char buffer[2049];
    va_start(ap, format);
    vsnprintf(buffer, 2047, format, ap);
    buffer[2048]='\0';
    va_end(ap);

    DebugMessage(level, "%s", buffer);
}

int main_set_core_defaults(void)
{
    float fConfigParamsVersion;
    int bSaveConfig = 0, bUpgrade = 0;

    if (ConfigGetParameter(g_CoreConfig, "Version", M64TYPE_FLOAT, &fConfigParamsVersion, sizeof(float)) != M64ERR_SUCCESS)
    {
        DebugMessage(M64MSG_WARNING, "No version number in 'Core' config section. Setting defaults.");
        ConfigDeleteSection("Core");
        ConfigOpenSection("Core", &g_CoreConfig);
        bSaveConfig = 1;
    }
    else if (((int) fConfigParamsVersion) != ((int) CONFIG_PARAM_VERSION))
    {
        DebugMessage(M64MSG_WARNING, "Incompatible version %.2f in 'Core' config section: current is %.2f. Setting defaults.", fConfigParamsVersion, (float) CONFIG_PARAM_VERSION);
        ConfigDeleteSection("Core");
        ConfigOpenSection("Core", &g_CoreConfig);
        bSaveConfig = 1;
    }
    else if ((CONFIG_PARAM_VERSION - fConfigParamsVersion) >= 0.0001f)
    {
        float fVersion = (float) CONFIG_PARAM_VERSION;
        ConfigSetParameter(g_CoreConfig, "Version", M64TYPE_FLOAT, &fVersion);
        DebugMessage(M64MSG_INFO, "Updating parameter set version in 'Core' config section to %.2f", fVersion);
        bUpgrade = 1;
        bSaveConfig = 1;
    }

    /* parameters controlling the operation of the core */
    ConfigSetDefaultFloat(g_CoreConfig, "Version", (float) CONFIG_PARAM_VERSION,  "Mupen64Plus Core config parameter set version number.  Please don't change this version number.");
    ConfigSetDefaultBool(g_CoreConfig, "OnScreenDisplay", 1, "Draw on-screen display if True, otherwise don't draw OSD");
#if defined(DYNAREC)
    ConfigSetDefaultInt(g_CoreConfig, "R4300Emulator", 2, "Use Pure Interpreter if 0, Cached Interpreter if 1, or Dynamic Recompiler if 2 or more");
#else
    ConfigSetDefaultInt(g_CoreConfig, "R4300Emulator", 1, "Use Pure Interpreter if 0, Cached Interpreter if 1, or Dynamic Recompiler if 2 or more");
#endif
    ConfigSetDefaultBool(g_CoreConfig, "NoCompiledJump", 0, "Disable compiled jump commands in dynamic recompiler (should be set to False) ");
    ConfigSetDefaultBool(g_CoreConfig, "DisableExtraMem", 0, "Disable 4MB expansion RAM pack. May be necessary for some games");
    ConfigSetDefaultBool(g_CoreConfig, "AutoStateSlotIncrement", 0, "Increment the save state slot after each save operation");
    ConfigSetDefaultBool(g_CoreConfig, "EnableDebugger", 0, "Activate the R4300 debugger when ROM execution begins, if core was built with Debugger support");
    ConfigSetDefaultInt(g_CoreConfig, "CurrentStateSlot", 0, "Save state slot (0-9) to use when saving/loading the emulator state");
    ConfigSetDefaultString(g_CoreConfig, "ScreenshotPath", "", "Path to directory where screenshots are saved. If this is blank, the default value of ${UserConfigPath}/screenshot will be used");
    ConfigSetDefaultString(g_CoreConfig, "SaveStatePath", "", "Path to directory where emulator save states (snapshots) are saved. If this is blank, the default value of ${UserConfigPath}/save will be used");
    ConfigSetDefaultString(g_CoreConfig, "SaveSRAMPath", "", "Path to directory where SRAM/EEPROM data (in-game saves) are stored. If this is blank, the default value of ${UserConfigPath}/save will be used");
    ConfigSetDefaultString(g_CoreConfig, "SharedDataPath", "", "Path to a directory to search when looking for shared data files");

    /* handle upgrades */
    if (bUpgrade)
    {
        if (fConfigParamsVersion < 1.01f)
        {  // added separate SaveSRAMPath parameter in v1.01
            const char *pccSaveStatePath = ConfigGetParamString(g_CoreConfig, "SaveStatePath");
            if (pccSaveStatePath != NULL)
                ConfigSetParameter(g_CoreConfig, "SaveSRAMPath", M64TYPE_STRING, pccSaveStatePath);
        }
    }

    if (bSaveConfig)
        ConfigSaveSection("Core");

    return 0;
}
u8 * alloc_read_file (char *filename, u32 & osize)
{
    u32 size=0;
    u8 * buf;
    FILE * f = fopen(filename, "rb");
    if (f == NULL)
    {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf=(u8*)malloc(size);
    printf("Plain ROM file, size=%d\n",size);
    size_t r = fread(buf, 1, size, f);
    if (r != size)
    {
        fclose(f);
        free(buf);
        return NULL;
    }
    fclose(f);
    osize=size;
    return buf;
}
int run_rom(char * romfile)
{
    u32 rom_size;
    char dbg[256];
    sprintf(dbg, "[run_rom] Loading: %s", romfile);
    DLOG(dbg);
    unsigned char * rom_data=alloc_read_file(romfile,rom_size);
	
	if (!rom_data)
	{
		DLOG("[run_rom] FAILED to read ROM file");
		return 1;
	}
    sprintf(dbg, "[run_rom] ROM loaded, size=%u", rom_size);
    DLOG(dbg);
	
	m64p_error err=open_rom(rom_data,rom_size);
	
	if(err!=M64ERR_SUCCESS)
	{
		DLOG("[run_rom] open_rom FAILED");
		return 2;
	}

	cls_GUI();
	
    if (g_MemHasBeenBSwapped == 0)
    {
        init_memory(1);
        g_MemHasBeenBSwapped = 1;
    }
    else
    {
        init_memory(0);
    }

	DLOG("[run_rom] plugin_load_plugins...");
	plugin_load_plugins(NULL,NULL,NULL,NULL);
	DLOG("[run_rom] plugin_load_plugins DONE");

	DLOG("[run_rom] gfx.romOpen...");
	gfx.romOpen();
	DLOG("[run_rom] gfx.romOpen DONE");

	DLOG("[run_rom] audio.romOpen...");
	audio.romOpen();
	DLOG("[run_rom] audio.romOpen DONE");

	DLOG("[run_rom] cpu_init...");
	cpu_init();
	DLOG("[run_rom] cpu_init DONE");

	DLOG("[run_rom] go() START...");
	go();
	DLOG("[run_rom] go() DONE");
   
	rsp.romClosed();
	audio.romClosed();
	gfx.romClosed();
	
	free_memory();
	
	close_rom();

	free(rom_data);
	rom_data=NULL;
	
	return 0;
}



extern void EnsureGraphicsDevice(void);
extern FILE *_dbglog;
static void DLOG(const char *msg);
int main ()
{
	_dbglog = fopen("game:\\mupen64-360\\debug.log", "w");
	DLOG("[main] START");

	DLOG("[main] EnsureGraphicsDevice...");
	EnsureGraphicsDevice();
	DLOG("[main] EnsureGraphicsDevice DONE");

	DLOG("[main] InitialiseVideo...");
	ZLX::InitialiseVideo();
	DLOG("[main] InitialiseVideo DONE");

	printf("\n" MUPEN_NAME " version " MUPEN_VERSION "\n\n");

	DLOG("[main] SystemInit...");
	ZLX::Hw::SystemInit(ZLX::INIT_USB|ZLX::INIT_ATA|ZLX::INIT_ATAPI|ZLX::INIT_FILESYSTEM);
	ZLX::Hw::SystemPoll();
	DLOG("[main] SystemInit DONE");

    r4300emu=CORE_PURE_INTERPRETER;
	
	DLOG("[main] ConfigInit...");
	ConfigInit(MUPEN_DIR,MUPEN_DIR);
	DLOG("[main] ConfigInit DONE");

	DLOG("[main] romdatabase_open...");
	romdatabase_open();
	DLOG("[main] romdatabase_open DONE");
	
	DLOG("[main] main_set_core_defaults...");
	main_set_core_defaults();
	DLOG("[main] main_set_core_defaults DONE");

	DLOG("[main] run_rom...");

	const char *romDir = "game:\\mupen64-360\\";
	char romPath[256] = "";
	int found = 0;

	// Scan directory for first .z64/.n64/.v64 file
	{
		char pattern[256];
		snprintf(pattern, sizeof(pattern), "%s*", romDir);
		WIN32_FIND_DATAA fd;
		HANDLE hFind = FindFirstFileA(pattern, &fd);
		if (hFind != INVALID_HANDLE_VALUE) {
			do {
				const char *name = fd.cFileName;
				int len = strlen(name);
				if (len > 4) {
					const char *ext = name + len - 4;
					if (_stricmp(ext, ".z64") == 0 ||
					    _stricmp(ext, ".n64") == 0 ||
					    _stricmp(ext, ".v64") == 0) {
						snprintf(romPath, sizeof(romPath), "%s%s", romDir, name);
						found = 1;
						break;
					}
				}
			} while (FindNextFileA(hFind, &fd));
			FindClose(hFind);
		}
	}

	if (!found) {
		DLOG("[main] No ROM file found in game:\\mupen64-360\\");
		printf("No ROM file found in game:\\mupen64-360\\\n");
		printf("Place a .z64, .n64, or .v64 file there.\n");
	} else {
		DLOG(romPath);
		run_rom(romPath);
	}
	DLOG("[main] run_rom DONE");

	ConfigShutdown();
	romdatabase_close();

	DLOG("[main] EXIT");
	if (_dbglog) { fclose(_dbglog); _dbglog = NULL; }

	return 0;
}
