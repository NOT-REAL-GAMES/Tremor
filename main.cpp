

// Copyright 2025 NOT REAL GAMES
//
// Permission is hereby granted, free of charge, 
// to any person obtaining a copy of this software 
// and associated documentation files(the “Software”), 
// to deal in the Software without restriction, 
// including without limitation the rights to use, copy, 
// modify, merge, publish, distribute, sublicense, and/or 
// sell copies of the Software, and to permit persons to 
// whom the Software is furnished to do so, subject to the 
// following conditions:
//
// The above copyright notice and this permission notice shall 
// be included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, 
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES 
// OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-
// INFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
// BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN 
// AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF 
// OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS 
// IN THE SOFTWARE.

// This file is part of Tremor: a game engine built on a 
// rewrite of the Quake Engine as a foundation. The original 
// Quake Engine is licensed under the GPLv2 license. 

// The Tremor project is not affiliated with or endorsed by id Software.
// idTech 2's dependencies on Quake will be removed from the Tremor project. 

#include <vulkan/vulkan_core.h>
#include <windows.h>
#include <mmsystem.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include <SDL2/SDL_vulkan.h>

#include "atomics.h"

#include <sys/types.h>
#include <errno.h>
#include <io.h>
#include <direct.h>



#include <iostream>
#include <cstring>

#define SDL_MAIN_HANDLED
#define NO_SDL_VULKAN_TYPEDEFS

#define QS_STRINGIFY_(x) #x
#define QS_STRINGIFY(x)	 QS_STRINGIFY_ (x)

#define VKQUAKE_VERSION 0.0
#define VKQUAKE_VER_PATCH 1
#define VKQUAKE_VER_SUFFIX "-dev"

#define COMMA ,
#define NO_COMMA

#define MAX_MAPSTRING 2048
#define MAX_DEMOS	  8
#define MAX_DEMONAME  16

#define TREMOR_VER_STRING	  QS_STRINGIFY (VKQUAKE_VERSION) "." QS_STRINGIFY (VKQUAKE_VER_PATCH) VKQUAKE_VER_SUFFIX


#define ANNOTATE_HAPPENS_BEFORE(x) \
	do                             \
	{                              \
	} while (false)
#define ANNOTATE_HAPPENS_AFTER(x) \
	do                            \
	{                             \
	} while (false)
#define ANNOTATE_HAPPENS_BEFORE_FORGET_ALL(x) \
	do                                        \
	{                                         \
	} while (false)


#define ENGINE_NAME_AND_VER \
	"Tremor"               \
	" " TREMOR_VER_STRING

#define THREAD_STACK_RESERVATION (128ll * 1024ll)
#define MAX_STACK_ALLOC_SIZE	 (512ll * 1024ll)

#define TASKS_MAX_WORKERS	32

#define NUM_INDEX_BITS		 8
#define MAX_PENDING_TASKS	 (1u << NUM_INDEX_BITS)
#define MAX_EXECUTABLE_TASKS 256
#define MAX_DEPENDENT_TASKS	 16
#define MAX_PAYLOAD_SIZE	 128
#define WORKER_HUNK_SIZE	 (1 * 1024 * 1024)
#define WAIT_SPIN_COUNT		 100

#define THREAD_LOCAL  __declspec (thread)

#define SIGNONS 4

#define MAXPRINTMSG 4096

static char	   cvar_null_string[] = "";

static char logfilename[MAX_OSPATH]; // current logfile name
static int	log_fd = -1;			 // log file descriptor

typedef uint64_t task_handle_t;
typedef void (*task_func_t) (void*);
typedef void (*task_indexed_func_t) (int, void*);

static void* Clamp(void* number, void* min, void* max)
{
	int* num = ((int*)number);
	if (((int*)number) < ((int*)min))
		num = ((int*)min);
	else if (((int*)number) > ((int*)max))
		num = ((int*)max);
	return num;
}

static void* Min(void* a, void* b)
{
	return (*(int*)a < *(int*)b) ? a : b;
}

static void* Max(void* a, void* b)
{
	return (*(int*)a > *(int*)b) ? a : b;
}

typedef struct sizebuf_s
{
	bool allowoverflow; // if false, do a Sys_Error
	bool overflowed;	// set to true if the buffer size failed
	byte* data;
	int		 maxsize;
	int		 cursize;
} sizebuf_t;

typedef enum
{
	TASK_TYPE_NONE,
	TASK_TYPE_SCALAR,
	TASK_TYPE_INDEXED,
} task_type_t;

typedef struct
{
	task_type_t		task_type;
	int				num_dependents;
	int				indexed_limit;
	atomic_uint32_t remaining_workers;
	atomic_uint32_t remaining_dependencies;
	uint64_t		epoch;
	void* func;
	SDL_mutex* epoch_mutex;
	SDL_cond* epoch_condition;
	uint8_t			payload[MAX_PAYLOAD_SIZE];
	task_handle_t	dependent_task_handles[MAX_DEPENDENT_TASKS];
} task_t;

typedef struct
{
	atomic_uint32_t head;
	uint32_t		head_padding[15]; // Pad to 64 byte cache line size
	atomic_uint32_t tail;
	uint32_t		tail_padding[15];
	uint32_t		capacity_mask;
	SDL_sem* push_semaphore;
	SDL_sem* pop_semaphore;
	atomic_uint32_t task_indices[1];
} task_queue_t;

typedef struct
{
	atomic_uint32_t index;
	uint32_t		limit;
} task_counter_t;

static int					 num_workers = 0;
static SDL_Thread** worker_threads;
static task_t				 tasks[MAX_PENDING_TASKS];
static task_queue_t* free_task_queue;
static task_queue_t* executable_task_queue;
static task_counter_t* indexed_task_counters;
static uint8_t				 steal_worker_indices[TASKS_MAX_WORKERS * 2];
static THREAD_LOCAL bool is_worker = false;
static THREAD_LOCAL int		 tl_worker_index;


typedef unsigned char byte;
typedef int64_t qfileofs_t;


typedef enum
{
	ca_dedicated,	 // a dedicated server with no ability to start a client
	ca_disconnected, // full screen console with no connection
	ca_connected	 // valid netcon, talking to a server
} cactive_t;

