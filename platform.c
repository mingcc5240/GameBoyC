/* *****************
 * GameBoy emulator written in C.
 * Credits: Greg Tourville
 ********************/
// v0.04

// Includes
#define SDL_MAIN_HANDLED
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// SDL
#include <SDL2\SDL.h>

// GameBoy
#include "gameboy.h"
#include "minigb_apu.h"

// emulator data
int running = 1;
SDL_Event event;
u8   frameskip = 0;
u8   frames;
u32  f0_ticks;
u32  f1_ticks;
u16  fps;

// color schemes
u32 COLORS_Y[4] = {0xFFFFFFFF, 0x99999999, 0x44444444, 0x00000000};
u32 COLORS_R[4] = {0xFFFFFFFF, 0xFFFF9999, 0xFF444499, 0x00000000};
u32 COLORS_G[4] = {0xFFFFFFFF, 0xFF99FF99, 0xFF994444, 0x00000000};
u32 COLORS_B[4] = {0xFFFFFFFF, 0xFF9999FF, 0xFF449944, 0x00000000};
u32 COLORS_O[4] = {0xFFFFFFEE, 0xFFFFFF66, 0xFF444499, 0x00000000};
u32* color_map;

// gameboy color conversion
u32 ColorTo32(u16 cgb)
{
	u8 r = (cgb & 0x001F) << 3;// * 0xFF / 0x1F;
	u8 g = ((cgb >>  5) & 0x001F) << 3;// * 0xFF / 0x1F;
	u8 b = ((cgb >> 10) & 0x001F) << 3;// * 0xFF / 0x1F;

	//cy = (299*r + 587*g + 114*b) / 1000;
	//cb = (-16874*r - 33126*g + 50000*b + 12800000) / 100000;
	//cr = (50000*r - 41869*g - 8131*b + 12800000) / 100000;

	//*v0++ = *v1++ = (cy<<24) | (cb<<16) | (cy<<8) | cr;
	return 0xFF000000 | (b << 16) | (g << 8) | r;
}

// key mappings
#define NUM_KEYS    8
u32 KEYS[] =
{
	SDLK_RIGHT, // control map one
	SDLK_LEFT,
	SDLK_UP,
	SDLK_DOWN,
	SDLK_z,
	SDLK_x,
	SDLK_RSHIFT,
	SDLK_RETURN,
	SDLK_d,     // control map two
	SDLK_a,
	SDLK_w,
	SDLK_s,
	SDLK_SPACE,
	SDLK_BACKSPACE,
	SDLK_LSHIFT,
	SDLK_ESCAPE
};

// strings
char  window_caption[100];
char  window_caption_fps[100];
//char  rom_file_buf[260];
//char* rom_file = rom_file_buf;
char  save_file[260];

// pointers
u8*   rom;
u32   rom_size;
u8*   save;
u32   save_size;
FILE* rom_f;
FILE* save_f;