typedef struct
{
	cactive_t state;

	// personalization data sent to server
	char spawnparms[MAX_MAPSTRING]; // to restart a level

	// demo loop control
	int	 demonum;						 // -1 = don't play demos
	char demos[MAX_DEMOS][MAX_DEMONAME]; // when not playing

	// demo recording info must be here, because record is started before
	// entering a map (and clearing client_state_t)
	bool demorecording;
	bool demoplayback;

	// did the user pause demo playback? (separate from cl.paused because we don't
	// want a svc_setpause inside the demo to actually pause demo playback).
	bool demopaused;
	bool demoseeking;
	float	 seektime;
	float	 demospeed;

	// demo file position where the current level starts (after signon packets)
	qfileofs_t demo_prespawn_end;

	bool timedemo;
	int		 forcetrack; // -1 = use normal cd track
	FILE* demofile;
	int		 td_lastframe;	// to meter out one message a frame
	int		 td_startframe; // host_framecount at start
	float	 td_starttime;	// realtime at second frame of timedemo

	// connection information
	int				  signon; // 0 to SIGNONS
	struct qsocket_s* netcon;
	sizebuf_t		  message; // writing buffer to send to server

	char userinfo[8192];
} client_static_t;

typedef enum
{
	dpi_unaware = 0,
	dpi_system_aware = 1,
	dpi_monitor_aware = 2
} dpi_awareness;
typedef BOOL(WINAPI* SetProcessDPIAwareFunc) ();
typedef HRESULT(WINAPI* SetProcessDPIAwarenessFunc) (dpi_awareness value);

static HANDLE hinput, houtput;
static char	  cwd[1024];
static double counter_freq;

size_t max_thread_stack_alloc_size = 0;

typedef struct
{
	const char* basedir;
	const char* userdir; // user's directory on UNIX platforms.
	// if user directories are enabled, basedir
	// and userdir will point to different
	// memory locations, otherwise to the same.
	int			argc;
	char** argv;
	int			errstate;
} parms_t;

typedef enum
{
	CVAR_NONE = 0,
	CVAR_ARCHIVE = (1U << 0),	 // if set, causes it to be saved to config
	CVAR_NOTIFY = (1U << 1),	 // changes will be broadcasted to all players (q1)
	CVAR_SERVERINFO = (1U << 2),   // added to serverinfo will be sent to clients (q1/net_dgrm.c and qwsv)
	CVAR_USERINFO = (1U << 3),	 // added to userinfo, will be sent to server (qwcl)
	CVAR_CHANGED = (1U << 4),
	CVAR_ROM = (1U << 6),
	CVAR_LOCKED = (1U << 8),	 // locked temporarily
	CVAR_REGISTERED = (1U << 10),  // the var is added to the list of variables
	CVAR_CALLBACK = (1U << 16),	 // var has a callback
	CVAR_USERDEFINED = (1U << 17),  // cvar was created by the user/mod, and needs to be saved a bit differently.
	CVAR_AUTOCVAR = (1U << 18),	 // cvar changes need to feed back to qc global changes.
	CVAR_SETA = (1U << 19)   // cvar will be saved with seta.
} cvarflags_t;

typedef void (*cvarcallback_t) (struct cvar_s*);

typedef struct cvar_s
{
	const char* name;
	const char* string;
	cvarflags_t	   flags;
	float		   value;
	const char* default_string; // johnfitz -- remember defaults for reset function
	cvarcallback_t callback;
	struct cvar_s* next;
} cvar_t;



static const char errortxt1[] = "\nERROR-OUT BEGIN\n\n";
static const char errortxt2[] = "\nQUAKE ERROR: ";

void ErrorDialog(const char* errorMsg)
{
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Quake Error", errorMsg, NULL);
}

class Engine {
public:

	bool isDedicated;

	class q {
	public:
		Engine* engine;
		q(Engine e) {
			engine = &e;
		}
		static inline int toupper(int c)
		{
			return ((islower(c)) ? (c & ~('a' - 'A')) : c);
		}
		static inline int tolower(int c)
		{
			return ((isupper(c)) ? (c | ('a' - 'A')) : c);
		}
		static inline int to_ascii(int c)
		{
			return (c & 0x7f);
		}
		static inline int isprint(int c)
		{
			return (c >= 0x20 && c <= 0x7e);
		}
		static inline int isgraph(int c)
		{
			return (c > 0x20 && c <= 0x7e);
		}

		static inline int isspace(int c)
		{
			switch (c)
			{
			case ' ':
			case '\t':
			case '\n':
			case '\r':
			case '\f':
			case '\v':
				return 1;
			}
			return 0;
		}

		static inline int isblank(int c)
		{
			return (c == ' ' || c == '\t');
		}

		static inline int is_ascii(int c)
		{
			return ((c & ~0x7f) == 0);
		}

		static inline int islower(int c)
		{
			return (c >= 'a' && c <= 'z');
		}

		static inline int isupper(int c)
		{
			return (c >= 'A' && c <= 'Z');
		}

		static inline int isalpha(int c)
		{
			return (islower(c) || isupper(c));
		}

		static inline int isdigit(int c)
		{
			return (c >= '0' && c <= '9');
		}

		static inline int isxdigit(int c)
		{
			return (isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'));
		}

		static inline int isalnum(int c)
		{
			return (isalpha(c) || isdigit(c));
		}

		int vsnprintf(char* str, size_t size, const char* format, va_list args)
		{
			int ret;

			ret = _vsnprintf_s(str, size, _TRUNCATE, format, args);

			if (ret < 0)
				ret = (int)size;
			if (size == 0) /* no buffer */
				return ret;
			if ((size_t)ret >= size)
				str[size - 1] = '\0';

			return ret;
		}
		int strcasecmp(const char* s1, const char* s2)
		{
			const char* p1 = s1;
			const char* p2 = s2;
			char		c1, c2;

			if (p1 == p2)
				return 0;

			do
			{
				c1 = tolower(*p1++);
				c2 = tolower(*p2++);
				if (c1 == '\0')
					break;
			} while (c1 == c2);

			return (int)(c1 - c2);
		}

		int strncasecmp(const char* s1, const char* s2, size_t n)
		{
			const char* p1 = s1;
			const char* p2 = s2;
			char		c1, c2;

			if (p1 == p2 || n == 0)
				return 0;

			do
			{
				c1 = tolower(*p1++);
				c2 = tolower(*p2++);
				if (c1 == '\0' || c1 != c2)
					break;
			} while (--n > 0);

			return (int)(c1 - c2);
		}

		char* strcasestr(const char* haystack, const char* needle)
		{
			const size_t len = strlen(needle);

			if (!len)
				return (char*)haystack;

			while (*haystack)
			{
				if (!strncasecmp(haystack, needle, len))
					return (char*)haystack;

				++haystack;
			}

			return NULL;
		}

		char* q_strlwr(char* str)
		{
			char* c;
			c = str;
			while (*c)
			{
				*c = tolower(*c);
				c++;
			}
			return str;
		}

		char* q_strupr(char* str)
		{
			char* c;
			c = str;
			while (*c)
			{
				*c = toupper(*c);
				c++;
			}
			return str;
		}

		char* strdup(const char* str)
		{
			size_t len = strlen(str) + 1;
			char* newstr = (char*)mem->Alloc(len);
			memcpy(newstr, str, len);
			return newstr;
		}

		int snprintf(char* str, size_t size, const char* format, ...)
		{
			int		ret;
			va_list argptr;

			va_start(argptr, format);
			ret = vsnprintf(str, size, format, argptr);
			va_end(argptr);

			return ret;
		}

		int wildcmp(const char* wild, const char* string)
		{ // case-insensitive string compare with wildcards. returns true for a match.
			while (*string)
			{
				if (*wild == '*')
				{
					if (*string == '/' || *string == '\\')
					{
						//* terminates if we get a match on the char following it, or if its a \ or / char
						wild++;
						continue;
					}
					if (wildcmp(wild + 1, string))
						return true;
					string++;
				}
				else if ((tolower(*wild) == tolower(*string)) || (*wild == '?'))
				{
					// this char matches
					wild++;
					string++;
				}
				else
				{
					// failure
					return false;
				}
			}

			while (*wild == '*')
			{
				wild++;
			}
			return !*wild;
		}

		void Info_RemoveKey(char* info, const char* key)
		{ // only shrinks, so no need for max size.
			size_t keylen = strlen(key);

			while (*info)
			{
				char* l = info;
				if (*info++ != '\\')
					break; // error / end-of-string

				if (!strncmp(info, key, keylen) && info[keylen] == '\\')
				{
					// skip the key name
					info += keylen + 1;
					// this is the old value for the key. skip over it
					while (*info && *info != '\\')
						info++;

					// okay, we found it. strip it out now.
					memmove(l, info, strlen(info) + 1);
					return;
				}
				else
				{
					// skip the key
					while (*info && *info != '\\')
						info++;

					// validate that its a value now
					if (*info++ != '\\')
						break; // error
					// skip the value
					while (*info && *info != '\\')
						info++;
				}
			}
		}

		void Info_SetKey(char* info, size_t infosize, const char* key, const char* val)
		{
			size_t keylen = strlen(key);
			size_t vallen = strlen(val);

			Info_RemoveKey(info, key);

			if (vallen)
			{
				char* o = info + strlen(info);
				char* e = info + infosize - 1;

				if (!*key || strchr(key, '\\') || strchr(val, '\\'))
					con->Warning("Info_SetKey(%s): invalid key/value\n", key);
				else if (o + 2 + keylen + vallen >= e)
					con->Warning("Info_SetKey(%s): length exceeds max\n", key);
				else
				{
					*o++ = '\\';
					memcpy(o, key, keylen);
					o += keylen;
					*o++ = '\\';
					memcpy(o, val, vallen);
					o += vallen;

					*o = 0;
				}
			}
		}

		size_t strlcat(char* dst, const char* src, size_t siz)
		{
			char* d = dst;
			const char* s = src;
			size_t		n = siz;
			size_t		dlen;

			/* Find the end of dst and adjust bytes left but don't go past end */
			while (n-- != 0 && *d != '\0')
				d++;
			dlen = d - dst;
			n = siz - dlen;

			if (n == 0)
				return (dlen + strlen(s));
			while (*s != '\0')
			{
				if (n != 1)
				{
					*d++ = *s;
					n--;
				}
				s++;
			}
			*d = '\0';

			return (dlen + (s - src)); /* count does not include NUL */
		}
	};
	class VID {
	public:
		Engine* engine;
		bool fullscreen;
		bool initiialized = false;
		SDL_Window* draw_context;
		static HICON icon;

		//VID_Init()
		VID(Engine e) {
			engine = &e;
			fullscreen = false;
			auto display = new Display();

			_putenv("SDL_VIDEO_CENTERED = center");

			if (SDL_InitSubSystem(SDL_INIT_VIDEO) < 0) { std::cout << ("SDL_Init failed: %s", SDL_GetError()) << std::endl; }

			SetMode(1280, 720, 60, false);
		}
		void PL_SetWindowIcon(void)
		{
			HINSTANCE	  handle;
			SDL_SysWMinfo wminfo;
			HWND		  hwnd;

			handle = GetModuleHandle(NULL);
			icon = LoadIcon(handle, L"icon");

			if (!icon)
				return; /* no icon in the exe */

			SDL_VERSION(&wminfo.version);

			if (SDL_GetWindowWMInfo((SDL_Window*)GetWindow(), &wminfo) != SDL_TRUE)
				return; /* wrong SDL version */

			hwnd = wminfo.info.win.window;
#ifdef _WIN64
			SetClassLongPtr(hwnd, GCLP_HICON, (LONG_PTR)icon);
#else
			SetClassLong(hwnd, GCL_HICON, (LONG)icon);
#endif
		}
		void Gamma_Init(){
			std::cout << "Gamma_Init to be implemented: haven't implemented cvars yet" << std::endl;
		}
		int GetCurrentWidth() {
			int w, h;
			SDL_GetWindowSize(draw_context, &w, &h);
			return w;
		}
		int GetCurrentHeight() {
			int w, h;
			SDL_GetWindowSize(draw_context, &w, &h);
			return h;
		}
		int GetCurrentRefreshRate() {
			SDL_DisplayMode mode;
			SDL_GetCurrentDisplayMode(0, &mode);
			return mode.refresh_rate;
		}
		int GetCurrentBPP() {
			return SDL_BITSPERPIXEL(SDL_GetWindowPixelFormat(draw_context));
		}
		bool GetFullscreen() {
			return (SDL_GetWindowFlags(draw_context) & SDL_WINDOW_FULLSCREEN) != 0;
		}
		bool GetDesktopFullscreen() {
			return (SDL_GetWindowFlags(draw_context) & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN_DESKTOP;
		}
		SDL_Window* GetWindow() {
			return draw_context;
		}
		bool HasMouseOrInputFocus() {
			return (SDL_GetWindowFlags(draw_context) & (SDL_WINDOW_MOUSE_FOCUS | SDL_WINDOW_INPUT_FOCUS)) != 0;
		}
		bool IsMinimized() {
			return !(SDL_GetWindowFlags(draw_context) & SDL_WINDOW_SHOWN);
		}
		SDL_DisplayMode* SDL2_GetDisplayMode(int width, int height, int refreshrate) {
			SDL_DisplayMode mode;
			int sdlmodes = SDL_GetNumDisplayModes(0);
			int i = 0;

			for (i = 0; i < sdlmodes; i++)
			{
				if (SDL_GetDisplayMode(0, i, &mode) != 0)
					continue;

				if (mode.w == width && mode.h == height && SDL_BITSPERPIXEL(mode.format) >= 24 && mode.refresh_rate == refreshrate)
				{
					return &mode;
				}
			}
			return NULL;
		}

		void Shutdown() {
			if (initiialized){
				SDL_QuitSubSystem(SDL_INIT_VIDEO);
				draw_context = NULL;
				DestroyIcon(icon);
			}
		}

		bool ValidMode(int width, int height, int refreshrate, bool fullscreen) {
			// ignore width / height / bpp if vid_desktopfullscreen is enabled
			if (fullscreen && GetDesktopFullscreen())
				return true;

			if (width < 320)
				return false;

			if (height < 200)
				return false;

			if (fullscreen && SDL2_GetDisplayMode(width, height, refreshrate) == NULL)
				return false;

			return true;
		}

		bool SetMode(int width, int height, int refreshrate, bool fullscreen) {
			int	   temp;
			Uint32 flags;
			char   caption[50];
			int	   previous_display;

			

			//TODO: quake shit
			if (!draw_context) {
				draw_context = SDL_CreateWindow("Tremor", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, width, height, SDL_WINDOW_VULKAN);
				if (!draw_context) {
					std::cout << ("SDL_CreateWindow failed: %s", SDL_GetError()) << std::endl;
					return false;
				}
			}
			//TODO: more quake shit
		}
		class Display {
		public:
			int width, height;
			int refreshRate;
			Display() {
				width = 1920;
				height = 1080;
				refreshRate = 60;
			}
		};
	};
	class COM {
	public:
		Engine* engine;
		COM(Engine e) {
			engine = &e;
			uint32_t uint_value = 0x12345678;
			uint8_t	 bytes[4];
			memcpy(bytes, &uint_value, sizeof(uint32_t));

			/*    U N I X */

			/*
			BE_ORDER:  12 34 56 78
				   U  N  I  X

			LE_ORDER:  78 56 34 12
				   X  I  N  U

			PDP_ORDER: 34 12 78 56
				   N  U  X  I
			*/
			if (bytes[0] != 0x78 || bytes[1] != 0x56 || bytes[2] != 0x34 || bytes[3] != 0x12)
				std::cout << ("Unsupported endianism. Only little endian is supported") << std::endl;
		}
	};
	class SCR{
	public:
		Engine* engine;
		bool disabled_for_loading;
		SCR(Engine e) {
			engine = &e;
			disabled_for_loading = false;
		}
	};
	class CL{
	public:
		Engine* engine;
		CL(Engine e) {
			engine = &e;
		}
		client_static_t s;
	};
	class Con {
	public:
		Engine* engine;
		int linewidth;