int main(int argc, char **argv)
{
	int     i, x, y;
	u8      j;
	u32     romread;
	u32     old_ticks;
	u32     new_ticks;
	int     delay;
	u32*    s;
	int     quit_seq;
	u32		fb[LCD_HEIGHT][LCD_WIDTH];
	char	*rom_file = NULL;
	SDL_Window *win = NULL;
	SDL_Renderer *ren = NULL;
	SDL_Texture *tex = NULL;

	int c;

	while((c = getopt(argc, argv, "hm:f:")) != -1)
	{
		switch (c) {
			case 'h':
				printf("Usage: %s [-m magnification] -f GB_ROM\n", argv[0]);
				return 0;
			case 'f':
				rom_file = optarg;
				break;
			default:
				printf("?? getopt returned character code 0%o ??\n", c);
				return -1;
		}
	}

	if(rom_file == NULL)
	{
		printf("Please specify a file to load.\nUse -h to see help.\n");
		return -1;
	}

	// Load ROM file
	if((access(rom_file, F_OK) != -1) &&
			((rom_f = fopen(rom_file, "rb")) != NULL))
	{
		printf("%s: Opening %s.\n", __func__, rom_file);
		(rom_f = fopen(rom_file, "rb")) == NULL;
	}
	else
	{
		printf("%s: File \"%s\" not found.\n", __func__, rom_file);
		return -1;
	}

	// Init SDL
	SDL_setenv("SDL_AUDIODRIVER", "directsound", SDL_TRUE);
	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
  	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
	SDL_CreateWindowAndRenderer(LCD_WIDTH*4, LCD_HEIGHT*4, SDL_WINDOW_RESIZABLE,
		&win, &ren);
	SDL_assert(win != NULL);
	SDL_assert(ren != NULL);
	tex = SDL_CreateTexture(ren,
				    SDL_PIXELFORMAT_RGBA32,
				    SDL_TEXTUREACCESS_STREAMING,
				    LCD_WIDTH, LCD_HEIGHT);
	SDL_assert(tex != NULL);


    	SDL_AudioDeviceID dev;

        	SDL_AudioSpec want, have;

	want.freq = AUDIO_SAMPLE_RATE;
	want.format   = AUDIO_S16,
	want.channels = 2;
	want.samples = AUDIO_SAMPLES;
	want.callback = audio_callback;
	want.userdata = NULL;

	if((dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0)) == 0)
	{
			exit(EXIT_FAILURE);
	}
	audio_init();
	SDL_PauseAudioDevice(dev, 0);

         /*
    	if(!SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1")){
		exit(EXIT_FAILURE);
      	} */
        	
	fseek(rom_f, 0, SEEK_END);
	rom_size = ftell(rom_f);
	rewind(rom_f);
	rom = (u8*)malloc(rom_size);
	for (i = 0; i < rom_size; i++)
		rom[i] = 0xFF;

	romread = fread(rom, sizeof(u8), rom_size, rom_f);
	fclose(rom_f);

	// Load SAVE file (if it exists)
	sprintf(save_file, "%s.sav", rom_file);
	save_size = GetSaveSize(rom);
	save = (u8*)malloc(save_size);
	save_f = fopen(save_file, "rb");
	if (save_f)
	{
		fseek(save_f, 0, SEEK_SET);
		fread(save, sizeof(u8), save_size, save_f);
		fclose(save_f);
	}

	// Start the emulator
	LoadROM(rom, rom_size, save, save_size);

	color_map = COLORS_Y;

	new_ticks= SDL_GetTicks();
	f1_ticks = new_ticks;
	quit_seq = 0;
	while (running && quit_seq != 3)
	{
		// handle input / events
		while (SDL_PollEvent(&event))
		{
			if (event.type == SDL_QUIT)
				running = 0;
			if (event.type == SDL_KEYDOWN)
			{
				if (event.key.keysym.sym == SDLK_0)
				{
					frameskip = 0;
					SetFrameSkip(0);
				}
				else if (event.key.keysym.sym == SDLK_1)
				{
					frameskip = 1;
					SetFrameSkip(0);
				}
				else if (event.key.keysym.sym == SDLK_2)
				{
					frameskip = 2;
					SetFrameSkip(2);
				}
				else if (event.key.keysym.sym == SDLK_3)
				{
					frameskip = 3;
					SetFrameSkip(3);
				}
				else if (event.key.keysym.sym == SDLK_9)
				{
					frameskip = 9;
					SetFrameSkip(10);
				}
				else if (event.key.keysym.sym == SDLK_r)
					color_map = COLORS_R;
				else if (event.key.keysym.sym == SDLK_g)
					color_map = COLORS_G;
				else if (event.key.keysym.sym == SDLK_b)
					color_map = COLORS_B;
				else if (event.key.keysym.sym == SDLK_o)
					color_map = COLORS_O;
				else if (event.key.keysym.sym == SDLK_y)
					color_map = COLORS_Y;
				else if (event.key.keysym.sym == SDLK_q)
					quit_seq |= 1;
				else if (event.key.keysym.sym == SDLK_LCTRL)
					quit_seq |= 2;
				else
					for (j = 0; j < 2*NUM_KEYS; j++)
						if (KEYS[j] == event.key.keysym.sym)
						{
							KeyPress(j%NUM_KEYS);
							break;
						}
			}
			else if(event.type == SDL_KEYUP)
			{
				for (j = 0; j < 2*NUM_KEYS; j++)
					if (KEYS[j] == event.key.keysym.sym)
					{
						KeyRelease(j%NUM_KEYS);
						break;
					}
				if (event.key.keysym.sym == SDLK_q
						|| event.key.keysym.sym == SDLK_LCTRL)
				{
					quit_seq = 0;
				}
			}
		}

		old_ticks = SDL_GetTicks();
		usleep(16 - (new_ticks - old_ticks));

		// emulate frame
		RunFrame();

		if (gb_framecount == 0)
		{
			// convert colors
			if (cgb_enable)
				for (y = 0; y < LCD_HEIGHT; y++)
					for (x = 0; x < LCD_WIDTH; x++)
						fb[y][x] = ColorTo32(cgb_fb[y][x]);
			else
				for (y = 0; y < LCD_HEIGHT; y++)
					for (x = 0; x < LCD_WIDTH; x++)
						fb[y][x] = color_map[gb_fb[y][x] & 3];

			// render
			SDL_UpdateTexture(tex, NULL, fb, LCD_WIDTH * sizeof(uint32_t));
			SDL_RenderClear(ren);
			SDL_RenderCopy(ren, tex, NULL, NULL);
			SDL_RenderPresent(ren);

			//old_ticks = new_ticks;
			new_ticks = SDL_GetTicks();
			frames++;
			if (frames % 0x80 == 0)
			{
				f0_ticks = f1_ticks;
				f1_ticks = new_ticks;
				fps = (128*1000)/(f1_ticks - f0_ticks) * (gb_frameskip ? gb_frameskip : 1);
				sprintf(window_caption_fps, "%s - %u fps", window_caption, fps);
				SDL_SetWindowTitle(win, window_caption_fps);
			}

			// Cap at 60FPS unless using frameskip
			if (!frameskip)
			{
				delay = 16 - (new_ticks - old_ticks);
				SDL_Delay(delay > 0 ? delay : 0);
			}
		}
	}

	// Save game before exit
	if (save_size)        {
		save_f = fopen(save_file, "wb");
		if (save_f)
		{
			fseek(save_f, 0, SEEK_SET);
			fwrite(save, 1, save_size, save_f);
			fclose(save_f);
		}
	}

	// Clean up
	free(rom);
	free(save);
	SDL_Quit();

	return 0;
}