		float cursorspeed = 4;

		#define CON_TEXTSIZE (1024 * 1024) // ericw -- was 65536. johnfitz -- new default size
		#define CON_MINSIZE	 16384		   // johnfitz -- old default, now the minimum size

		int buffersize; // johnfitz -- user can now override default

		bool forcedup; // because no entities to refresh

		int	  totallines; // total lines in console scrollback
		int	  backscroll; // lines up from bottom to display
		int	  current;	  // where next message will be printed
		int	  x;		  // offset in current line for next print
		char* text = NULL;

		cvar_t notifytime = { "con_notifytime", "3", CVAR_NONE };			// seconds
		cvar_t logcenterprint = { "con_logcenterprint", "1", CVAR_NONE }; // johnfitz

		char lastcenterstring[1024];				 // johnfitz
		void (*redirect_flush) (const char* buffer); // call this to flush the redirection buffer (for rcon)
		char redirect_buffer[8192];

		#define NUM_CON_TIMES 4
		float times[NUM_CON_TIMES]; // realtime time the line was generated
		// for transparent notify lines

		int vislines;

		bool debuglog = false;

		bool initialized;

		SDL_mutex* mutex;

		int history_line;
		
		Con(Engine e) {
			engine = &e;
		}

		const char* Quakebar(int len)
		{
			static char bar[42];
			int			i;

			len = (int)Min((void*)len, (void*)((int)sizeof(bar) - 2));
			len = (int)Min((void*)len, (void*)linewidth);

			bar[0] = '\35';
			for (i = 1; i < len - 1; i++)
				bar[i] = '\36';
			bar[len - 1] = '\37';

			if (len < linewidth)
			{
				bar[len] = '\n';
				bar[len + 1] = 0;
			}
			else
				bar[len] = 0;

			return bar;
		}

		

		void DebugLog(const char* msg)
		{
			if (log_fd == -1)
				return;

			size_t msg_len = strlen(msg);
			if (_write(log_fd, msg, msg_len) != msg_len)
				return; // Nonsense to supress warning
		}

		static void Linefeed(void)
		{
			// johnfitz -- improved scrolling
			if (con->backscroll)
				con->backscroll++;
			if (con->backscroll > con->totallines - (vid->GetCurrentHeight() >> 3) - 1)
				con->backscroll = con->totallines - (vid->GetCurrentHeight() >> 3) - 1;
			// johnfitz

			con->x = 0;
			con->current++;
			memset(&con->text[(con->current % con->totallines) * con->linewidth], ' ', con->linewidth);
		}


		void Con_Warning(const char* fmt, ...)
		{
			va_list argptr;
			char	msg[MAXPRINTMSG];

			va_start(argptr, fmt);
			qk->vsnprintf(msg, sizeof(msg), fmt, argptr);
			va_end(argptr);

			SafePrintf("\x02Warning: ");
			Printf("%s", msg);
		}


		static void Print(const char* txt)
		{
			int		   y;
			int		   c, l;
			static int cr;
			int		   mask;
			bool   boundary;

			SDL_LockMutex(con->mutex);

			// con_backscroll = 0; //johnfitz -- better console scrolling

			if (txt[0] == 1)
			{
				mask = 128;						// go to colored text
				//S_LocalSound("misc/talk.wav"); // play talk wav
				txt++;
			}
			else if (txt[0] == 2)
			{
				mask = 128; // go to colored text
				txt++;
			}
			else
				mask = 0;

			boundary = true;

			while ((c = *txt))
			{
				if (c <= ' ')
				{
					boundary = true;
				}
				else if (boundary)
				{
					// count word length
					for (l = 0; l < con->linewidth; l++)
						if (txt[l] <= ' ')
							break;

					// word wrap
					if (l != con->linewidth && (con->x + l > con->linewidth))
						con->x = 0;

					boundary = false;
				}

				txt++;

				if (cr)
				{
					con->current--;
					cr = false;
				}

				if (!con->x)
				{
					Linefeed();
					// mark time for transparent overlay
					if (con->current >= 0)
						con->times[con->current % NUM_CON_TIMES] = host->realtime;
				}

				switch (c)
				{
				case '\n':
					con->x = 0;
					break;

				case '\r':
					con->x = 0;
					cr = 1;
					break;

				default: // display character and advance
					y = con->current % con->totallines;
					con->text[y * con->linewidth + con->x] = c | mask;
					con->x++;
					if (con->x >= con->linewidth)
						con->x = 0;
					break;
				}
			}
			SDL_UnlockMutex(con->mutex);
		}

		#define MAXPRINTMSG 4096
		void Printf(const char* fmt, ...)
		{
			va_list			argptr;
			char			msg[MAXPRINTMSG];
			static bool inupdate;

			va_start(argptr, fmt);
			qk->vsnprintf(msg, sizeof(msg), fmt, argptr);
			va_end(argptr);

			if (redirect_flush)
				qk->strlcat(redirect_buffer, msg, sizeof(redirect_buffer));
			// also echo to debugging console
			sys->Printf("%s", msg);

			// log all messages to file
			if (debuglog)
				DebugLog(msg);

			if (!initialized)
				return;

			if (cl->s.state == ca_dedicated)
				return; // no graphics mode

			// write it to the scrollable buffer
			Print(msg);

			// update the screen if the console is displayed
			if (cl->s.signon != SIGNONS && !scr->disabled_for_loading && !t->IsWorker())
			{
				// protect against infinite loop if something in SCR_UpdateScreen calls
				// Con_Printd
				if (!inupdate)
				{
					inupdate = true;
					//SCR_UpdateScreen(false); //remember to uncomment when implemented
					inupdate = false;
				}
			}
		}
	
		void Warning(const char* fmt, ...)
		{
			va_list argptr;
			char	msg[MAXPRINTMSG];

			va_start(argptr, fmt);
			qk->vsnprintf(msg, sizeof(msg), fmt, argptr);
			va_end(argptr);

			SafePrintf("\x02Warning: ");
			Printf("%s", msg);
		}

		void Con_DPrintf(const char* fmt, ...)
		{
			va_list argptr;
			char	msg[MAXPRINTMSG];

			if (!host->developer.value)
				return; // don't confuse non-developers with techie stuff...

			va_start(argptr, fmt);
			qk->vsnprintf(msg, sizeof(msg), fmt, argptr);
			va_end(argptr);

			SafePrintf("%s", msg); // johnfitz -- was Con_Printf
		}

		void SafePrintf(const char* fmt, ...)
		{
			va_list argptr;
			char	msg[MAXPRINTMSG];
			int		temp;

			va_start(argptr, fmt);
			qk->vsnprintf(msg, sizeof(msg), fmt, argptr);
			va_end(argptr);

			SDL_LockMutex(con->mutex);
			temp = scr->disabled_for_loading;
			scr->disabled_for_loading = true;
			Printf("%s", msg);
			scr->disabled_for_loading = temp;
			SDL_UnlockMutex(con->mutex);
		}


	};
	class Cmd {
	public:
		Engine* engine;
		bool wait = false;
		sizebuf_t text;
		typedef enum
		{
			src_client,	 // came in over a net connection as a clc_stringcmd. host_client will be valid during this state.
			src_command, // from the command buffer
			src_server	 // from a svc_stufftext
		} cmd_source_t;
		cmd_source_t cmd_source = src_command;
		typedef void (*xcommand_t) (void);
		typedef struct cmd_function_s
		{
			struct cmd_function_s* next;
			const char* name;
			xcommand_t			   function;
			cmd_source_t		   srctype;
			bool			   dynamic;
		} cmd_function_t;

		cmd_function_t* functions;

		void Wait_f() {
			wait = true;
		}
		Cmd(Engine e) {
			engine = &e;
		}

		cmd_function_t* AddCommand(const char* cmd_name, xcommand_t function, cmd_source_t srctype)
		{
			cmd_function_t* cmd;
			cmd_function_t* cursor, * prev; // johnfitz -- sorted list insert

			// fail if the command is a variable name
			if (cvar->VariableString(cmd_name)[0])
			{
				con->Printf("Cmd_AddCommand: %s already defined as a var\n", cmd_name);
				return NULL;
			}

			// fail if the command already exists
			for (cmd = functions; cmd; cmd = cmd->next)
			{
				if (!strcmp(cmd_name, cmd->name) && cmd->srctype == srctype)
				{
					if (cmd->function != function && function)
						con->Printf("Cmd_AddCommand: %s already defined\n", cmd_name);
					return NULL;
				}
			}

			if (host->initialized)
			{
				cmd = (cmd_function_t*)mem->Alloc(sizeof(*cmd) + strlen(cmd_name) + 1);
				cmd->name = (const char*)strcpy_s((char*)(cmd + 1), sizeof((char*)(cmd + 1)), cmd_name);
				cmd->dynamic = true;
			}
			else
			{
				cmd = (cmd_function_t*)mem->Alloc(sizeof(*cmd));
				cmd->name = cmd_name;
				cmd->dynamic = false;
			}
			cmd->function = function;
			cmd->srctype = srctype;

			// johnfitz -- insert each entry in alphabetical order
			if (functions == NULL || strcmp(cmd->name, functions->name) < 0) // insert at front
			{
				cmd->next = functions;
				functions = cmd;
			}
			else // insert later
			{
				prev = functions;
				cursor = functions->next;
				while ((cursor != NULL) && (strcmp(cmd->name, cursor->name) > 0))
				{
					prev = cursor;
					cursor = cursor->next;
				}
				cmd->next = prev->next;
				prev->next = cmd;
			}
			// johnfitz

			if (cmd->dynamic)
				return cmd;
			return NULL;
		}
	};
	class Cbuf {
	public:
		Engine* engine;
		Cbuf(Engine e) {
			engine = &e;			
			sz->Alloc(&cmd->text, 1 << 18);

		}

		void AddText(const char* text)
		{
			int l;

			l = strlen(text);

			if (&cmd->text.cursize + l >= &cmd->text.maxsize)
			{
				con->Printf("Cbuf_AddText: overflow\n");
				return;
			}

			sz->Write(&cmd->text, text, l);
		}

	};
	class Cvar {
	public:
		Engine* engine;
		static cvar_t* vars;

		Cvar(Engine e) {
			engine = &e;
		}
		void Reset(const char* name);
		void List_f() {

		}
		bool Exists(const char* cmd_name) {
			
		}
		void RegisterVariable(Cvar* variable) {
			char value[512];
			bool set_rom;
			Cvar* cursor, *prev;
		}
		cvar_t* FindVar(const char* var_name)
		{
			cvar_t* var;

			for (var = vars; var; var = var->next)
			{
				if (!strcmp(var_name, var->name))
					return var;
			}

			return NULL;
		}
		const char* VariableString(const char* var_name)
		{
			cvar_t* var;

			var = FindVar(var_name);
			if (!var)
				return cvar_null_string;
			return var->string;
		}
	};
	class Mem {
	public:
		Engine* engine;
		Mem(Engine e) {
			engine = &e;
		}
		void Init() {
			max_thread_stack_alloc_size = MAX_STACK_ALLOC_SIZE;
		}
		void* Alloc(const size_t size) {
			return SDL_calloc(1, size);
		}
		void Free(const void* ptr) {
			if (ptr) {
				free((void*)ptr);
			}
		}
	};
	class Host {
	public:
		Engine* engine;
		parms_t* parms;
		bool initialized;
		double frametime;
		double realtime;
		double oldrealtime;

		cvar_t developer = { "developer", "0", CVAR_NONE };

		int framecount;

		int minimum_memory;

		void Shutdown() {

		}

		Host(Engine e) {
			engine = &e;

			engine->mem = new Mem(*engine);
			engine->t = new Tasks(*engine);
			engine->cbuf = new Cbuf(*engine);

			engine->com = new COM(*engine);


		}
	};
	class Tasks {
	public:
		Engine* engine;
		Tasks(Engine e) {
			engine = &e;

			free_task_queue = CreateTaskQueue(MAX_PENDING_TASKS);
			executable_task_queue = CreateTaskQueue(MAX_EXECUTABLE_TASKS);

			for (uint32_t task_index = 0; task_index < (MAX_PENDING_TASKS - 1); ++task_index)
			{
				TaskQueuePush(free_task_queue, task_index);
			}

			for (uint32_t task_index = 0; task_index < MAX_PENDING_TASKS; ++task_index)
			{
				tasks[task_index].epoch_mutex = SDL_CreateMutex();
				tasks[task_index].epoch_condition = SDL_CreateCond();
			}

			num_workers = (int)(Clamp((void*)1, (void*)SDL_GetCPUCount(), (void*)TASKS_MAX_WORKERS));

			// Fill lookup table to avoid modulo in Task_ExecuteIndexed
			for (int i = 0; i < num_workers; ++i)
			{
				steal_worker_indices[i] = i;
				steal_worker_indices[i + num_workers] = i;
			}

			indexed_task_counters = (task_counter_t*)mem->Alloc(sizeof(task_counter_t) * num_workers * MAX_PENDING_TASKS);
			worker_threads = (SDL_Thread**)mem->Alloc(sizeof(SDL_Thread*) * num_workers);
			for (int i = 0; i < num_workers; ++i)
			{
				worker_threads[i] = SDL_CreateThread(Worker, "Task_Worker", (void*)(intptr_t)i);
			}
		}
		static inline int IndexedTaskCounterIndex(int task_index, int worker_index)
		{
			return (MAX_PENDING_TASKS * worker_index) + task_index;
		}
		static inline uint32_t IndexFromTaskHandle(task_handle_t handle)
		{
			return handle & (MAX_PENDING_TASKS - 1);
		}
		static inline uint64_t EpochFromTaskHandle(task_handle_t handle)
		{
			return handle >> NUM_INDEX_BITS;
		}
		static inline task_handle_t CreateTaskHandle(uint32_t index, int epoch)
		{
			return (task_handle_t)index | ((task_handle_t)epoch << NUM_INDEX_BITS);
		}
		static uint32_t ShuffleIndex(uint32_t i)
		{
			// Swap bits 0-3 and 4-7 to avoid false sharing
			return (i & ~0xFF) | ((i & 0xF) << 4) | ((i >> 4) & 0xF);
		}
		static inline void CPUPause() {
			// Pause the CPU for a short time
			// This is a placeholder for an actual implementation
			_mm_pause();
		}
		static inline void SpinWaitSemaphore(SDL_sem* semaphore)
		{
			int remaining_spins = WAIT_SPIN_COUNT;
			int result = 0;
			while ((result = SDL_SemTryWait(semaphore)) != 0)
			{
				CPUPause();
				if (--remaining_spins == 0)
					break;
			}
			if (result != 0)
				SDL_SemWait(semaphore);
		}
		static task_queue_t* CreateTaskQueue(int capacity)
		{
			assert(capacity > 0);
			assert((capacity & (capacity - 1)) == 0); // Needs to be power of 2
			task_queue_t* queue = (task_queue_t*)mem->Alloc(sizeof(task_queue_t) + (sizeof(atomic_uint32_t) * (capacity - 1)));
			queue->capacity_mask = capacity - 1;
			queue->push_semaphore = SDL_CreateSemaphore(capacity - 1);
			queue->pop_semaphore = SDL_CreateSemaphore(0);
			return queue;
		}
		static inline void TaskQueuePush(task_queue_t* queue, uint32_t task_index)
		{
			SpinWaitSemaphore(queue->push_semaphore);
			uint32_t head = Atomic_LoadUInt32(&queue->head);
			bool cas_successful = false;
			do
			{
				const uint32_t next = (head + 1u) & queue->capacity_mask;
				cas_successful = Atomic_CompareExchangeUInt32(&queue->head, &head, next);
			} while (!cas_successful);

			const uint32_t shuffled_index = ShuffleIndex(head);
			while (Atomic_LoadUInt32(&queue->task_indices[shuffled_index]) != 0u)
				CPUPause();

			ANNOTATE_HAPPENS_BEFORE(&queue->task_indices[shuffled_index]);
			Atomic_StoreUInt32(&queue->task_indices[shuffled_index], task_index + 1);
			SDL_SemPost(queue->pop_semaphore);
		}
		static inline uint32_t TaskQueuePop(task_queue_t* queue)
		{
			SpinWaitSemaphore(queue->pop_semaphore);
			uint32_t tail = Atomic_LoadUInt32(&queue->tail);
			bool cas_successful = false;
			do
			{
				const uint32_t next = (tail + 1u) & queue->capacity_mask;
				cas_successful = Atomic_CompareExchangeUInt32(&queue->tail, &tail, next);
			} while (!cas_successful);

			const uint32_t shuffled_index = ShuffleIndex(tail);
			while (Atomic_LoadUInt32(&queue->task_indices[shuffled_index]) == 0u)
				CPUPause();

			const uint32_t val = Atomic_LoadUInt32(&queue->task_indices[shuffled_index]) - 1;
			Atomic_StoreUInt32(&queue->task_indices[shuffled_index], 0u);
			SDL_SemPost(queue->push_semaphore);
			ANNOTATE_HAPPENS_AFTER(&queue->task_indices[shuffled_index]);

			return val;
		}
		static inline void ExecuteIndexed(int worker_index, task_t* task, uint32_t task_index)
		{
			for (int i = 0; i < num_workers; ++i)
			{
				const int		steal_worker_index = steal_worker_indices[worker_index + i];
				int				counter_index = IndexedTaskCounterIndex(task_index, steal_worker_index);
				task_counter_t* counter = &indexed_task_counters[counter_index];
				uint32_t		index = 0;
				while ((index = Atomic_IncrementUInt32(&counter->index)) < counter->limit)
				{
					((task_indexed_func_t)task->func) (index, task->payload);
				}
			}
		}
		static int Worker(void* data)
		{
			is_worker = true;

			const int worker_index = (intptr_t)data;
			tl_worker_index = worker_index;
			while (true)
			{
				uint32_t task_index = TaskQueuePop(executable_task_queue);
				task_t* task = &tasks[task_index];
				ANNOTATE_HAPPENS_AFTER(task);

				if (task->task_type == TASK_TYPE_SCALAR)
				{
					((task_func_t)task->func) (task->payload);
				}
				else if (task->task_type == TASK_TYPE_INDEXED)
				{
					ExecuteIndexed(worker_index, task, task_index);
				}

#if defined(USE_HELGRIND)
				ANNOTATE_HAPPENS_BEFORE(task);
				qboolean indexed_task = task->task_type == TASK_TYPE_INDEXED;
				if (indexed_task)
				{
					// Helgrind needs to know about all threads
					// that participated in an indexed execution
					SDL_LockMutex(task->epoch_mutex);
					for (int i = 0; i < task->num_dependents; ++i)
					{
						const int task_index = IndexFromTaskHandle(task->dependent_task_handles[i]);
						task_t* dep_task = &tasks[task_index];
						ANNOTATE_HAPPENS_BEFORE(dep_task);
					}
				}
#endif

				if (Atomic_DecrementUInt32(&task->remaining_workers) == 1)
				{
					SDL_LockMutex(task->epoch_mutex);
					for (int i = 0; i < task->num_dependents; ++i)
						t->Submit(task->dependent_task_handles[i]);
					task->epoch += 1;
					SDL_CondBroadcast(task->epoch_condition);
					SDL_UnlockMutex(task->epoch_mutex);
					TaskQueuePush(free_task_queue, task_index);
				}

#if defined(USE_HELGRIND)
				if (indexed_task)
					SDL_UnlockMutex(task->epoch_mutex);
#endif
			}
			return 0;
		}
		int NumWorkers(void)
		{
			return num_workers;
		}
		bool IsWorker(void)
		{
			return is_worker;
		}
		int GetWorkerIndex(void)
		{
			return tl_worker_index;
		}
		task_handle_t Allocate(void)
		{
			uint32_t task_index = TaskQueuePop(free_task_queue);
			task_t* task = &tasks[task_index];
			Atomic_StoreUInt32(&task->remaining_dependencies, 1);
			task->task_type = TASK_TYPE_NONE;
			task->num_dependents = 0;
			task->indexed_limit = 0;
			task->func = NULL;
			return CreateTaskHandle(task_index, task->epoch);
		}
		void AssignFunc(task_handle_t handle, task_func_t func, void* payload, size_t payload_size)
		{
			assert(payload_size <= MAX_PAYLOAD_SIZE);
			task_t* task = &tasks[IndexFromTaskHandle(handle)];
			task->task_type = TASK_TYPE_SCALAR;
			task->func = (void*)func;
			if (payload)
				memcpy(&task->payload, payload, payload_size);
		}
		void AssignIndexedFunc(task_handle_t handle, task_indexed_func_t func, uint32_t limit, void* payload, size_t payload_size)
		{
			assert(payload_size <= MAX_PAYLOAD_SIZE);
			uint32_t task_index = IndexFromTaskHandle(handle);
			task_t* task = &tasks[task_index];
			task->task_type = TASK_TYPE_INDEXED;
			task->func = (void*)func;
			task->indexed_limit = limit;
			uint32_t index = 0;
			uint32_t count_per_worker = (limit + num_workers - 1) / num_workers;
			for (int worker_index = 0; worker_index < num_workers; ++worker_index)
			{
				const int		task_counter_index = IndexedTaskCounterIndex(task_index, worker_index);
				task_counter_t* counter = &indexed_task_counters[task_counter_index];
				Atomic_StoreUInt32(&counter->index, index);
				counter->limit = (uint32_t)(Min((void*)(index + count_per_worker), (void*)limit));
				index += count_per_worker;
			}
			if (payload)
				memcpy(&task->payload, payload, payload_size);
		}
		void Submit(task_handle_t handle)
		{
			uint32_t task_index = IndexFromTaskHandle(handle);
			task_t* task = &tasks[task_index];
			assert(task->epoch == EpochFromTaskHandle(handle));
			ANNOTATE_HAPPENS_BEFORE(task);
			if (Atomic_DecrementUInt32(&task->remaining_dependencies) == 1)
			{
				const int num_task_workers = (task->task_type == TASK_TYPE_INDEXED) ? (int)(Min((void*)task->indexed_limit, (void*)num_workers)) : 1;
				Atomic_StoreUInt32(&task->remaining_workers, num_task_workers);
				for (int i = 0; i < num_task_workers; ++i)
				{
					TaskQueuePush(executable_task_queue, task_index);
				}
			}
		}



	};
	class SZ {
	public:
		Engine* engine;
		SZ(Engine e) {
			engine = &e;
		}
		void Alloc(sizebuf_t* buf, int startsize) {
			if (startsize < 256)
				startsize = 256;
			buf->data = (byte*)mem->Alloc(startsize);
			buf->maxsize = startsize;
			buf->cursize = 0;
		}
		void Free(sizebuf_t* buf) {
			mem->Free(buf->data);
			buf->data = NULL;
			buf->maxsize = 0;
			buf->cursize = 0;
		}
		void Clear(sizebuf_t* buf) {
			buf->cursize = 0;
			buf->overflowed = false;
		}

		void* GetSpace(sizebuf_t* buf, int length) {
			void* data;

			if (buf->cursize + length > buf->maxsize)
			{
				if (!buf->allowoverflow)
					std::cout << ("SZ_GetSpace: overflow without allowoverflow set") << std::endl; 

				if (length > buf->maxsize)
					std::cout << ("SZ_GetSpace: %i is > full buffer size", length) << std::endl;

				std::cout << ("SZ_GetSpace: overflow\n") << std::endl;
				Clear(buf);
				buf->overflowed = true;
			}

			data = buf->data + buf->cursize;
			buf->cursize += length;

			return data;


		}

		void Write(sizebuf_t* buf, const void* data, int length)
		{
			memcpy(GetSpace(buf, length), data, length);
		}

		void Print(sizebuf_t* buf, const char* data)
		{
			int len = strlen(data) + 1;

			if (buf->data[buf->cursize - 1])
			{ /* no trailing 0 */
				memcpy((byte*)GetSpace(buf, len), data, len);
			}
			else
			{ /* write over trailing 0 */
				memcpy((byte*)GetSpace(buf, len - 1) - 1, data, len);
			}
		}
	};
	class MSG {
	public:
		Engine* engine;
		MSG(Engine e) {
			engine = &e;
		}
	};
	class Sys {
	public:
		Engine* engine;

		Sys(Engine e) {
			engine = &e;

		}
		void Init() {
			SetTimerResolution();
			SetDPIAware();

			hinput = GetStdHandle(STD_INPUT_HANDLE);
			houtput = GetStdHandle(STD_OUTPUT_HANDLE);

		}
		static void SetTimerResolution() {
			timeBeginPeriod(1);
		}
		static void SetDPIAware() {
			
			HMODULE					   hUser32, hShcore;
			SetProcessDPIAwarenessFunc setDPIAwareness;
			SetProcessDPIAwareFunc	   setDPIAware;


			hShcore = LoadLibraryA("Shcore.dll");
			hUser32 = LoadLibraryA("user32.dll");
			setDPIAwareness = (SetProcessDPIAwarenessFunc)(hShcore ? GetProcAddress(hShcore, "SetProcessDpiAwareness") : NULL);
			setDPIAware = (SetProcessDPIAwareFunc)(hUser32 ? GetProcAddress(hUser32, "SetProcessDPIAware") : NULL);

			if (setDPIAwareness) /* Windows 8.1+ */
				setDPIAwareness(dpi_monitor_aware);
			else if (setDPIAware) /* Windows Vista-8.0 */
				setDPIAware();

			if (hShcore)
				FreeLibrary(hShcore);
			if (hUser32)
				FreeLibrary(hUser32);
		}
		static void InitSDL() {
			SDL_version v;
			SDL_GetVersion(&v);

			std::cout << "SDL version: " << (int)v.major << "." << (int)v.minor << "." << (int)v.patch << std::endl;

			if (SDL_Init(0) < 0) {
				std::cout << ("SDL_Init failed: %s", SDL_GetError()) << std::endl;
			}

			atexit(AtExit);
		}
		void Error(const char* error, ...) {
			va_list argptr;
			char	text[1024];
			DWORD	dummy;

			host->parms->errstate++;

			va_start(argptr, error);
			qk->vsnprintf(text, sizeof(text), error, argptr);
			va_end(argptr);

			//PR_SwitchQCVM(NULL);

			if (this->engine->isDedicated)
				WriteFile(houtput, errortxt1, strlen(errortxt1), &dummy, NULL);
			/* SDL will put these into its own stderr log,
			   so print to stderr even in graphical mode. */
			fputs(errortxt1, stderr);
			fputs(errortxt2, stderr);
			fputs(text, stderr);
			fputs("\n\n", stderr);
			if (!engine->isDedicated)
				ErrorDialog(text);
			else
			{
				WriteFile(houtput, errortxt2, strlen(errortxt2), &dummy, NULL);
				WriteFile(houtput, text, strlen(text), &dummy, NULL);
				WriteFile(houtput, "\r\n", 2, &dummy, NULL);
				SDL_Delay(3000); /* show the console 3 more seconds */
			}

#ifdef _DEBUG
			__debugbreak();
#endif

			exit(1);
		}
		void Printf(const char* fmt, ...) {
			va_list argptr;
			char	text[1024];
			DWORD	dummy;

			va_start(argptr, fmt);
			qk->vsnprintf(text, sizeof(text), fmt, argptr);
			va_end(argptr);

			if (this->engine->isDedicated)
			{
				WriteFile(houtput, text, strlen(text), &dummy, NULL);
			}
			else
			{
				/* SDL will put these into its own stdout log,
				   so print to stdout even in graphical mode. */
				fputs(text, stdout);
				OutputDebugStringA(text);
			}
		}

		void Sys_Quit()
		{
			host->Shutdown();

			if (this->engine->isDedicated)
				FreeConsole();

			exit(0);
		}

		static void AtExit() {
			SDL_Quit();
		}
	};

	static VID* vid;
	static COM* com;
	static SCR* scr;
	static CL* cl;
	static Con* con;
	static Cmd* cmd;
	static Cvar* cvar;
	static Mem* mem;
	static Host* host;
	static SZ* sz;
	static MSG* msg;
	static Sys* sys;
	static q* qk;
	static Tasks* t;
	static Cbuf* cbuf;

	Engine() {		
		
		sys = new Sys(*this);

		
		sys->InitSDL();
		sys->Init();

		sys->Printf("Detected %d CPUs.\n", SDL_GetCPUCount());
		sys->Printf("Initializing %s.\n", ENGINE_NAME_AND_VER);

		host = new Host(*this);


		scr = new SCR(*this);
		cl = new CL(*this);
		con = new Con(*this);
		cmd = new Cmd(*this);
		cvar = new Cvar(*this);
		sz = new SZ(*this);
		vid = new VID(*this);
		msg = new MSG(*this);
		qk = new q(*this);
	}
};

// needs to be here, otherwise compiler will bitch
Engine::VID* Engine::vid = nullptr;
Engine::COM* Engine::com = nullptr;
Engine::SCR* Engine::scr = nullptr;
Engine::CL* Engine::cl = nullptr;
Engine::Con* Engine::con = nullptr;
Engine::Cmd* Engine::cmd = nullptr;
Engine::Cvar* Engine::cvar = nullptr;
Engine::Mem* Engine::mem = nullptr;
Engine::Host* Engine::host = nullptr;
Engine::SZ* Engine::sz = nullptr;
Engine::MSG* Engine::msg = nullptr;
Engine::Sys* Engine::sys = nullptr;
Engine::q* Engine::qk = nullptr;
Engine::Tasks* Engine::t = nullptr;
Engine::Cbuf* Engine::cbuf = nullptr;

int main(int argc, char *argv[]) {

	
	auto bla = new Engine();




	return 0;
}